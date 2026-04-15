#include "basicfilterplugin.h"

#include "document.h"
#include "meshfilterpluginmanager.h"
#include <wrap/io_trimesh/io_mask.h>
#include <vcg/math/perlin_noise.h>
#include <vcg/complex/allocate.h>
#include <vcg/complex/algorithms/create/marching_cubes.h>
#include <vcg/complex/algorithms/create/mc_trivial_walker.h>
#include <vcg/complex/algorithms/update/bounding.h>
#include <vcg/complex/algorithms/update/normal.h>
#include <algorithm>
#include <cmath>
#include <memory>

namespace {
constexpr QLatin1StringView kFilterMeshInfo("mesh_info");
constexpr QLatin1StringView kFilterNormalizeUnit("normalize_unit_box");
constexpr QLatin1StringView kFilterDuplicateCurrent("duplicate_current_mesh");
constexpr QLatin1StringView kFilterCreateNoisyIso("create_noisy_isosurface");

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

QString stringParameter(const MeshFilterParameterValues &params, const QString &id, const QString &fallback)
{
    const auto it = params.constFind(id);
    if (it == params.constEnd())
        return fallback;
    const QString value = it.value().toString();
    return value.isEmpty() ? fallback : value;
}

std::vector<MeshFilterDescriptor> buildDescriptors(const Document &)
{
    std::vector<MeshFilterDescriptor> out;

    {
        MeshFilterDescriptor descriptor;
        descriptor.id = QString::fromLatin1(kFilterMeshInfo);
        descriptor.menuPath = QObject::tr("Inspection");
        descriptor.name = QObject::tr("Current Mesh Info");
        descriptor.shortDescription = QObject::tr("Prints a compact summary for the current mesh.");
        descriptor.longDescriptionMarkdown =
            QObject::tr("Outputs vertex/edge/face counts and bounding-box metrics for the current mesh.");
        descriptor.tags = { QStringLiteral("inspection"), QStringLiteral("document"), QStringLiteral("info") };
        descriptor.inputDomain = MeshFilterInputDomain::SingleMesh;
        descriptor.outputDomain = MeshFilterOutputDomain::Information;

        MeshFilterParameterDescriptor precision;
        precision.id = QStringLiteral("precision");
        precision.label = QObject::tr("Decimal Precision");
        precision.helpMarkdown = QObject::tr("Number of decimals used when formatting bounding-box metrics.");
        precision.group = QStringLiteral("advanced.output");
        precision.type = MeshFilterParameterType::Int;
        precision.defaultValue = 3;
        precision.minValue = 0;
        precision.maxValue = 8;
        descriptor.parameters.push_back(std::move(precision));

        out.push_back(std::move(descriptor));
    }

    {
        MeshFilterDescriptor descriptor;
        descriptor.id = QString::fromLatin1(kFilterNormalizeUnit);
        descriptor.menuPath = QObject::tr("Geometry/Transform");
        descriptor.name = QObject::tr("Normalize To Unit Box");
        descriptor.shortDescription = QObject::tr("Uniformly scales the current mesh to a target box size.");
        descriptor.longDescriptionMarkdown =
            QObject::tr("Computes current mesh bounding box and applies uniform scale, with optional recentering.");
        descriptor.tags = { QStringLiteral("geometry"), QStringLiteral("normalize"), QStringLiteral("transform") };
        descriptor.inputDomain = MeshFilterInputDomain::SingleMesh;
        descriptor.inputRequirements.requireVertices = true;
        descriptor.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;

        MeshFilterParameterDescriptor targetSize;
        targetSize.id = QStringLiteral("target_size");
        targetSize.label = QObject::tr("Target Size");
        targetSize.helpMarkdown = QObject::tr("Largest box dimension after normalization.");
        targetSize.group = QStringLiteral("main");
        targetSize.type = MeshFilterParameterType::Double;
        targetSize.defaultValue = 1.0;
        targetSize.minValue = 1e-8;
        targetSize.maxValue = 1e6;
        targetSize.decimals = 6;
        descriptor.parameters.push_back(std::move(targetSize));

        MeshFilterParameterDescriptor recenter;
        recenter.id = QStringLiteral("recenter");
        recenter.label = QObject::tr("Recenter");
        recenter.helpMarkdown = QObject::tr("Recenters mesh to origin before scaling.");
        recenter.group = QStringLiteral("main");
        recenter.type = MeshFilterParameterType::Bool;
        recenter.defaultValue = true;
        descriptor.parameters.push_back(std::move(recenter));

        out.push_back(std::move(descriptor));
    }

    {
        MeshFilterDescriptor descriptor;
        descriptor.id = QString::fromLatin1(kFilterDuplicateCurrent);
        descriptor.menuPath = QObject::tr("Layer");
        descriptor.name = QObject::tr("Duplicate Current Mesh");
        descriptor.shortDescription = QObject::tr("Creates a copy of the current mesh as a new layer.");
        descriptor.longDescriptionMarkdown =
            QObject::tr("Duplicates geometry and attributes of the current mesh and appends it to the document.");
        descriptor.tags = { QStringLiteral("layer"), QStringLiteral("duplicate"), QStringLiteral("new mesh") };
        descriptor.inputDomain = MeshFilterInputDomain::SingleMesh;
        descriptor.outputDomain = MeshFilterOutputDomain::NewMeshes;

        MeshFilterParameterDescriptor suffix;
        suffix.id = QStringLiteral("name_suffix");
        suffix.label = QObject::tr("Name Suffix");
        suffix.helpMarkdown = QObject::tr("Suffix appended to the duplicated mesh name.");
        suffix.group = QStringLiteral("main");
        suffix.type = MeshFilterParameterType::String;
        suffix.defaultValue = QObject::tr(" copy");
        descriptor.parameters.push_back(std::move(suffix));

        out.push_back(std::move(descriptor));
    }

    {
        MeshFilterDescriptor descriptor;
        descriptor.id = QString::fromLatin1(kFilterCreateNoisyIso);
        descriptor.menuPath = QObject::tr("Create");
        descriptor.name = QObject::tr("Noisy Isosurface");
        descriptor.shortDescription = QObject::tr("Creates an isosurface perturbed by 3D Perlin noise.");
        descriptor.longDescriptionMarkdown = QObject::tr(
            "Generates a scalar field over a cubic grid and extracts an isosurface using marching cubes.");
        descriptor.tags = {
            QStringLiteral("create"),
            QStringLiteral("isosurface"),
            QStringLiteral("marching cubes"),
            QStringLiteral("noise")
        };
        descriptor.inputDomain = MeshFilterInputDomain::None;
        descriptor.outputDomain = MeshFilterOutputDomain::NewMeshes;

        MeshFilterParameterDescriptor resolution;
        resolution.id = QStringLiteral("resolution");
        resolution.label = QObject::tr("Resolution");
        resolution.helpMarkdown =
            QObject::tr("Resolution of the side of the cubic grid used for volume creation.");
        resolution.group = QStringLiteral("main");
        resolution.type = MeshFilterParameterType::Int;
        resolution.defaultValue = 64;
        resolution.minValue = 8;
        resolution.maxValue = 256;
        descriptor.parameters.push_back(std::move(resolution));

        out.push_back(std::move(descriptor));
    }

    return out;
}
}

QString BasicFilterPlugin::pluginId() const
{
    return QStringLiteral("qmeshlab.filter.basic");
}

QString BasicFilterPlugin::name() const
{
    return QObject::tr("QMeshLab Basic Filters");
}

std::vector<MeshFilterDescriptor> BasicFilterPlugin::filters(const Document &doc) const
{
    return buildDescriptors(doc);
}

MeshFilterRunResult BasicFilterPlugin::runFilter(
    const QString &filterId,
    const MeshFilterParameterValues &parameters,
    Document &doc) const
{
    if (filterId == QString::fromLatin1(kFilterMeshInfo)) {
        const int meshIndex = doc.currentMeshIndex();
        if (meshIndex < 0 || meshIndex >= doc.meshCount()) {
            return { false, false, QObject::tr("No current mesh selected.") };
        }

        const int precision = std::clamp(intParameter(parameters, QStringLiteral("precision"), 3), 0, 8);
        const Document::MeshEntry &entry = doc.mesh(meshIndex);
        const vcg::Box3f &bbox = entry.mesh.bbox;
        const QString diag = QString::number(bbox.Diag(), 'f', precision);

        QStringList info;
        info << QObject::tr("Mesh: %1").arg(entry.name);
        info << QObject::tr("Vertices: %1").arg(entry.mesh.VN());
        info << QObject::tr("Edges: %1").arg(entry.mesh.EN());
        info << QObject::tr("Faces: %1").arg(entry.mesh.FN());
        info << QObject::tr("BBox diagonal: %1").arg(diag);
        info << QObject::tr("BBox min: (%1, %2, %3)")
                    .arg(QString::number(bbox.min.X(), 'f', precision))
                    .arg(QString::number(bbox.min.Y(), 'f', precision))
                    .arg(QString::number(bbox.min.Z(), 'f', precision));
        info << QObject::tr("BBox max: (%1, %2, %3)")
                    .arg(QString::number(bbox.max.X(), 'f', precision))
                    .arg(QString::number(bbox.max.Y(), 'f', precision))
                    .arg(QString::number(bbox.max.Z(), 'f', precision));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = false;
        result.infoMessages = info;
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterNormalizeUnit)) {
        const int meshIndex = doc.currentMeshIndex();
        if (meshIndex < 0 || meshIndex >= doc.meshCount()) {
            return { false, false, QObject::tr("No current mesh selected.") };
        }

        const double targetSize = doubleParameter(parameters, QStringLiteral("target_size"), 1.0);
        if (!std::isfinite(targetSize) || targetSize <= 0.0) {
            return { false, false, QObject::tr("Target size must be greater than zero.") };
        }
        const bool recenter = boolParameter(parameters, QStringLiteral("recenter"), true);

        Document::MeshEntry &entry = doc.mesh(meshIndex);
        if (entry.mesh.VN() <= 0) {
            return { false, false, QObject::tr("Current mesh has no vertices.") };
        }

        vcg::tri::UpdateBounding<VCGMesh>::Box(entry.mesh);
        const vcg::Box3f bbox = entry.mesh.bbox;
        const float maxDim = std::max({ bbox.DimX(), bbox.DimY(), bbox.DimZ() });
        if (maxDim <= 1e-12f) {
            return { false, false, QObject::tr("Cannot normalize a degenerate mesh bounding box.") };
        }

        const float scale = float(targetSize) / maxDim;
        const vcg::Point3f pivot = recenter ? bbox.Center() : bbox.min;
        for (VCGVertex &v : entry.mesh.vert) {
            if (v.IsD())
                continue;
            v.P() = (v.cP() - pivot) * scale;
        }
        vcg::tri::UpdateBounding<VCGMesh>::Box(entry.mesh);
        doc.markMeshGeometryChanged(meshIndex, QObject::tr("Normalized mesh '%1'").arg(entry.name));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = {
            QObject::tr("Applied scale factor %1").arg(QString::number(scale, 'f', 6))
        };
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterDuplicateCurrent)) {
        const int meshIndex = doc.currentMeshIndex();
        if (meshIndex < 0 || meshIndex >= doc.meshCount()) {
            return { false, false, QObject::tr("No current mesh selected.") };
        }

        const Document::MeshEntry &entry = doc.mesh(meshIndex);
        const QString suffix =
            stringParameter(parameters, QStringLiteral("name_suffix"), QObject::tr(" copy"));
        const QString newName = entry.name + suffix;
        const int newIndex = doc.duplicateMesh(meshIndex, newName);
        if (newIndex < 0) {
            return { false, false, QObject::tr("Failed to duplicate the current mesh.") };
        }

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.newMeshIndices = { newIndex };
        result.infoMessages = {
            QObject::tr("Created duplicated mesh '%1'").arg(doc.mesh(newIndex).name)
        };
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterCreateNoisyIso)) {
        const int gridSize = std::clamp(intParameter(parameters, QStringLiteral("resolution"), 64), 8, 256);

        using Scalar = float;
        using VolumeType = vcg::SimpleVolume<vcg::SimpleVoxel<Scalar>>;
        using WalkerType = vcg::tri::TrivialWalker<VCGMesh, VolumeType>;
        using MarchingCubesType = vcg::tri::MarchingCubes<VCGMesh, WalkerType>;

        VolumeType volume;
        volume.Init(
            vcg::Point3i(gridSize, gridSize, gridSize),
            vcg::Box3f(vcg::Point3f(0.0f, 0.0f, 0.0f), vcg::Point3f(1.0f, 1.0f, 1.0f)));

        const float halfGrid = float(gridSize) * 0.5f;
        const float perlinFreq = 0.2f;
        const float perlinAmp = float(gridSize) * 0.2f;
        for (int i = 0; i < gridSize; ++i) {
            for (int j = 0; j < gridSize; ++j) {
                for (int k = 0; k < gridSize; ++k) {
                    const float fy = float(j) - halfGrid;
                    const float fz = float(k) - halfGrid;
                    const float noise = float(vcg::math::Perlin::Noise(
                        double(i) * perlinFreq,
                        double(j) * perlinFreq,
                        double(k) * perlinFreq));
                    volume.Val(i, j, k) = fy * fy + fz * fz + float(i) * perlinAmp * noise;
                }
            }
        }

        VCGMesh generatedMesh;
        WalkerType walker;
        MarchingCubesType mc(generatedMesh, walker);
        // Keep historical noisy-isosurface appearance by extracting at the
        // same threshold that was previously used in this filter.
        const float isoThreshold = float(std::max(16, (gridSize * gridSize) / 10));
        walker.BuildMesh<MarchingCubesType>(
            generatedMesh,
            volume,
            mc,
            isoThreshold,
            nullptr);

        if (generatedMesh.VN() <= 0 || generatedMesh.FN() <= 0) {
            return { false, false, QObject::tr("Noisy isosurface generation produced an empty mesh.") };
        }

        vcg::tri::Allocator<VCGMesh>::CompactEveryVector(generatedMesh);
        vcg::tri::UpdateBounding<VCGMesh>::Box(generatedMesh);
        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(generatedMesh);

        const int ioMask =
            vcg::tri::io::Mask::IOM_VERTNORMAL
            | vcg::tri::io::Mask::IOM_FACENORMAL;
        const int newIndex = doc.addMesh(generatedMesh, QObject::tr("Noisy Isosurface"), ioMask);
        if (newIndex < 0)
            return { false, false, QObject::tr("Failed to add generated isosurface to document.") };

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.newMeshIndices = { newIndex };
        result.infoMessages = {
            QObject::tr("Generated '%1' at resolution %2 (%3 vertices, %4 faces)")
                .arg(doc.mesh(newIndex).name)
                .arg(gridSize)
                .arg(doc.mesh(newIndex).mesh.VN())
                .arg(doc.mesh(newIndex).mesh.FN())
        };
        return result;
    }

    return { false, false, QObject::tr("Unknown filter id: %1").arg(filterId) };
}

void registerBasicFilterPlugin(MeshFilterPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<BasicFilterPlugin>());
}
