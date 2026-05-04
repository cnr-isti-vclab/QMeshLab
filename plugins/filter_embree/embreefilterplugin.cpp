#include "embreefilterplugin.h"

#include "document.h"
#include "meshfilterpluginmanager.h"
#include <QVector3D>
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
}

QString EmbreeFilterPlugin::pluginId() const
{
    return QStringLiteral("qmeshlab.filter.embree");
}

QString EmbreeFilterPlugin::name() const
{
    return QObject::tr("QMeshLab Embree Filters");
}

MeshFilterRunResult EmbreeFilterPlugin::runFilter(
    const QString &filterId,
    const FilterParams &params,
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
    const int rays = std::clamp(params.getInt(QStringLiteral("rays")), 1, 8192);
    vcg::EmbreeAdaptor<VCGMesh> adaptor(entry.mesh);
    vcg::CallBackPos *cb = doc.progressCallback();
    auto interruptedResult = [&]() -> MeshFilterRunResult {
        return { false, false, QObject::tr("Filter interrupted by user.") };
    };

    if (filterId == QString::fromLatin1(kFilterObscurance)) {
        const float tau = float(params.getDouble(QStringLiteral("tau")));
        adaptor.computeObscurance(entry.mesh, rays, tau, cb);
        if (doc.isOperationCancelRequested())
            return interruptedResult();
        vcg::tri::UpdateQuality<VCGMesh>::VertexFromFace(entry.mesh);
        vcg::tri::UpdateColor<VCGMesh>::PerVertexQualityGray(entry.mesh);

        entry.ioMask |=
            Mask::IOM_FACEQUALITY | Mask::IOM_VERTQUALITY | Mask::IOM_FACECOLOR | Mask::IOM_VERTCOLOR;
        doc.markMeshGeometryChanged(
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
        adaptor.computeAmbientOcclusion(entry.mesh, rays, cb);
        adaptor.rayEpsilon = 1e-4f; // Set ray epsilon to a small value to improve AO accuracy on thin features
        if (doc.isOperationCancelRequested())
            return interruptedResult();
        vcg::tri::UpdateQuality<VCGMesh>::VertexFromFace(entry.mesh);
        vcg::tri::UpdateColor<VCGMesh>::PerVertexQualityGray(entry.mesh);

        entry.ioMask |=
            Mask::IOM_FACEQUALITY | Mask::IOM_VERTQUALITY | Mask::IOM_FACECOLOR | Mask::IOM_VERTCOLOR;
        doc.markMeshGeometryChanged(
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
        const float coneAmplitude = float(params.getDouble(QStringLiteral("cone_amplitude")));
        adaptor.computeSDF(entry.mesh, rays, coneAmplitude, cb);
        if (doc.isOperationCancelRequested())
            return interruptedResult();
        vcg::tri::UpdateQuality<VCGMesh>::VertexFromFace(entry.mesh);
        vcg::tri::UpdateColor<VCGMesh>::PerVertexQualityRamp(entry.mesh);

        entry.ioMask |=
            Mask::IOM_FACEQUALITY | Mask::IOM_VERTQUALITY | Mask::IOM_FACECOLOR | Mask::IOM_VERTCOLOR;
        doc.markMeshGeometryChanged(
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
        const QVector3D dv = params.getPoint3f(QStringLiteral("direction"));
        const vcg::Point3f dir { -float(dv.x()), -float(dv.y()), -float(dv.z()) };
        if (dir.SquaredNorm() <= 1e-20f)
            return { false, false, QObject::tr("Direction vector must be non-zero.") };

        // Always pass incremental=false: the framework manager handles incremental
        // selection by ORing back the pre-run selection when requested.
        adaptor.selectVisibleFaces(entry.mesh, dir, false, cb);
        if (doc.isOperationCancelRequested())
            return interruptedResult();

        int selectedFaces = 0;
        for (const VCGFace &f : entry.mesh.face) {
            if (f.IsS())
                ++selectedFaces;
        }

        entry.ioMask |= Mask::IOM_FACEFLAGS;
        doc.markMeshSelectionChanged(
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
        const bool parity = params.getBool(QStringLiteral("parity_sampling"));
        adaptor.computeNormalAnalysis(entry.mesh, rays, parity, cb);
        if (doc.isOperationCancelRequested())
            return interruptedResult();

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
