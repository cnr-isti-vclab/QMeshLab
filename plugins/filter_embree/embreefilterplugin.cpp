#include "embreefilterplugin.h"

#include "document.h"
#include "meshfilterpluginmanager.h"
#include <wrap/embree/EmbreeAdaptor.h>
#include <wrap/io_trimesh/io_mask.h>
#include <vcg/complex/algorithms/update/bounding.h>
#include <vcg/complex/algorithms/update/color.h>
#include <vcg/complex/algorithms/update/normal.h>
#include <vcg/complex/algorithms/update/quality.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>

namespace {
constexpr QLatin1StringView kFilterObscurance("compute_obscurance");
constexpr QLatin1StringView kFilterAmbientOcclusion("compute_ambient_occlusion");
constexpr QLatin1StringView kFilterShapeDiameter("compute_shape_diameter_function");
constexpr QLatin1StringView kFilterSelectVisibleFaces("select_visible_faces");
constexpr QLatin1StringView kFilterAnalyzeNormals("analyze_normals");

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

struct CurrentMeshRef
{
    int index = -1;
    Document::MeshEntry *entry = nullptr;
};

std::optional<CurrentMeshRef> currentMesh(Document &doc, QString &errorMessage)
{
    const int meshIndex = doc.currentMeshIndex();
    if (meshIndex < 0 || meshIndex >= doc.meshCount()) {
        errorMessage = QObject::tr("No current mesh selected.");
        return std::nullopt;
    }
    return CurrentMeshRef { meshIndex, &doc.mesh(meshIndex) };
}

MeshFilterParameterDescriptor raysParameter()
{
    MeshFilterParameterDescriptor rays;
    rays.id = QStringLiteral("rays");
    rays.label = QObject::tr("Rays");
    rays.helpMarkdown =
        QObject::tr("Number of rays shot from each face barycenter.");
    rays.group = QStringLiteral("main");
    rays.type = MeshFilterParameterType::Int;
    rays.defaultValue = 64;
    rays.minValue = 1;
    rays.maxValue = 8192;
    return rays;
}

std::vector<MeshFilterDescriptor> buildDescriptors(const Document &)
{
    std::vector<MeshFilterDescriptor> out;

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterObscurance);
        d.menuPath = QObject::tr("Quality/Embree");
        d.name = QObject::tr("Compute Obscurance");
        d.shortDescription =
            QObject::tr("Computes volumetric obscurance and stores it in quality.");
        d.longDescriptionMarkdown = QObject::tr(
            "Computes **ambient obscurance** per face by ray casting from face barycenters.\n"
            "Face quality is updated, then propagated to vertex quality and mapped to gray colors.\n"
            "Uses Intel Embree through VCGLib's `EmbreeAdaptor`.");
        d.tags = { QStringLiteral("quality"), QStringLiteral("embree"), QStringLiteral("obscurance") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        d.parameters.push_back(raysParameter());

        MeshFilterParameterDescriptor tau;
        tau.id = QStringLiteral("tau");
        tau.label = QObject::tr("Tau");
        tau.helpMarkdown =
            QObject::tr("Spatial decay factor used in obscurance accumulation.");
        tau.group = QStringLiteral("main");
        tau.type = MeshFilterParameterType::Double;
        tau.defaultValue = 0.1;
        tau.minValue = 0.0;
        tau.maxValue = 10.0;
        tau.decimals = 4;
        d.parameters.push_back(std::move(tau));

        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterAmbientOcclusion);
        d.menuPath = QObject::tr("Quality/Embree");
        d.name = QObject::tr("Compute Ambient Occlusion");
        d.shortDescription =
            QObject::tr("Computes ambient occlusion and stores it in quality.");
        d.longDescriptionMarkdown = QObject::tr(
            "Computes **ambient occlusion** per face via Embree ray casting.\n"
            "Face quality is updated, then propagated to vertex quality and mapped to gray colors.");
        d.tags = { QStringLiteral("quality"), QStringLiteral("embree"), QStringLiteral("ao") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        d.parameters.push_back(raysParameter());
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterShapeDiameter);
        d.menuPath = QObject::tr("Quality/Embree");
        d.name = QObject::tr("Compute Shape Diameter Function");
        d.shortDescription =
            QObject::tr("Computes SDF and stores it in quality.");
        d.longDescriptionMarkdown = QObject::tr(
            "Computes **Shape Diameter Function (SDF)** by tracing rays inside a cone around inward directions.\n"
            "Face quality is updated, then propagated to vertex quality and mapped with a ramp colormap.");
        d.tags = { QStringLiteral("quality"), QStringLiteral("embree"), QStringLiteral("sdf") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        d.parameters.push_back(raysParameter());

        MeshFilterParameterDescriptor cone;
        cone.id = QStringLiteral("cone_amplitude");
        cone.label = QObject::tr("Cone Amplitude");
        cone.helpMarkdown =
            QObject::tr("Cone opening angle in degrees used for SDF ray directions.");
        cone.group = QStringLiteral("main");
        cone.type = MeshFilterParameterType::Double;
        cone.defaultValue = 90.0;
        cone.minValue = 1.0;
        cone.maxValue = 179.0;
        cone.decimals = 2;
        d.parameters.push_back(std::move(cone));

        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSelectVisibleFaces);
        d.menuPath = QObject::tr("Selection/Embree");
        d.name = QObject::tr("Select Visible Faces");
        d.shortDescription =
            QObject::tr("Selects faces visible from a user-defined direction.");
        d.longDescriptionMarkdown = QObject::tr(
            "Shoots one ray from each face barycenter along the chosen direction and selects faces with no hit.\n"
            "Selection can be incremental.");
        d.tags = { QStringLiteral("selection"), QStringLiteral("embree"), QStringLiteral("visibility") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;

        MeshFilterParameterDescriptor dirX;
        dirX.id = QStringLiteral("dir_x");
        dirX.label = QObject::tr("Dir X");
        dirX.helpMarkdown = QObject::tr("X component of visibility direction.");
        dirX.group = QStringLiteral("main");
        dirX.type = MeshFilterParameterType::Double;
        dirX.defaultValue = 1.0;
        dirX.minValue = -1e6;
        dirX.maxValue = 1e6;
        dirX.decimals = 4;
        d.parameters.push_back(std::move(dirX));

        MeshFilterParameterDescriptor dirY;
        dirY.id = QStringLiteral("dir_y");
        dirY.label = QObject::tr("Dir Y");
        dirY.helpMarkdown = QObject::tr("Y component of visibility direction.");
        dirY.group = QStringLiteral("main");
        dirY.type = MeshFilterParameterType::Double;
        dirY.defaultValue = 1.0;
        dirY.minValue = -1e6;
        dirY.maxValue = 1e6;
        dirY.decimals = 4;
        d.parameters.push_back(std::move(dirY));

        MeshFilterParameterDescriptor dirZ;
        dirZ.id = QStringLiteral("dir_z");
        dirZ.label = QObject::tr("Dir Z");
        dirZ.helpMarkdown = QObject::tr("Z component of visibility direction.");
        dirZ.group = QStringLiteral("main");
        dirZ.type = MeshFilterParameterType::Double;
        dirZ.defaultValue = 0.0;
        dirZ.minValue = -1e6;
        dirZ.maxValue = 1e6;
        dirZ.decimals = 4;
        d.parameters.push_back(std::move(dirZ));

        MeshFilterParameterDescriptor incremental;
        incremental.id = QStringLiteral("incremental_selection");
        incremental.label = QObject::tr("Incremental Selection");
        incremental.helpMarkdown =
            QObject::tr("If enabled, keep existing selection and add newly visible faces.");
        incremental.group = QStringLiteral("main");
        incremental.type = MeshFilterParameterType::Bool;
        incremental.defaultValue = false;
        d.parameters.push_back(std::move(incremental));

        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterAnalyzeNormals);
        d.menuPath = QObject::tr("Normals/Embree");
        d.name = QObject::tr("Reorient Face Normals by Geometry");
        d.shortDescription =
            QObject::tr("Reorients wrongly oriented faces using Embree ray casting.");
        d.longDescriptionMarkdown = QObject::tr(
            "Detects inward-facing facets by ray casting and flips them.\n"
            "Supports parity sampling for difficult configurations.");
        d.tags = { QStringLiteral("normals"), QStringLiteral("embree"), QStringLiteral("orientation") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        d.parameters.push_back(raysParameter());

        MeshFilterParameterDescriptor parity;
        parity.id = QStringLiteral("parity_sampling");
        parity.label = QObject::tr("Parity Sampling");
        parity.helpMarkdown =
            QObject::tr("Uses parity sampling instead of visibility sampling.");
        parity.group = QStringLiteral("main");
        parity.type = MeshFilterParameterType::Bool;
        parity.defaultValue = false;
        d.parameters.push_back(std::move(parity));

        out.push_back(std::move(d));
    }

    return out;
}
}

QString EmbreeFilterPlugin::pluginId() const
{
    return QStringLiteral("qmeshlab.filter.embree");
}

QString EmbreeFilterPlugin::name() const
{
    return QObject::tr("QMeshLab Embree Filters");
}

std::vector<MeshFilterDescriptor> EmbreeFilterPlugin::filters(const Document &doc) const
{
    return buildDescriptors(doc);
}

MeshFilterRunResult EmbreeFilterPlugin::runFilter(
    const QString &filterId,
    const MeshFilterParameterValues &parameters,
    Document &doc) const
{
    QString errorMessage;
    auto current = currentMesh(doc, errorMessage);
    if (!current)
        return { false, false, errorMessage };
    Document::MeshEntry &entry = *current->entry;
    if (entry.mesh.FN() <= 0) {
        return { false, false, QObject::tr("Current mesh has no faces.") };
    }

    using Mask = vcg::tri::io::Mask;
    const int rays = std::clamp(intParameter(parameters, QStringLiteral("rays"), 64), 1, 8192);
    vcg::EmbreeAdaptor<VCGMesh> adaptor(entry.mesh);

    if (filterId == QString::fromLatin1(kFilterObscurance)) {
        const float tau = float(doubleParameter(parameters, QStringLiteral("tau"), 0.1));
        adaptor.computeObscurance(entry.mesh, rays, tau);
        vcg::tri::UpdateQuality<VCGMesh>::VertexFromFace(entry.mesh);
        vcg::tri::UpdateColor<VCGMesh>::PerVertexQualityGray(entry.mesh);

        entry.ioMask |=
            Mask::IOM_FACEQUALITY | Mask::IOM_VERTQUALITY | Mask::IOM_FACECOLOR | Mask::IOM_VERTCOLOR;
        doc.markMeshMaterialChanged(
            current->index,
            QObject::tr("Computed obscurance for '%1'").arg(entry.name));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = {
            QObject::tr("Computed obscurance using %1 rays and tau=%2")
                .arg(rays)
                .arg(QString::number(tau, 'f', 4))
        };
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterAmbientOcclusion)) {
        adaptor.computeAmbientOcclusion(entry.mesh, rays);
        vcg::tri::UpdateQuality<VCGMesh>::VertexFromFace(entry.mesh);
        vcg::tri::UpdateColor<VCGMesh>::PerVertexQualityGray(entry.mesh);

        entry.ioMask |=
            Mask::IOM_FACEQUALITY | Mask::IOM_VERTQUALITY | Mask::IOM_FACECOLOR | Mask::IOM_VERTCOLOR;
        doc.markMeshMaterialChanged(
            current->index,
            QObject::tr("Computed ambient occlusion for '%1'").arg(entry.name));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = {
            QObject::tr("Computed ambient occlusion using %1 rays").arg(rays)
        };
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterShapeDiameter)) {
        const float coneAmplitude =
            float(doubleParameter(parameters, QStringLiteral("cone_amplitude"), 90.0));
        adaptor.computeSDF(entry.mesh, rays, coneAmplitude);
        vcg::tri::UpdateQuality<VCGMesh>::VertexFromFace(entry.mesh);
        vcg::tri::UpdateColor<VCGMesh>::PerVertexQualityRamp(entry.mesh);

        entry.ioMask |=
            Mask::IOM_FACEQUALITY | Mask::IOM_VERTQUALITY | Mask::IOM_FACECOLOR | Mask::IOM_VERTCOLOR;
        doc.markMeshMaterialChanged(
            current->index,
            QObject::tr("Computed shape diameter function for '%1'").arg(entry.name));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = {
            QObject::tr("Computed SDF using %1 rays and cone amplitude=%2°")
                .arg(rays)
                .arg(QString::number(coneAmplitude, 'f', 2))
        };
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterSelectVisibleFaces)) {
        const double dx = doubleParameter(parameters, QStringLiteral("dir_x"), 1.0);
        const double dy = doubleParameter(parameters, QStringLiteral("dir_y"), 1.0);
        const double dz = doubleParameter(parameters, QStringLiteral("dir_z"), 0.0);
        const bool incremental =
            boolParameter(parameters, QStringLiteral("incremental_selection"), false);
        const vcg::Point3f dir { float(dx), float(dy), float(dz) };
        if (dir.SquaredNorm() <= 1e-20f)
            return { false, false, QObject::tr("Direction vector must be non-zero.") };

        adaptor.selectVisibleFaces(entry.mesh, dir, incremental);

        int selectedFaces = 0;
        for (const VCGFace &f : entry.mesh.face) {
            if (!f.IsD() && f.IsS())
                ++selectedFaces;
        }

        entry.ioMask |= Mask::IOM_FACEFLAGS;
        doc.markMeshMaterialChanged(
            current->index,
            QObject::tr("Selected visible faces for '%1'").arg(entry.name));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = {
            QObject::tr("Selected %1 visible faces").arg(selectedFaces)
        };
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterAnalyzeNormals)) {
        const bool parity =
            boolParameter(parameters, QStringLiteral("parity_sampling"), false);
        adaptor.computeNormalAnalysis(entry.mesh, rays, parity);

        vcg::tri::UpdateNormal<VCGMesh>::PerFaceNormalized(entry.mesh);
        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(entry.mesh);
        vcg::tri::UpdateBounding<VCGMesh>::Box(entry.mesh);

        entry.ioMask |= Mask::IOM_FACENORMAL | Mask::IOM_VERTNORMAL | Mask::IOM_FACEFLAGS;
        doc.markMeshGeometryChanged(
            current->index,
            QObject::tr("Reoriented face normals for '%1'").arg(entry.name));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = {
            QObject::tr("Normal reorientation completed (%1 sampling)")
                .arg(parity ? QObject::tr("parity") : QObject::tr("visibility"))
        };
        return result;
    }

    return { false, false, QObject::tr("Unknown filter id: %1").arg(filterId) };
}

void registerEmbreeFilterPlugin(MeshFilterPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<EmbreeFilterPlugin>());
}
