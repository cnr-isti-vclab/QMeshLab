#include "isoparamfilterplugin.h"

#include "document.h"
#include "filterparam.h"
#include "layerdata.h"
#include "meshfilterpluginmanager.h"
#include "vcgmesh.h"

#include <QObject>
#include <QStringList>

#include <cstdarg>
#include <cstdio>
#include <memory>
#include <mutex>
#include <vector>

// The reference implementation narrates itself with ~120 printf/fprintf calls -- a few
// hundred lines per run. Intercepting them here, before the headers are included, routes
// the whole narration into the document log without touching a line of the vendored code.
// These are header-inline, so every call in them compiles in this translation unit.
namespace isoparam_capture {

std::mutex &mutex();
void append(const char *text);

inline int captured_printf(const char *format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    const int n = std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    append(buffer);
    return n;
}

inline int captured_fprintf(std::FILE *, const char *format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    const int n = std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    append(buffer);
    return n;
}

} // namespace isoparam_capture

#define printf isoparam_capture::captured_printf
#define fprintf isoparam_capture::captured_fprintf

// Order matters: the rest of the vendored tree assumes iso_parametrization.h has already
// been seen and does not include it itself.
#include "upstream/iso_parametrization.h"
#include "upstream/parametrizator.h"
#include "upstream/diam_parametrization.h"
#include "upstream/diamond_sampler.h"
#include "upstream/iso_transfer.h"

#undef printf
#undef fprintf

#include <vcg/complex/append.h>
#include <vcg/complex/algorithms/clean.h>
#include <vcg/complex/algorithms/update/normal.h>
#include <vcg/complex/algorithms/update/bounding.h>
#include <vcg/complex/algorithms/update/topology.h>

namespace isoparam_capture {

namespace {
std::mutex g_mutex;
QStringList *g_sink = nullptr;
QString g_partial;
}

std::mutex &mutex() { return g_mutex; }

void append(const char *text)
{
    if (!g_sink || !text)
        return;
    // The reference code prints in fragments and newlines land where they land, so lines
    // are reassembled here rather than one log entry per printf.
    g_partial += QString::fromLocal8Bit(text);
    int nl;
    while ((nl = int(g_partial.indexOf(QLatin1Char('\n')))) >= 0) {
        const QString line = g_partial.left(nl).trimmed();
        g_partial.remove(0, nl + 1);
        if (!line.isEmpty())
            *g_sink << line;
    }
}

// Redirects the vendored narration into `sink` for its lifetime.
class Scope
{
public:
    explicit Scope(QStringList &sink) { g_sink = &sink; g_partial.clear(); }
    ~Scope()
    {
        if (g_sink && !g_partial.trimmed().isEmpty())
            *g_sink << g_partial.trimmed();
        g_sink = nullptr;
        g_partial.clear();
    }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
};

} // namespace isoparam_capture

namespace {

constexpr QLatin1StringView kFilterAbstractDomain("parametrize_by_abstract_domain");
constexpr QLatin1StringView kFilterRemesh("remesh_by_abstract_domain");
constexpr QLatin1StringView kFilterAtlas("create_atlased_mesh_from_abstract_domain");
constexpr QLatin1StringView kFilterTransfer("transfer_abstract_domain_to_another_layer");
const QString kDomainKey = QStringLiteral("qmeshlab.filter.isoparam/abstract_domain");

MeshFilterRunResult fail(const QString &message)
{
    MeshFilterRunResult result;
    result.errorMessage = message;
    return result;
}

// The abstract domain and the parametrization built on it. Immutable once installed, as
// LayerData requires: the later filters of this family read it and never edit it.
class AbstractDomainData : public LayerData
{
public:
    AbstractDomainData(std::unique_ptr<AbstractMesh> abstractMesh,
                       std::unique_ptr<ParamMesh> paramMesh,
                       std::unique_ptr<IsoParametrization> iso,
                       int domainFaces, float stretch, float distortion)
        : m_abstractMesh(std::move(abstractMesh))
        , m_paramMesh(std::move(paramMesh))
        , m_iso(std::move(iso))
        , m_domainFaces(domainFaces)
        , m_stretch(stretch)
        , m_distortion(distortion)
    {
    }

    QString describe() const override
    {
        return QObject::tr("abstract domain, %1 faces, stretch %2, distortion %3%")
            .arg(m_domainFaces)
            .arg(double(m_stretch), 0, 'f', 4)
            .arg(double(m_distortion), 0, 'f', 2);
    }

    // The domain is a parametrization *of this geometry*; move the vertices and it means
    // nothing. Core drops it for us when the layer's geometry changes.
    bool survivesGeometryChange() const override { return false; }

    std::size_t approximateBytes() const override
    {
        // Dominated by the two meshes the parametrization holds.
        return m_iso ? std::size_t(m_domainFaces) * 512u : 0u;
    }

    IsoParametrization *parametrization() const { return m_iso.get(); }
    const AbstractMesh *abstractMesh() const { return m_abstractMesh.get(); }

private:
    // IsoParametrization only holds pointers to these two -- MeshLab allocates them with a
    // bare new and never frees them. Owning them here, and declaring them before m_iso so
    // they outlive it, makes the whole thing a value with a lifetime.
    std::unique_ptr<AbstractMesh> m_abstractMesh;
    std::unique_ptr<ParamMesh> m_paramMesh;
    std::unique_ptr<IsoParametrization> m_iso;
    int m_domainFaces;
    float m_stretch;
    float m_distortion;
};

QString describeReturnCode(IsoParametrizator::ReturnCode code)
{
    switch (code) {
    case IsoParametrizator::MultiComponent:
        return QObject::tr("the mesh has more than one connected component");
    case IsoParametrizator::NonSizeCons:
        return QObject::tr("the mesh is too small for the requested abstract domain");
    case IsoParametrizator::NonManifoldE:
        return QObject::tr("the mesh has non-manifold edges");
    case IsoParametrizator::NonManifoldV:
        return QObject::tr("the mesh has non-manifold vertices");
    case IsoParametrizator::NonWatertigh:
        return QObject::tr("the mesh is not watertight");
    case IsoParametrizator::FailParam:
        return QObject::tr("the parametrization did not converge");
    case IsoParametrizator::Done:
        break;
    }
    return QObject::tr("unknown failure");
}

} // namespace

// Fetches the domain a previous run of the main filter left on a layer, with the message
// the user needs when it is not there -- which is the normal case for anyone who reaches
// one of these three filters first.
const AbstractDomainData *domainOf(const Document &doc, int meshIndex, QString &error)
{
    const LayerDataPtr data = doc.layerData(meshIndex, kDomainKey);
    if (!data) {
        error = QObject::tr(
            "'%1' has no abstract domain. Run \"Parametrize by Abstract Domain\" on it first; "
            "note that the domain is dropped whenever the layer's geometry changes.")
            .arg(meshIndex >= 0 && meshIndex < doc.meshCount() ? doc.mesh(meshIndex).name
                                                               : QString());
        return nullptr;
    }
    return static_cast<const AbstractDomainData *>(data.get());
}

// Uniform remeshing: every domain triangle is subdivided recursively, so the sampling
// rate is a resolution multiplier rather than a target count.
MeshFilterRunResult runRemesh(const FilterParams &params, Document &doc, int index)
{
    QString error;
    const AbstractDomainData *domain = domainOf(doc, index, error);
    if (!domain)
        return fail(error);

    const int samplingRate = params.getInt(QStringLiteral("samplingRate"), 10);
    if (samplingRate < 2)
        return fail(QObject::tr("The sampling rate must be at least 2."));

    doc.beginFilterProgress(QObject::tr("Remesh by Abstract Domain"));
    QStringList narration;
    VCGMesh remeshed;
    bool done = false;
    int diamonds = 0, inFace = 0, inEdge = 0, inStar = 0, merged = 0;
    {
        const std::lock_guard<std::mutex> guard(isoparam_capture::mutex());
        const isoparam_capture::Scope capture(narration);
        DiamSampler sampler;
        sampler.Init(domain->parametrization());
        done = sampler.SamplePos(samplingRate);
        if (done) {
            sampler.GetMesh<VCGMesh>(remeshed);
            sampler.getResData(diamonds, inFace, inEdge, inStar, merged);
        }
    }
    for (const QString &line : narration)
        doc.writeLog(QStringLiteral("[isoparam] %1").arg(line),
                     Document::LogSource::VCG, Document::LogLevel::Debug);

    if (!done || remeshed.FN() <= 0) {
        const QString message = QObject::tr("Remeshing over the abstract domain failed.");
        doc.finishFilterProgress(false, message);
        return fail(message);
    }

    vcg::tri::UpdateBounding<VCGMesh>::Box(remeshed);
    vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(remeshed);
    const int newIndex = doc.addMesh(
        remeshed, QObject::tr("Remeshed - %1").arg(doc.mesh(index).name),
        vcg::tri::io::Mask::IOM_VERTCOORD | vcg::tri::io::Mask::IOM_FACEINDEX
            | vcg::tri::io::Mask::IOM_VERTNORMAL | vcg::tri::io::Mask::IOM_FACENORMAL);
    if (newIndex < 0) {
        const QString message = QObject::tr("Could not add the remeshed layer.");
        doc.finishFilterProgress(false, message);
        return fail(message);
    }
    doc.setMeshTransform(newIndex, doc.mesh(index).transform);

    MeshFilterRunResult result;
    result.success = true;
    result.documentModified = true;
    result.newMeshIndices.push_back(newIndex);
    result.infoMessages
        << QObject::tr("Remeshed to %1 vertices and %2 faces.")
               .arg(remeshed.VN()).arg(remeshed.FN())
        << QObject::tr("Interpolation domains: %1 in face, %2 in diamond, %3 in star.")
               .arg(inFace).arg(inEdge).arg(inStar)
        << QObject::tr("Merged %1 vertices.").arg(merged);
    doc.finishFilterProgress(true, QObject::tr("Remeshed over the abstract domain."));
    return result;
}

// One chart per diamond of the domain, packed into a single atlas: the parametrization
// arrives as per-wedge UVs on a new layer.
MeshFilterRunResult runAtlasedMesh(const FilterParams &params, Document &doc, int index)
{
    QString error;
    const AbstractDomainData *domain = domainOf(doc, index, error);
    if (!domain)
        return fail(error);

    const double borderSize = params.getDouble(QStringLiteral("borderSize"), 0.1);

    doc.beginFilterProgress(QObject::tr("Create Atlased Mesh from Abstract Domain"));
    QStringList narration;
    VCGMesh atlased;
    atlased.face.EnableWedgeTexCoord();
    {
        const std::lock_guard<std::mutex> guard(isoparam_capture::mutex());
        const isoparam_capture::Scope capture(narration);
        DiamondParametrizator diamond;
        diamond.Init(domain->parametrization());
        diamond.SetCoordinates<VCGMesh>(atlased, float(borderSize));
    }
    for (const QString &line : narration)
        doc.writeLog(QStringLiteral("[isoparam] %1").arg(line),
                     Document::LogSource::VCG, Document::LogLevel::Debug);

    if (atlased.FN() <= 0) {
        const QString message = QObject::tr("The atlased mesh came out empty.");
        doc.finishFilterProgress(false, message);
        return fail(message);
    }

    vcg::tri::UpdateBounding<VCGMesh>::Box(atlased);
    vcg::tri::UpdateNormal<VCGMesh>::PerFaceNormalized(atlased);
    const int newIndex = doc.addMesh(
        atlased, QObject::tr("Atlased - %1").arg(doc.mesh(index).name),
        vcg::tri::io::Mask::IOM_VERTCOORD | vcg::tri::io::Mask::IOM_FACEINDEX
            | vcg::tri::io::Mask::IOM_WEDGTEXCOORD | vcg::tri::io::Mask::IOM_FACENORMAL);
    if (newIndex < 0) {
        const QString message = QObject::tr("Could not add the atlased layer.");
        doc.finishFilterProgress(false, message);
        return fail(message);
    }
    doc.setMeshTransform(newIndex, doc.mesh(index).transform);

    MeshFilterRunResult result;
    result.success = true;
    result.documentModified = true;
    result.newMeshIndices.push_back(newIndex);
    result.infoMessages
        << QObject::tr("Atlased mesh: %1 vertices, %2 faces, per-wedge UVs.")
               .arg(atlased.VN()).arg(atlased.FN());
    doc.finishFilterProgress(true, QObject::tr("Built the atlased mesh."));
    return result;
}

// Carries a domain onto a second, similar layer by closest point.
MeshFilterRunResult runTransfer(const FilterParams &params, Document &doc)
{
    const int sourceIndex = params.getMesh(QStringLiteral("sourceMesh"), doc.currentMeshIndex());
    const int targetIndex = params.getMesh(QStringLiteral("targetMesh"), doc.currentMeshIndex());
    if (sourceIndex < 0 || sourceIndex >= doc.meshCount()
        || targetIndex < 0 || targetIndex >= doc.meshCount())
        return fail(QObject::tr("Both a source and a target layer are needed."));
    if (sourceIndex == targetIndex)
        return fail(QObject::tr("The source and target layers must be different."));

    QString error;
    const AbstractDomainData *domain = domainOf(doc, sourceIndex, error);
    if (!domain)
        return fail(error);

    Document::MeshEntry &target = doc.mesh(targetIndex);
    if (target.mesh.FN() <= 0)
        return fail(QObject::tr("The target layer needs faces."));

    doc.beginFilterProgress(QObject::tr("Transfer Abstract Domain to Another Layer"));

    VCGMeshFFAdjScope ffAdj(target.mesh);
    VCGMeshMarkScope faceMark(target.mesh);
    target.mesh.vert.EnableTexCoord();
    target.mesh.face.EnableWedgeTexCoord();
    vcg::tri::UpdateTopology<VCGMesh>::FaceFace(target.mesh);

    // MeshLab moves the domain: it transfers, clears the source's parametrization and
    // re-points the same meshes at the target, which leaves the source without one. We
    // cannot do that -- an installed domain is immutable and undo snapshots may share it --
    // so the target gets its own copy of the abstract mesh and a param mesh built from
    // itself. The source keeps its domain, which is the better behaviour anyway.
    QStringList narration;
    auto abstractCopy = std::make_unique<AbstractMesh>();
    auto paramCopy = std::make_unique<ParamMesh>();
    auto iso = std::make_unique<IsoParametrization>();
    bool ok = false;
    {
        const std::lock_guard<std::mutex> guard(isoparam_capture::mutex());
        const isoparam_capture::Scope capture(narration);
        IsoTransfer transfer;
        transfer.Transfer<VCGMesh>(*domain->parametrization(), target.mesh);

        vcg::tri::Append<AbstractMesh, AbstractMesh>::MeshCopyConst(
            *abstractCopy, *domain->abstractMesh());
        iso->AbsMesh() = abstractCopy.get();
        ok = iso->SetParamMesh<VCGMesh>(&target.mesh, paramCopy.get());
    }
    for (const QString &line : narration)
        doc.writeLog(QStringLiteral("[isoparam] %1").arg(line),
                     Document::LogSource::VCG, Document::LogLevel::Debug);

    if (!ok) {
        const QString message =
            QObject::tr("The transferred domain could not be initialized on '%1'. The two "
                        "layers must be similar in shape and already aligned.")
                .arg(target.name);
        doc.finishFilterProgress(false, message);
        return fail(message);
    }

    target.ioMask |= vcg::tri::io::Mask::IOM_VERTTEXCOORD
        | vcg::tri::io::Mask::IOM_WEDGTEXCOORD;
    doc.markMeshGeometryChanged(
        targetIndex, QObject::tr("Transferred an abstract domain onto '%1'.").arg(target.name));
    doc.setLayerData(targetIndex, kDomainKey,
                     std::make_shared<AbstractDomainData>(
                         std::move(abstractCopy), std::move(paramCopy), std::move(iso),
                         int(domain->parametrization()->AbsMesh()->fn), 0.0f, 0.0f));

    MeshFilterRunResult result;
    result.success = true;
    result.documentModified = true;
    result.infoMessages
        << QObject::tr("Transferred the abstract domain from '%1' to '%2'.")
               .arg(doc.mesh(sourceIndex).name, target.name)
        << QObject::tr("'%1' keeps its own domain.").arg(doc.mesh(sourceIndex).name);
    doc.finishFilterProgress(true, QObject::tr("Abstract domain transferred."));
    return result;
}

QString IsoParamFilterPlugin::pluginId() const
{
    return QStringLiteral("qmeshlab.filter.isoparam");
}

QString IsoParamFilterPlugin::name() const
{
    return QObject::tr("Isoparametrization Filters");
}

MeshFilterRunResult IsoParamFilterPlugin::runFilter(
    const QString &filterId,
    const FilterParams &params,
    Document &doc) const
{
    const int index = doc.currentMeshIndex();
    if (index < 0 || index >= doc.meshCount())
        return fail(QObject::tr("No layer selected."));

    if (filterId == QString::fromLatin1(kFilterRemesh))
        return runRemesh(params, doc, index);
    if (filterId == QString::fromLatin1(kFilterAtlas))
        return runAtlasedMesh(params, doc, index);
    if (filterId == QString::fromLatin1(kFilterTransfer))
        return runTransfer(params, doc);
    if (filterId != QString::fromLatin1(kFilterAbstractDomain))
        return fail(QObject::tr("Unknown filter id: %1").arg(filterId));

    Document::MeshEntry &entry = doc.mesh(index);
    if (entry.mesh.FN() <= 0)
        return fail(QObject::tr("The layer needs faces."));

    const int minFaces = params.getInt(QStringLiteral("minDomainFaces"), 150);
    const int maxFaces = params.getInt(QStringLiteral("maxDomainFaces"), 200);
    if (maxFaces < minFaces) {
        return fail(QObject::tr("The largest abstract domain (%1 faces) must not be "
                                "smaller than the smallest (%2).")
                        .arg(maxFaces).arg(minFaces));
    }
    const int accuracy = params.getInt(QStringLiteral("convergenceAccuracy"), 1);
    const bool doubleStep = params.getBool(QStringLiteral("doubleStep"), true);

    // What the algorithm reads and writes on the mesh it is given.
    VCGMeshFFAdjScope ffAdj(entry.mesh);
    VCGMeshMarkScope faceMark(entry.mesh);
    VCGMeshVertexMarkScope vertexMark(entry.mesh);
    entry.mesh.vert.EnableTexCoord();
    vcg::tri::UpdateTopology<VCGMesh>::FaceFace(entry.mesh);

    doc.beginFilterProgress(QObject::tr("Parametrize by Abstract Domain"));

    IsoParametrizator parametrizator;
    IsoParametrizator::StopMode stopMode = IsoParametrizator::SM_Corr;
    const QString criteria = params.getEnum(QStringLiteral("stopCriteria"));
    if (criteria == QStringLiteral("heuristic"))
        stopMode = IsoParametrizator::SM_Euristic;
    else if (criteria == QStringLiteral("regularity"))
        stopMode = IsoParametrizator::SM_Reg;
    else if (criteria == QStringLiteral("l2"))
        stopMode = IsoParametrizator::SM_L2;

    vcg::CallBackPos *callback =
        doc.progressCallback() ? doc.progressCallback() : vcg::DummyCallBackPos;
    parametrizator.SetParameters(callback, minFaces, maxFaces - minFaces, stopMode, accuracy);

    QStringList narration;
    IsoParametrizator::ReturnCode code = IsoParametrizator::FailParam;
    {
        const std::lock_guard<std::mutex> guard(isoparam_capture::mutex());
        const isoparam_capture::Scope capture(narration);
        vcg::tri::ParamEdgeCollapseParameter pecp;
        code = parametrizator.Parametrize<VCGMesh>(&entry.mesh, pecp, doubleStep);
    }
    for (const QString &line : narration)
        doc.writeLog(QStringLiteral("[isoparam] %1").arg(line),
                     Document::LogSource::VCG, Document::LogLevel::Debug);

    if (code != IsoParametrizator::Done) {
        const QString message =
            QObject::tr("Could not build an abstract domain: %1.").arg(describeReturnCode(code));
        doc.finishFilterProgress(false, message);
        return fail(message);
    }

    float aggregate = 0.0f;
    float stretch = 0.0f;
    int domainFaces = 0;
    parametrizator.getValues(aggregate, stretch, domainFaces);

    auto abstractMesh = std::make_unique<AbstractMesh>();
    auto paramMesh = std::make_unique<ParamMesh>();
    parametrizator.ExportMeshes(*paramMesh, *abstractMesh);

    auto iso = std::make_unique<IsoParametrization>();
    if (!iso->Init(abstractMesh.get(), paramMesh.get())) {
        const QString message = QObject::tr("The abstract domain could not be initialized.");
        doc.finishFilterProgress(false, message);
        return fail(message);
    }
    iso->CopyParametrization<VCGMesh>(&entry.mesh);

    // Announce the mesh change *before* attaching the domain, not after. The UVs written
    // above are a change to this layer, and markMeshGeometryChanged drops plugin data that
    // does not survive one -- which this deliberately does not. Attaching first would
    // install the domain and then immediately throw it away.
    entry.ioMask |= vcg::tri::io::Mask::IOM_VERTTEXCOORD;
    doc.markMeshGeometryChanged(
        index, QObject::tr("Built an abstract domain for '%1'.").arg(entry.name));

    doc.setLayerData(index, kDomainKey,
                     std::make_shared<AbstractDomainData>(
                         std::move(abstractMesh), std::move(paramMesh), std::move(iso),
                         domainFaces, stretch, aggregate * 100.0f));

    MeshFilterRunResult result;
    result.success = true;
    result.documentModified = true;
    result.infoMessages
        << QObject::tr("Abstract domain: %1 faces.").arg(domainFaces)
        << QObject::tr("One-way stretch efficiency: %1.").arg(double(stretch), 0, 'f', 4)
        << QObject::tr("Area and angle distortion: %1%.").arg(double(aggregate) * 100.0, 0, 'f', 2);
    doc.finishFilterProgress(true, QObject::tr("Abstract domain built."));
    return result;
}

void registerIsoParamFilterPlugin(MeshFilterPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<IsoParamFilterPlugin>());
}
