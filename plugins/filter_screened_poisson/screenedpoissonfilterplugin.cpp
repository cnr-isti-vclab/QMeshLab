#include "screenedpoissonfilterplugin.h"

#include "document.h"
#include "meshfilterpluginmanager.h"
#include "poisson_utils.h"
#include "upstream_backend.h"

#include <wrap/io_trimesh/io_mask.h>
#include <vcg/complex/allocate.h>
#include <vcg/complex/algorithms/update/bounding.h>
#include <vcg/complex/algorithms/update/normal.h>

#include <algorithm>
#include <thread>

namespace {
constexpr QLatin1StringView kFilterScreenedPoisson("surface_reconstruction_screened_poisson");

int intParameter(const MeshFilterParameterValues &params, const QString &id, int fallback)
{
    const auto it = params.constFind(id);
    if (it == params.constEnd())
        return fallback;
    bool ok = false;
    const int value = it.value().toInt(&ok);
    return ok ? value : fallback;
}

double doubleParameter(const MeshFilterParameterValues &params, const QString &id, double fallback)
{
    const auto it = params.constFind(id);
    if (it == params.constEnd())
        return fallback;
    bool ok = false;
    const double value = it.value().toDouble(&ok);
    return ok ? value : fallback;
}

bool boolParameter(const MeshFilterParameterValues &params, const QString &id, bool fallback)
{
    const auto it = params.constFind(id);
    if (it == params.constEnd())
        return fallback;
    if (it.value().userType() == QMetaType::Bool)
        return it.value().toBool();
    const QString text = it.value().toString().trimmed().toLower();
    if (text == QStringLiteral("true") || text == QStringLiteral("1"))
        return true;
    if (text == QStringLiteral("false") || text == QStringLiteral("0"))
        return false;
    return fallback;
}

void addBoolParam(
    MeshFilterDescriptor &descriptor,
    const QString &id,
    const QString &label,
    const QString &help,
    bool defaultValue,
    const QString &group = QStringLiteral("main"))
{
    MeshFilterParameterDescriptor p;
    p.id = id;
    p.label = label;
    p.helpMarkdown = help;
    p.group = group;
    p.type = MeshFilterParameterType::Bool;
    p.defaultValue = defaultValue;
    descriptor.parameters.push_back(std::move(p));
}

void addIntParam(
    MeshFilterDescriptor &descriptor,
    const QString &id,
    const QString &label,
    const QString &help,
    int defaultValue,
    int minValue,
    int maxValue,
    const QString &group = QStringLiteral("main"))
{
    MeshFilterParameterDescriptor p;
    p.id = id;
    p.label = label;
    p.helpMarkdown = help;
    p.group = group;
    p.type = MeshFilterParameterType::Int;
    p.defaultValue = defaultValue;
    p.minValue = minValue;
    p.maxValue = maxValue;
    descriptor.parameters.push_back(std::move(p));
}

void addDoubleParam(
    MeshFilterDescriptor &descriptor,
    const QString &id,
    const QString &label,
    const QString &help,
    double defaultValue,
    double minValue,
    double maxValue,
    int decimals,
    const QString &group = QStringLiteral("main"))
{
    MeshFilterParameterDescriptor p;
    p.id = id;
    p.label = label;
    p.helpMarkdown = help;
    p.group = group;
    p.type = MeshFilterParameterType::Double;
    p.defaultValue = defaultValue;
    p.minValue = minValue;
    p.maxValue = maxValue;
    p.decimals = decimals;
    descriptor.parameters.push_back(std::move(p));
}

std::vector<int> selectedMeshIndices(const Document &doc, bool mergeVisible)
{
    std::vector<int> indices;
    if (!mergeVisible) {
        const int currentIndex = doc.currentMeshIndex();
        if (currentIndex >= 0 && currentIndex < doc.meshCount())
            indices.push_back(currentIndex);
        return indices;
    }

    indices.reserve(doc.meshCount());
    for (int i = 0; i < doc.meshCount(); ++i) {
        if (doc.mesh(i).visible)
            indices.push_back(i);
    }
    return indices;
}

bool allInputsHaveVertexColor(const Document &doc, const std::vector<int> &meshIndices)
{
    using Mask = vcg::tri::io::Mask;
    for (int index : meshIndices) {
        if (index < 0 || index >= doc.meshCount())
            return false;
        if ((doc.mesh(index).ioMask & Mask::IOM_VERTCOLOR) == 0)
            return false;
    }
    return !meshIndices.empty();
}

QString invalidNormalsMessage()
{
    return QObject::tr(
        "Filter requires correct per-vertex normals.\n"
        "All input vertices must have a proper, non-null normal.\n\n"
        "Try enabling the Pre-Clean option and retry.\n\n"
        "To permanently remove this problem:\n"
        "- on triangulated meshes, use Remove Unreferenced Vertices\n"
        "- on point clouds, use Conditional Vertex Selection with\n"
        "  (nx==0.0) && (ny==0.0) && (nz==0.0)\n"
        "  and then delete the selected vertices.");
}

std::vector<MeshFilterDescriptor> buildDescriptors(const Document &)
{
    std::vector<MeshFilterDescriptor> out;
    const auto upstreamStatus = ScreenedPoissonUpstream::inspectBackend();

    const unsigned int hwThreads = std::thread::hardware_concurrency();
    const int defaultThreads = hwThreads > 0 ? static_cast<int>(hwThreads) : 8;

    MeshFilterDescriptor d;
    d.id = QString::fromLatin1(kFilterScreenedPoisson);
    d.menuPath = QObject::tr("Remeshing/Surface Reconstruction");
    d.name = QObject::tr("Surface Reconstruction: Screened Poisson");
    d.shortDescription = QObject::tr("Creates a watertight surface from an oriented point set.");
    d.longDescriptionMarkdown = QObject::tr(
        "This surface reconstruction algorithm creates watertight surfaces from oriented point sets.\n\n"
        "This first QMeshLab port keeps the original MeshLab integrated Screened Poisson implementation, "
        "based on the code by Michael Kazhdan and Matthew Bolitho implementing the algorithm described in:\n\n"
        "*Michael Kazhdan, Hugues Hoppe*  \n"
        "**Screened Poisson Surface Reconstruction**");
    if (upstreamStatus.vendoredSourcesPresent) {
        d.longDescriptionMarkdown += QObject::tr(
            "\n\nAn upstream `PoissonRecon` source subtree is also vendored inside the plugin "
            "as scaffolding for the next migration phase, while the current runtime path stays on the stable legacy backend.");
    }
    d.tags = {
        QStringLiteral("reconstruction"),
        QStringLiteral("surface"),
        QStringLiteral("poisson"),
        QStringLiteral("point cloud")
    };
    d.inputDomain = MeshFilterInputDomain::SingleMesh;
    d.inputRequirements.requireVertices = true;
    d.outputDomain = MeshFilterOutputDomain::NewMeshes;

    addBoolParam(
        d,
        QStringLiteral("visibleLayer"),
        QObject::tr("Merge All Visible Layers"),
        QObject::tr("Enabling this flag means that all the visible layers will be used for providing the points."),
        false);
    addIntParam(
        d,
        QStringLiteral("depth"),
        QObject::tr("Reconstruction Depth"),
        QObject::tr("This integer is the maximum depth of the tree that will be used for surface reconstruction. Running at depth d corresponds to solving on a voxel grid whose resolution is no larger than 2^d x 2^d x 2^d. Since the reconstructor adapts the octree to the sampling density, the specified reconstruction depth is only an upper bound. The default value for this parameter is 8."),
        8,
        1,
        20);
    addIntParam(
        d,
        QStringLiteral("fullDepth"),
        QObject::tr("Adaptive Octree Depth"),
        QObject::tr("This integer specifies the depth beyond which the octree will be adapted. At coarser depths, the octree will be complete, containing all 2^d x 2^d x 2^d nodes. The default value for this parameter is 5."),
        5,
        1,
        20,
        QStringLiteral("advanced.tree"));
    addIntParam(
        d,
        QStringLiteral("cgDepth"),
        QObject::tr("Conjugate Gradients Depth"),
        QObject::tr("This integer is the depth up to which a conjugate-gradients solver will be used to solve the linear system. Beyond this depth, Gauss-Seidel relaxation will be used. The default value for this parameter is 0."),
        0,
        0,
        20,
        QStringLiteral("advanced.solver"));
    addDoubleParam(
        d,
        QStringLiteral("scale"),
        QObject::tr("Scale Factor"),
        QObject::tr("This floating point value specifies the ratio between the diameter of the cube used for reconstruction and the diameter of the samples' bounding cube. The default value is 1.1."),
        1.1,
        0.1,
        100.0,
        3,
        QStringLiteral("advanced.tree"));
    addDoubleParam(
        d,
        QStringLiteral("samplesPerNode"),
        QObject::tr("Minimum Number of Samples"),
        QObject::tr("This floating point value specifies the minimum number of sample points that should fall within an octree node as the octree construction is adapted to sampling density. For noise-free samples, small values in the range [1.0 - 5.0] can be used. For noisy samples, larger values in the range [15.0 - 20.0] may be needed to provide a smoother, noise-reduced reconstruction. The default value is 1.5."),
        1.5,
        0.01,
        1000.0,
        3);
    addDoubleParam(
        d,
        QStringLiteral("pointWeight"),
        QObject::tr("Interpolation Weight"),
        QObject::tr("This floating point value specifies the importance that interpolation of the point samples is given in the formulation of the screened Poisson equation. The results of the original unscreened Poisson reconstruction can be obtained by setting this value to 0. The default value for this parameter is 4."),
        4.0,
        0.0,
        1000.0,
        3);
    addIntParam(
        d,
        QStringLiteral("iters"),
        QObject::tr("Gauss-Seidel Relaxations"),
        QObject::tr("This integer value specifies the number of Gauss-Seidel relaxations to be performed at each level of the hierarchy. The default value for this parameter is 8."),
        8,
        1,
        100,
        QStringLiteral("advanced.solver"));
    addBoolParam(
        d,
        QStringLiteral("confidence"),
        QObject::tr("Confidence Flag"),
        QObject::tr("Enabling this flag tells the reconstructor to use the quality as confidence information. This is done by scaling the unit normals with the quality values. When the flag is not enabled, all normals are normalized to have unit length prior to reconstruction."),
        false);
    addBoolParam(
        d,
        QStringLiteral("preClean"),
        QObject::tr("Pre-Clean"),
        QObject::tr("Enabling this flag forces a cleaning pre-pass on the data, removing all unreferenced vertices or vertices with null normals."),
        false);
    addIntParam(
        d,
        QStringLiteral("threads"),
        QObject::tr("Number of Threads"),
        QObject::tr("Maximum number of threads that the reconstruction algorithm can use."),
        defaultThreads,
        1,
        256);

    out.push_back(std::move(d));
    return out;
}
}

QString ScreenedPoissonFilterPlugin::pluginId() const
{
    return QStringLiteral("qmeshlab.filter.screened_poisson");
}

QString ScreenedPoissonFilterPlugin::name() const
{
    return QObject::tr("QMeshLab Screened Poisson Filters");
}

std::vector<MeshFilterDescriptor> ScreenedPoissonFilterPlugin::filters(const Document &doc) const
{
    return buildDescriptors(doc);
}

MeshFilterRunResult ScreenedPoissonFilterPlugin::runFilter(
    const QString &filterId,
    const MeshFilterParameterValues &parameters,
    Document &doc) const
{
    if (filterId != QString::fromLatin1(kFilterScreenedPoisson))
        return { false, false, QObject::tr("Unknown filter id: %1").arg(filterId) };

    const bool mergeVisible = boolParameter(parameters, QStringLiteral("visibleLayer"), false);
    const std::vector<int> meshIndices = selectedMeshIndices(doc, mergeVisible);
    if (meshIndices.empty()) {
        return {
            false,
            false,
            mergeVisible
                ? QObject::tr("No visible meshes available for Screened Poisson reconstruction.")
                : QObject::tr("No current mesh selected.")
        };
    }

    PoissonParam<Scalarm> pp;
    pp.MaxDepthVal = std::max(1, intParameter(parameters, QStringLiteral("depth"), 8));
    pp.FullDepthVal = std::clamp(intParameter(parameters, QStringLiteral("fullDepth"), 5), 1, pp.MaxDepthVal);
    pp.CGDepthVal = std::max(0, intParameter(parameters, QStringLiteral("cgDepth"), 0));
    pp.ScaleVal = Scalarm(std::max(0.1, doubleParameter(parameters, QStringLiteral("scale"), 1.1)));
    pp.SamplesPerNodeVal = Scalarm(std::max(0.01, doubleParameter(parameters, QStringLiteral("samplesPerNode"), 1.5)));
    pp.PointWeightVal = Scalarm(std::max(0.0, doubleParameter(parameters, QStringLiteral("pointWeight"), 4.0)));
    pp.ItersVal = std::max(1, intParameter(parameters, QStringLiteral("iters"), 8));
    pp.ConfidenceFlag = boolParameter(parameters, QStringLiteral("confidence"), false);
    pp.DensityFlag = true;
    pp.CleanFlag = boolParameter(parameters, QStringLiteral("preClean"), false);
    pp.ThreadsVal = std::max(1, intParameter(parameters, QStringLiteral("threads"), pp.ThreadsVal));

    const bool preserveColor = allInputsHaveVertexColor(doc, meshIndices);

    for (int meshIndex : meshIndices) {
        if (meshIndex < 0 || meshIndex >= doc.meshCount())
            continue;
        Document::MeshEntry &entry = doc.mesh(meshIndex);
        PoissonClean(entry.mesh, pp.ConfidenceFlag, pp.CleanFlag);
        if (!HasGoodNormal(entry.mesh))
            return { false, false, invalidNormalsMessage() };

        if (pp.CleanFlag) {
            doc.markMeshGeometryChanged(meshIndex);
        } else {
            doc.markMeshMaterialChanged(meshIndex);
        }
    }

    if (ScreenedPoissonUpstream::isEnabledByEnvironment()) {
        return ScreenedPoissonUpstream::runSingleMeshFilter(doc, meshIndices, mergeVisible, parameters);
    }

    Box3m bbox = ComputePointStreamBounds<Scalarm>(doc, meshIndices);
    if (bbox.IsNull())
        return { false, false, QObject::tr("Screened Poisson reconstruction received an empty point set.") };

    DocumentMeshPointStream<Scalarm> stream(doc, meshIndices);
    VCGMesh outputMesh;
    const int execOk = _Execute<Scalarm, 2, BOUNDARY_NEUMANN, PlyColorAndValueVertex<Scalarm>>(
        &stream,
        bbox,
        outputMesh,
        pp,
        doc.progressCallback());

    if (doc.isOperationCancelRequested())
        return { false, false, QObject::tr("Filter interrupted by user.") };
    if (!execOk) {
        const QString detailedError = QString::fromStdString(LastPoissonErrorMessage()).trimmed();
        return {
            false,
            false,
            detailedError.isEmpty()
                ? QObject::tr("Screened Poisson reconstruction failed.")
                : QObject::tr("Screened Poisson reconstruction failed: %1").arg(detailedError)
        };
    }
    if (outputMesh.VN() <= 0 || outputMesh.FN() <= 0)
        return { false, false, QObject::tr("Screened Poisson reconstruction produced an empty mesh.") };

    vcg::tri::Allocator<VCGMesh>::CompactEveryVector(outputMesh);
    vcg::tri::UpdateBounding<VCGMesh>::Box(outputMesh);
    vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(outputMesh);

    int ioMask =
        vcg::tri::io::Mask::IOM_VERTQUALITY
        | vcg::tri::io::Mask::IOM_VERTNORMAL
        | vcg::tri::io::Mask::IOM_FACENORMAL;
    if (preserveColor)
        ioMask |= vcg::tri::io::Mask::IOM_VERTCOLOR;

    const QString meshName = QObject::tr("Poisson mesh");
    const int newIndex = doc.addMesh(outputMesh, meshName, ioMask);
    if (newIndex < 0)
        return { false, false, QObject::tr("Failed to add reconstructed mesh to the document.") };

    MeshFilterRunResult result;
    result.success = true;
    result.documentModified = true;
    result.newMeshIndices = { newIndex };
    result.infoMessages = {
        mergeVisible
            ? QObject::tr("Created '%1' from %2 visible layers (%3 vertices, %4 faces)")
                  .arg(doc.mesh(newIndex).name)
                  .arg(meshIndices.size())
                  .arg(doc.mesh(newIndex).mesh.VN())
                  .arg(doc.mesh(newIndex).mesh.FN())
            : QObject::tr("Created '%1' from current mesh (%2 vertices, %3 faces)")
                  .arg(doc.mesh(newIndex).name)
                  .arg(doc.mesh(newIndex).mesh.VN())
                  .arg(doc.mesh(newIndex).mesh.FN())
    };
    return result;
}

void registerScreenedPoissonFilterPlugin(MeshFilterPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<ScreenedPoissonFilterPlugin>());
}
