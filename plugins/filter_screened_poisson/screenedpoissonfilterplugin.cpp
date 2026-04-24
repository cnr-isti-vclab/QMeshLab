#include "screenedpoissonfilterplugin.h"

#include "document.h"
#include "meshfilterpluginmanager.h"
#include "poissonrecon_backend.h"

#include <algorithm>
#include <limits>
#include <thread>

#include <vcg/complex/allocate.h>
#include <vcg/complex/algorithms/update/normal.h>

namespace {
constexpr QLatin1StringView kFilterScreenedPoisson("surface_reconstruction_screened_poisson");
constexpr QLatin1StringView kFilterSSDRecon("surface_reconstruction_ssd");
constexpr QLatin1StringView kFilterSurfaceTrimmer("surface_reconstruction_surface_trimmer");

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

template<class MeshType>
void cleanInputMesh(MeshType &mesh, bool scaleNormalByQuality, bool cleanFlag)
{
    vcg::tri::UpdateNormal<MeshType>::NormalizePerVertex(mesh);

    if (cleanFlag) {
        for (auto vi = mesh.vert.begin(); vi != mesh.vert.end(); ++vi) {
            if (vcg::SquaredNorm(vi->N()) < std::numeric_limits<float>::min() * 10.0f)
                vcg::tri::Allocator<MeshType>::DeleteVertex(mesh, *vi);
        }

        for (auto fi = mesh.face.begin(); fi != mesh.face.end(); ++fi) {
            if (fi->V(0)->IsD() || fi->V(1)->IsD() || fi->V(2)->IsD())
                vcg::tri::Allocator<MeshType>::DeleteFace(mesh, *fi);
        }
    }

    vcg::tri::Allocator<MeshType>::CompactEveryVector(mesh);
    if (scaleNormalByQuality) {
        for (auto vi = mesh.vert.begin(); vi != mesh.vert.end(); ++vi)
            vi->N() *= vi->Q();
    }
}

bool hasGoodNormals(VCGMesh &mesh)
{
    for (auto vi = mesh.vert.begin(); vi != mesh.vert.end(); ++vi) {
        if (vcg::SquaredNorm(vi->N()) < std::numeric_limits<float>::min() * 10.0f)
            return false;
    }
    return true;
}

std::vector<MeshFilterDescriptor> buildDescriptors(const Document &)
{
    std::vector<MeshFilterDescriptor> out;
    const unsigned int hwThreads = std::thread::hardware_concurrency();
    const int defaultThreads = hwThreads > 0 ? static_cast<int>(hwThreads) : 8;

    MeshFilterDescriptor d;
    d.id = QString::fromLatin1(kFilterScreenedPoisson);
    d.menuPath = QObject::tr("Remeshing/Surface Reconstruction");
    d.name = QObject::tr("Surface Reconstruction: Screened Poisson");
    d.shortDescription = QObject::tr("Creates a watertight surface from an oriented point set.");
    d.longDescriptionMarkdown = QObject::tr(
        "This surface reconstruction algorithm creates watertight surfaces from oriented point sets.\n\n"
        "This QMeshLab implementation is based on the `PoissonRecon` code by Michael Kazhdan and Matthew Bolitho, "
        "implementing the algorithm described in:\n\n"
        "*Michael Kazhdan, Hugues Hoppe*  \n"
        "**Screened Poisson Surface Reconstruction**");
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

    d = MeshFilterDescriptor();
    d.id = QString::fromLatin1(kFilterSSDRecon);
    d.menuPath = QObject::tr("Remeshing/Surface Reconstruction");
    d.name = QObject::tr("Surface Reconstruction: SSD");
    d.shortDescription = QObject::tr("Creates a watertight surface using smooth signed distance reconstruction.");
    d.longDescriptionMarkdown = QObject::tr(
        "This filter reconstructs a surface from an oriented point set using the SSD reconstruction formulation "
        "provided by the `PoissonRecon` code base.\n\n"
        "Compared to Screened Poisson, the SSD formulation exposes separate weights for zero-crossing, gradient, "
        "and bi-Laplacian terms, making it useful when you want more direct control over smoothness and interpolation.");
    d.tags = {
        QStringLiteral("reconstruction"),
        QStringLiteral("surface"),
        QStringLiteral("ssd"),
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
        QObject::tr("This integer is the maximum depth of the tree that will be used for reconstruction. Running at depth d corresponds to solving on a grid whose resolution is no larger than 2^d x 2^d x 2^d. The default value for this parameter is 8."),
        8,
        1,
        20);
    addIntParam(
        d,
        QStringLiteral("fullDepth"),
        QObject::tr("Adaptive Octree Depth"),
        QObject::tr("This integer specifies the depth beyond which the tree will be adapted to the sampling density. The default value for this parameter is 5."),
        5,
        1,
        20,
        QStringLiteral("advanced.tree"));
    addIntParam(
        d,
        QStringLiteral("baseDepth"),
        QObject::tr("Coarse Solver Depth"),
        QObject::tr("This integer specifies the depth of the coarsest multigrid solve level. Larger values make the coarse solve finer and more expensive."),
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
        QObject::tr("This floating point value specifies the minimum number of sample points that should fall within a node as the tree construction is adapted to sampling density. The default value is 1.5."),
        1.5,
        0.01,
        1000.0,
        3);
    addDoubleParam(
        d,
        QStringLiteral("valueWeight"),
        QObject::tr("Zero-Crossing Weight"),
        QObject::tr("This floating point value specifies the weight associated with the zero-crossing term of the SSD energy. Larger values make the reconstruction interpolate the input points more strongly."),
        1.0,
        0.0,
        100000.0,
        4);
    addDoubleParam(
        d,
        QStringLiteral("gradientWeight"),
        QObject::tr("Gradient Weight"),
        QObject::tr("This floating point value specifies the weight associated with the gradient fitting term of the SSD energy. The value must be strictly positive."),
        1.0,
        0.000001,
        100000.0,
        4);
    addDoubleParam(
        d,
        QStringLiteral("biLapWeight"),
        QObject::tr("Bi-Laplacian Weight"),
        QObject::tr("This floating point value specifies the weight associated with the bi-Laplacian smoothing term of the SSD energy. Larger values produce smoother surfaces. The value must be strictly positive."),
        1.0,
        0.000001,
        100000.0,
        4);
    addIntParam(
        d,
        QStringLiteral("iters"),
        QObject::tr("Gauss-Seidel Relaxations"),
        QObject::tr("This integer value specifies the number of Gauss-Seidel relaxations to be performed at each level of the hierarchy. The default value is 8."),
        8,
        1,
        100,
        QStringLiteral("advanced.solver"));
    addBoolParam(
        d,
        QStringLiteral("exactInterpolation"),
        QObject::tr("Exact Interpolation"),
        QObject::tr("If enabled, the exact interpolation formulation is used when building the SSD system."),
        false,
        QStringLiteral("advanced.solver"));
    addBoolParam(
        d,
        QStringLiteral("nonLinearFit"),
        QObject::tr("Non Linear Fit"),
        QObject::tr("If enabled, the extracted iso-surface uses the non-linear fit. If disabled, the linear fit is used."),
        false,
        QStringLiteral("advanced.output"));
    addBoolParam(
        d,
        QStringLiteral("nonManifold"),
        QObject::tr("Allow Non Manifold"),
        QObject::tr("If enabled, the extractor does not force the output mesh to be manifold."),
        false,
        QStringLiteral("advanced.output"));
    addDoubleParam(
        d,
        QStringLiteral("cgAccuracy"),
        QObject::tr("CG Solver Accuracy"),
        QObject::tr("This floating point value specifies the accuracy used by the conjugate gradients solver. Smaller values make the linear solve more accurate and more expensive."),
        1e-3,
        1e-8,
        1.0,
        6,
        QStringLiteral("advanced.solver"));
    addDoubleParam(
        d,
        QStringLiteral("dataScale"),
        QObject::tr("Per Level Data Scale"),
        QObject::tr("This floating point value specifies the pull factor used by the hierarchical SSD formulation. The default value is 32."),
        32.0,
        0.0,
        1000.0,
        3,
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

    d = MeshFilterDescriptor();
    d.id = QString::fromLatin1(kFilterSurfaceTrimmer);
    d.menuPath = QObject::tr("Remeshing/Surface Reconstruction");
    d.name = QObject::tr("Surface Reconstruction: Surface Trimmer");
    d.shortDescription = QObject::tr("Trims a reconstructed mesh using the scalar values stored on the vertices.");
    d.longDescriptionMarkdown = QObject::tr(
        "This filter trims a reconstructed surface by cutting the mesh along an isovalue defined over the vertices.\n\n"
        "It is especially useful after Poisson-based reconstruction when the vertex quality stores the reconstruction "
        "density, allowing low-confidence regions to be removed.");
    d.tags = {
        QStringLiteral("reconstruction"),
        QStringLiteral("trimming"),
        QStringLiteral("density"),
        QStringLiteral("surface")
    };
    d.inputDomain = MeshFilterInputDomain::SingleMesh;
    d.inputRequirements.requireVertices = true;
    d.inputRequirements.requireFaces = true;
    d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;

    addDoubleParam(
        d,
        QStringLiteral("trim"),
        QObject::tr("Trim Threshold"),
        QObject::tr("This floating point value specifies the trimming value. Faces whose scalar field is below this threshold are trimmed away; crossing faces are split along the threshold."),
        0.0,
        -1000000.0,
        1000000.0,
        6);
    addDoubleParam(
        d,
        QStringLiteral("islandAreaRatio"),
        QObject::tr("Island Area Ratio"),
        QObject::tr("This floating point value specifies the relative area threshold used to detect small disconnected islands. Smaller components may be merged or removed according to the selected options."),
        0.001,
        0.0,
        1.0,
        6,
        QStringLiteral("advanced.cleanup"));
    addBoolParam(
        d,
        QStringLiteral("removeIslands"),
        QObject::tr("Remove Islands"),
        QObject::tr("If enabled, disconnected components whose area is below the island area ratio are removed instead of being preserved."),
        false,
        QStringLiteral("advanced.cleanup"));
    addBoolParam(
        d,
        QStringLiteral("polygonMesh"),
        QObject::tr("Polygon Mesh"),
        QObject::tr("The original tool can preserve polygonal output. QMeshLab stores triangle meshes, so the result is always triangulated even when this option is enabled."),
        false,
        QStringLiteral("advanced.output"));

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
    return QObject::tr("QMeshLab PoissonRecon Filters");
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
    if (filterId == QString::fromLatin1(kFilterSurfaceTrimmer)) {
        const int meshIndex = doc.currentMeshIndex();
        if (meshIndex < 0 || meshIndex >= doc.meshCount())
            return { false, false, QObject::tr("No current mesh selected.") };
        return ScreenedPoisson::runSurfaceTrimmerFilter(doc, meshIndex, parameters);
    }

    if (filterId == QString::fromLatin1(kFilterScreenedPoisson) || filterId == QString::fromLatin1(kFilterSSDRecon)) {
        const bool mergeVisible = boolParameter(parameters, QStringLiteral("visibleLayer"), false);
        const std::vector<int> meshIndices = selectedMeshIndices(doc, mergeVisible);
        if (meshIndices.empty()) {
            return {
                false,
                false,
                mergeVisible
                    ? QObject::tr("No visible meshes available for reconstruction.")
                    : QObject::tr("No current mesh selected.")
            };
        }

        const bool confidence = boolParameter(parameters, QStringLiteral("confidence"), false);
        const bool preClean = boolParameter(parameters, QStringLiteral("preClean"), false);

        for (int meshIndex : meshIndices) {
            if (meshIndex < 0 || meshIndex >= doc.meshCount())
                continue;
            Document::MeshEntry &entry = doc.mesh(meshIndex);
            cleanInputMesh(entry.mesh, confidence, preClean);
            if (!hasGoodNormals(entry.mesh))
                return { false, false, invalidNormalsMessage() };

            if (preClean) {
                doc.markMeshGeometryChanged(meshIndex);
            } else {
                doc.markMeshMaterialChanged(meshIndex);
            }
        }

        if (filterId == QString::fromLatin1(kFilterSSDRecon))
            return ScreenedPoisson::runSSDReconFilter(doc, meshIndices, mergeVisible, parameters);
        return ScreenedPoisson::runScreenedPoissonFilter(doc, meshIndices, mergeVisible, parameters);
    }

    return { false, false, QObject::tr("Unknown filter id: %1").arg(filterId) };
}

void registerScreenedPoissonFilterPlugin(MeshFilterPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<ScreenedPoissonFilterPlugin>());
}
