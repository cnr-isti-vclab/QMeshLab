#include <QtTest/QtTest>

#include "document.h"
#include "textureassociationutils.h"

#include <wrap/io_trimesh/io_mask.h>
#include <vcg/complex/algorithms/clean.h>
#include <vcg/complex/algorithms/update/bounding.h>
#include <vcg/complex/algorithms/update/normal.h>
#include <vcg/complex/algorithms/update/topology.h>
#include <vcg/complex/allocate.h>
#include <array>
#include <cmath>

namespace {

void makeCubeMesh(VCGMesh &mesh, float offsetX, float offsetY, float offsetZ)
{
    mesh.Clear();
    vcg::tri::Allocator<VCGMesh>::AddVertices(mesh, 8);
    const std::array<vcg::Point3f, 8> vertices = {
        vcg::Point3f(offsetX + 0.0f, offsetY + 0.0f, offsetZ + 0.0f),
        vcg::Point3f(offsetX + 1.0f, offsetY + 0.0f, offsetZ + 0.0f),
        vcg::Point3f(offsetX + 1.0f, offsetY + 1.0f, offsetZ + 0.0f),
        vcg::Point3f(offsetX + 0.0f, offsetY + 1.0f, offsetZ + 0.0f),
        vcg::Point3f(offsetX + 0.0f, offsetY + 0.0f, offsetZ + 1.0f),
        vcg::Point3f(offsetX + 1.0f, offsetY + 0.0f, offsetZ + 1.0f),
        vcg::Point3f(offsetX + 1.0f, offsetY + 1.0f, offsetZ + 1.0f),
        vcg::Point3f(offsetX + 0.0f, offsetY + 1.0f, offsetZ + 1.0f)
    };
    for (size_t i = 0; i < vertices.size(); ++i)
        mesh.vert[i].P() = vertices[i];

    const std::array<std::array<int, 3>, 12> faces = {
        std::array<int, 3>{0, 2, 1}, std::array<int, 3>{0, 3, 2},
        std::array<int, 3>{4, 5, 6}, std::array<int, 3>{4, 6, 7},
        std::array<int, 3>{0, 1, 5}, std::array<int, 3>{0, 5, 4},
        std::array<int, 3>{3, 7, 6}, std::array<int, 3>{3, 6, 2},
        std::array<int, 3>{0, 4, 7}, std::array<int, 3>{0, 7, 3},
        std::array<int, 3>{1, 2, 6}, std::array<int, 3>{1, 6, 5}
    };
    for (const auto &face : faces)
        vcg::tri::Allocator<VCGMesh>::AddFace(mesh, face[0], face[1], face[2]);

    vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
    vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(mesh);
}

void makeOpenDiskMesh(VCGMesh &mesh)
{
    mesh.Clear();
    vcg::tri::Allocator<VCGMesh>::AddVertices(mesh, 5);
    mesh.vert[0].P() = vcg::Point3f(0.0f, 0.0f, 0.0f);
    mesh.vert[1].P() = vcg::Point3f(1.0f, 0.0f, 0.0f);
    mesh.vert[2].P() = vcg::Point3f(1.0f, 1.0f, 0.0f);
    mesh.vert[3].P() = vcg::Point3f(0.0f, 1.0f, 0.0f);
    mesh.vert[4].P() = vcg::Point3f(0.5f, 0.5f, 0.0f);

    vcg::tri::Allocator<VCGMesh>::AddFace(mesh, 0, 1, 4);
    vcg::tri::Allocator<VCGMesh>::AddFace(mesh, 1, 2, 4);
    vcg::tri::Allocator<VCGMesh>::AddFace(mesh, 2, 3, 4);
    vcg::tri::Allocator<VCGMesh>::AddFace(mesh, 3, 0, 4);

    vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
    vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(mesh);
}

void makeOpenCubeMesh(VCGMesh &mesh)
{
    makeCubeMesh(mesh, 0.0f, 0.0f, 0.0f);
    vcg::tri::Allocator<VCGMesh>::DeleteFace(mesh, mesh.face[0]);
    vcg::tri::Allocator<VCGMesh>::DeleteFace(mesh, mesh.face[1]);
    vcg::tri::Allocator<VCGMesh>::CompactEveryVector(mesh);
}

void makeTwoTextureTriangles(VCGMesh &mesh)
{
    mesh.Clear();
    mesh.face.EnableWedgeTexCoord();
    vcg::tri::Allocator<VCGMesh>::AddVertices(mesh, 6);
    for (int i = 0; i < 6; ++i)
        mesh.vert[size_t(i)].P() = vcg::Point3f(float(i % 3), float(i / 3), 0.0f);
    vcg::tri::Allocator<VCGMesh>::AddFace(mesh, 0, 1, 2);
    vcg::tri::Allocator<VCGMesh>::AddFace(mesh, 3, 4, 5);
    for (int face = 0; face < 2; ++face) {
        mesh.face[size_t(face)].WT(0) = VCGFace::TexCoordType(0.0f, 0.0f);
        mesh.face[size_t(face)].WT(1) = VCGFace::TexCoordType(1.0f, 0.0f);
        mesh.face[size_t(face)].WT(2) = VCGFace::TexCoordType(0.0f, 1.0f);
        for (int corner = 0; corner < 3; ++corner)
            mesh.face[size_t(face)].WT(corner).N() = face;
    }
}

void makeIcpPointCloud(VCGMesh &mesh)
{
    mesh.Clear();
    vcg::tri::Allocator<VCGMesh>::AddVertices(mesh, 5);
    const std::array<vcg::Point3f, 5> vertices = {
        vcg::Point3f(0.00f, 0.00f, 0.00f),
        vcg::Point3f(1.00f, 0.07f, 0.03f),
        vcg::Point3f(0.12f, 0.96f, 0.23f),
        vcg::Point3f(0.18f, 0.15f, 1.05f),
        vcg::Point3f(0.83f, 0.72f, 0.64f)
    };
    for (size_t i = 0; i < vertices.size(); ++i) {
        mesh.vert[i].P() = vertices[i];
        mesh.vert[i].N() = vcg::Point3f(0.0f, 0.0f, 1.0f);
    }
    vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
}

bool matrixNear(const QMatrix4x4 &a, const QMatrix4x4 &b, float eps = 1e-3f)
{
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            if (std::abs(a(row, col) - b(row, col)) > eps)
                return false;
    return true;
}

} // namespace

class FilterTests : public QObject
{
    Q_OBJECT

private slots:
    void filterRegistryExposesBuiltins();
    void filterApplicabilityReflectsDocumentState();
    void basicFiltersRunOnLoadedMesh();
    void filterParameterValidation();
    void meshFixRepairsOpenCube();
    void qslimSimplifiesCube();
    void instantMeshesRemeshesCube();
    void vertexDisplacementFiltersRunOnCube();
    void splitConnectedComponentsAfterDuplicateVertexRemoval();
    void hausdorffRunsOnTransientMeshCopies();
    void cgalAlphaWrapRunsWhenAvailable();
    void geodesicQualityFilterDoesNotBakeVertexColors();
    void triOptimizeFiltersRunOnLoadedMesh();
    void voronoiSurfaceSamplingRunsOnCube();
    void voronoiSolidWireframeRunsOnLoadedMesh();
    void icpBetweenPointCloudsUpdatesSourceTransform();
    void translateFilterMovesOnlyCurrentMesh();
    void packTextureImagesCreatesGutteredAtlas();
    void faceQualityFiltersAreSplit();
    void libiglParametrizationFiltersRunWhenAvailable();
    void meshBooleanFiltersRunWhenAvailable();
};

void FilterTests::filterRegistryExposesBuiltins()
{
    Document doc;
    const std::vector<Document::FilterInfo> infos = doc.filterInfos();
    QVERIFY(!infos.empty());

    bool hasMeshInfo = false;
    bool hasNormalize = false;
    bool hasDuplicate = false;
    bool hasCreateIso = false;
    bool hasCleanUnref = false;
    bool hasMeshFixRepair = false;
    bool hasOriginalQSlim = false;
    bool hasScreenedPoissonReference = false;
    bool hasTextureDefragReference = false;
    bool hasSmallIslandsReference = false;
    bool hasQuadricReference = false;
    bool hasTexturedQuadricReference = false;
    bool hasSelectOutliers = false;
    bool hasSelectColor = false;
    bool hasTriOptimizePlanar = false;
    bool hasTriOptimizeCurvature = false;
    bool hasTriOptimizeSmooth = false;
    bool hasVoronoiSampling = false;
    bool hasVoronoiVolume = false;
    bool hasVoronoiScaffolding = false;
    bool hasSolidWireframe = false;
    bool hasTwoMeshIcp = false;
    bool hasGlobalIcp = false;
    bool hasOverlapGraph = false;
    for (const auto &info : infos) {
        hasMeshInfo = hasMeshInfo || (info.descriptor.id == QStringLiteral("mesh_info"));
        hasNormalize = hasNormalize || (info.descriptor.id == QStringLiteral("normalize_unit_box"));
        hasDuplicate = hasDuplicate || (info.descriptor.id == QStringLiteral("generate_copy_of_current_mesh"));
        hasCreateIso = hasCreateIso || (info.descriptor.id == QStringLiteral("create_noisy_isosurface"));
        hasCleanUnref =
            hasCleanUnref || (info.descriptor.id == QStringLiteral("remove_unreferenced_vertices"));
        if (info.descriptor.id == QStringLiteral("repair_watertight_mesh")) {
            hasMeshFixRepair = true;
            QCOMPARE(info.descriptor.provenance.project, QStringLiteral("MeshFix 2.1"));
            QCOMPARE(info.descriptor.provenance.integration, QStringLiteral("external/meshfix"));
            QCOMPARE(info.descriptor.references.size(), size_t(1));
            QCOMPARE(info.descriptor.references.front().doi,
                     QStringLiteral("10.1007/s00371-010-0416-3"));
            QVERIFY(info.descriptor.references.front().bibTeX().contains(
                QStringLiteral("@article{attene2010meshfix,")));
        }
        if (info.descriptor.id
            == QStringLiteral("simplification_quadric_edge_collapse_qslim")) {
            hasOriginalQSlim = true;
            QCOMPARE(info.descriptor.provenance.project,
                     QStringLiteral("QSlim 2.1 (original MixKit implementation)"));
            QCOMPARE(info.descriptor.provenance.integration,
                     QStringLiteral("external/qslim"));
            QCOMPARE(info.descriptor.references.size(), size_t(1));
            QCOMPARE(info.descriptor.references.front().doi,
                     QStringLiteral("10.1145/258734.258849"));
        }
        if (info.descriptor.id == QStringLiteral("surface_reconstruction_screened_poisson")) {
            hasScreenedPoissonReference = true;
            QCOMPARE(info.descriptor.references.size(), size_t(1));
            QCOMPARE(info.descriptor.references.front().doi,
                     QStringLiteral("10.1145/2487228.2487237"));
            QVERIFY(!info.descriptor.references.front().webUrl().isEmpty());
        }
        if (info.descriptor.id == QStringLiteral("apply_texmap_defragmentation")) {
            hasTextureDefragReference = true;
            QCOMPARE(info.descriptor.references.size(), size_t(1));
            QCOMPARE(info.descriptor.references.front().id,
                     QStringLiteral("maggiordomo2021texture"));
        }
        if (info.descriptor.id == QStringLiteral("apply_small_islands_remover")) {
            hasSmallIslandsReference = true;
            QCOMPARE(info.descriptor.references.size(), size_t(1));
            QCOMPARE(info.descriptor.references.front().id,
                     QStringLiteral("maggiordomo2021texture"));
        }
        if (info.descriptor.id == QStringLiteral("meshing_decimation_quadric_edge_collapse")) {
            hasQuadricReference = true;
            QCOMPARE(info.descriptor.references.size(), size_t(1));
            QCOMPARE(info.descriptor.references.front().doi,
                     QStringLiteral("10.1145/258734.258849"));
            QCOMPARE(info.descriptor.references.front().webUrl(),
                     QStringLiteral("https://github.com/alecjacobson/qslim"));
        }
        if (info.descriptor.id
            == QStringLiteral("meshing_decimation_quadric_edge_collapse_with_texture")) {
            hasTexturedQuadricReference = true;
            QCOMPARE(info.descriptor.references.size(), size_t(1));
            QCOMPARE(info.descriptor.references.front().doi,
                     QStringLiteral("10.1109/VISUAL.1998.745312"));
        }
        hasSelectOutliers =
            hasSelectOutliers || (info.descriptor.id == QStringLiteral("select_outliers"));
        hasSelectColor =
            hasSelectColor || (info.descriptor.id == QStringLiteral("select_by_color"));
        hasTriOptimizePlanar =
            hasTriOptimizePlanar || (info.descriptor.id == QStringLiteral("meshing_edge_flip_by_planar_optimization"));
        hasTriOptimizeCurvature =
            hasTriOptimizeCurvature || (info.descriptor.id == QStringLiteral("meshing_edge_flip_by_curvature_optimization"));
        hasTriOptimizeSmooth =
            hasTriOptimizeSmooth || (info.descriptor.id == QStringLiteral("apply_coord_laplacian_smoothing_surface_preserving"));
        hasVoronoiSampling =
            hasVoronoiSampling || (info.descriptor.id == QStringLiteral("generate_sampling_voronoi"));
        hasVoronoiVolume =
            hasVoronoiVolume || (info.descriptor.id == QStringLiteral("generate_sampling_volumetric"));
        hasVoronoiScaffolding =
            hasVoronoiScaffolding || (info.descriptor.id == QStringLiteral("generate_voronoi_scaffolding"));
        hasSolidWireframe =
            hasSolidWireframe || (info.descriptor.id == QStringLiteral("generate_solid_wireframe"));
        hasTwoMeshIcp =
            hasTwoMeshIcp || (info.descriptor.id == QStringLiteral("compute_matrix_by_icp_between_meshes"));
        hasGlobalIcp =
            hasGlobalIcp || (info.descriptor.id == QStringLiteral("compute_matrix_by_mesh_global_alignment"));
        hasOverlapGraph =
            hasOverlapGraph || (info.descriptor.id == QStringLiteral("get_overlapping_meshes_graph"));
    }

    QVERIFY(hasMeshInfo);
    QVERIFY(hasNormalize);
    QVERIFY(hasDuplicate);
    QVERIFY(hasCreateIso);
    QVERIFY(hasCleanUnref);
    QVERIFY(hasMeshFixRepair);
    QVERIFY(hasOriginalQSlim);
    QVERIFY(hasScreenedPoissonReference);
    QVERIFY(hasTextureDefragReference);
    QVERIFY(hasSmallIslandsReference);
    QVERIFY(hasQuadricReference);
    QVERIFY(hasTexturedQuadricReference);
    QVERIFY(hasSelectOutliers);
    QVERIFY(hasSelectColor);
    QVERIFY(hasTriOptimizePlanar);
    QVERIFY(hasTriOptimizeCurvature);
    QVERIFY(hasTriOptimizeSmooth);
    QVERIFY(hasVoronoiSampling);
    QVERIFY(hasVoronoiVolume);
    QVERIFY(hasVoronoiScaffolding);
    QVERIFY(hasSolidWireframe);
    QVERIFY(hasTwoMeshIcp);
    QVERIFY(hasGlobalIcp);
    QVERIFY(hasOverlapGraph);
}

void FilterTests::filterApplicabilityReflectsDocumentState()
{
    Document doc;
    QString createIsoKey;
    int noneDomainCount = 0;

    {
        const std::vector<Document::FilterInfo> infos = doc.filterInfos();
        QVERIFY(!infos.empty());
        for (const auto &info : infos) {
            if (info.descriptor.id == QStringLiteral("create_noisy_isosurface")) {
                createIsoKey = info.key;
            }
            if (info.descriptor.inputDomain == MeshFilterInputDomain::None) {
                ++noneDomainCount;
                QVERIFY(info.applicable);
                QVERIFY(info.applicabilityError.trimmed().isEmpty());
            } else {
                QVERIFY(!info.applicable);
                QVERIFY(!info.applicabilityError.trimmed().isEmpty());
            }
        }
    }
    QVERIFY(noneDomainCount > 0);
    if (!createIsoKey.isEmpty()) {
        MeshFilterParameterValues params;
        params.insert(QStringLiteral("resolution"), 32);
        const MeshFilterRunResult createResult = doc.runFilter(createIsoKey, params);
        QVERIFY(createResult.success);
        QVERIFY(createResult.documentModified);
        QCOMPARE(createResult.newMeshIndices.size(), 1);
    }

    const QString path = QStringLiteral(TEST_SOURCE_DIR "/tests/data/simple.off");
    const int meshCountBeforeLoad = doc.meshCount();
    QCOMPARE(doc.loadMesh(path), 0);
    QCOMPARE(doc.meshCount(), meshCountBeforeLoad + 1);

    bool hasMeshInfoApplicable = false;
    bool hasNormalizeApplicable = false;
    bool hasDuplicateApplicable = false;
    {
        const std::vector<Document::FilterInfo> infos = doc.filterInfos();
        QVERIFY(!infos.empty());
        for (const auto &info : infos) {
            if (info.descriptor.id == QStringLiteral("mesh_info"))
                hasMeshInfoApplicable = info.applicable;
            else if (info.descriptor.id == QStringLiteral("normalize_unit_box"))
                hasNormalizeApplicable = info.applicable;
            else if (info.descriptor.id == QStringLiteral("generate_copy_of_current_mesh"))
                hasDuplicateApplicable = info.applicable;
        }
    }
    QVERIFY(hasMeshInfoApplicable);
    QVERIFY(hasNormalizeApplicable);
    QVERIFY(hasDuplicateApplicable);
}

void FilterTests::basicFiltersRunOnLoadedMesh()
{
    Document doc;
    const QString path = QStringLiteral(TEST_SOURCE_DIR "/tests/data/simple.off");
    QCOMPARE(doc.loadMesh(path), 0);
    QCOMPARE(doc.meshCount(), 1);

    QString meshInfoKey;
    QString normalizeKey;
    QString duplicateKey;
    QString createIsoKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("mesh_info"))
            meshInfoKey = info.key;
        else if (info.descriptor.id == QStringLiteral("normalize_unit_box"))
            normalizeKey = info.key;
        else if (info.descriptor.id == QStringLiteral("generate_copy_of_current_mesh"))
            duplicateKey = info.key;
        else if (info.descriptor.id == QStringLiteral("create_noisy_isosurface"))
            createIsoKey = info.key;
    }

    QVERIFY(!meshInfoKey.isEmpty());
    QVERIFY(!normalizeKey.isEmpty());
    QVERIFY(!duplicateKey.isEmpty());
    QVERIFY(!createIsoKey.isEmpty());

    {
        MeshFilterParameterValues params;
        params.insert(QStringLiteral("precision"), 2);
        const MeshFilterRunResult result = doc.runFilter(meshInfoKey, params);
        QVERIFY(result.success);
        QVERIFY(!result.documentModified);
        QVERIFY(!result.infoMessages.isEmpty());
    }

    const int currentBeforeNormalize = doc.currentMeshIndex();
    const std::uint64_t geomRevBefore = doc.mesh(currentBeforeNormalize).geometryRevision;
    {
        MeshFilterParameterValues params;
        params.insert(QStringLiteral("target_size"), 1.0);
        params.insert(QStringLiteral("recenter"), true);
        const MeshFilterRunResult result = doc.runFilter(normalizeKey, params);
        QVERIFY(result.success);
        QVERIFY(result.documentModified);
    }
    QCOMPARE(doc.mesh(currentBeforeNormalize).geometryRevision, geomRevBefore + 1);

    const int meshCountBeforeDuplicate = doc.meshCount();
    {
        const MeshFilterRunResult result = doc.runFilter(duplicateKey, {});
        QVERIFY(result.success);
        QVERIFY(result.documentModified);
        QCOMPARE(result.newMeshIndices.size(), 1);
    }
    QCOMPARE(doc.meshCount(), meshCountBeforeDuplicate + 1);
    QVERIFY(doc.mesh(doc.currentMeshIndex()).name.endsWith(QStringLiteral(" copy")));

    const int meshCountBeforeCreate = doc.meshCount();
    {
        MeshFilterParameterValues params;
        params.insert(QStringLiteral("resolution"), 24);
        const MeshFilterRunResult result = doc.runFilter(createIsoKey, params);
        QVERIFY(result.success);
        QVERIFY(result.documentModified);
        QCOMPARE(result.newMeshIndices.size(), 1);
        const int generatedIndex = result.newMeshIndices.front();
        QVERIFY(generatedIndex >= 0 && generatedIndex < doc.meshCount());
        QVERIFY(doc.mesh(generatedIndex).mesh.VN() > 0);
        QVERIFY(doc.mesh(generatedIndex).mesh.FN() > 0);
    }
    QCOMPARE(doc.meshCount(), meshCountBeforeCreate + 1);
}

void FilterTests::filterParameterValidation()
{
    Document doc;
    const QString path = QStringLiteral(TEST_SOURCE_DIR "/tests/data/simple.off");
    QCOMPARE(doc.loadMesh(path), 0);

    QString meshInfoKey;
    QString normalizeKey;
    QString createIsoKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("mesh_info"))
            meshInfoKey = info.key;
        else if (info.descriptor.id == QStringLiteral("normalize_unit_box"))
            normalizeKey = info.key;
        else if (info.descriptor.id == QStringLiteral("create_noisy_isosurface"))
            createIsoKey = info.key;
    }

    QVERIFY(!meshInfoKey.isEmpty());
    QVERIFY(!normalizeKey.isEmpty());
    QVERIFY(!createIsoKey.isEmpty());

    {
        const MeshFilterRunResult result = doc.runFilter(meshInfoKey, {});
        QVERIFY(result.success);
    }
    {
        MeshFilterParameterValues params;
        params.insert(QStringLiteral("precision"), QStringLiteral("oops"));
        const MeshFilterRunResult result = doc.runFilter(meshInfoKey, params);
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("precision")));
    }
    {
        MeshFilterParameterValues params;
        params.insert(QStringLiteral("precision"), -1);
        const MeshFilterRunResult result = doc.runFilter(meshInfoKey, params);
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("minimum")));
    }
    {
        MeshFilterParameterValues params;
        params.insert(QStringLiteral("unknown_param"), 1);
        const MeshFilterRunResult result = doc.runFilter(meshInfoKey, params);
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("Unknown parameter")));
    }
    {
        MeshFilterParameterValues params;
        params.insert(QStringLiteral("target_size"), 0.0);
        const MeshFilterRunResult result = doc.runFilter(normalizeKey, params);
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("minimum")));
    }
    {
        MeshFilterParameterValues params;
        params.insert(QStringLiteral("resolution"), 4);
        const MeshFilterRunResult result = doc.runFilter(createIsoKey, params);
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("minimum")));
    }
}

void FilterTests::meshFixRepairsOpenCube()
{
    Document doc;
    VCGMesh input;
    makeOpenCubeMesh(input);
    const int inputIndex = doc.addMesh(input, QStringLiteral("Open cube"));
    QVERIFY(inputIndex >= 0);

    QMatrix4x4 transform;
    transform.translate(2.0f, 3.0f, 4.0f);
    doc.setMeshTransform(inputIndex, transform);

    QString filterKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("repair_watertight_mesh")) {
            filterKey = info.key;
            break;
        }
    }
    QVERIFY(!filterKey.isEmpty());

    const MeshFilterRunResult result = doc.runFilter(filterKey, {});
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QCOMPARE(result.newMeshIndices.size(), 1);
    QCOMPARE(doc.meshCount(), 2);

    const int outputIndex = result.newMeshIndices.front();
    QVERIFY(matrixNear(doc.meshTransform(outputIndex), transform));
    VCGMesh &output = doc.mesh(outputIndex).mesh;
    QVERIFY(output.VN() > 0);
    QVERIFY(output.FN() >= 12);
    output.face.EnableFFAdjacency();
    vcg::tri::UpdateTopology<VCGMesh>::FaceFace(output);
    QCOMPARE(vcg::tri::Clean<VCGMesh>::CountHoles(output), 0);
}

void FilterTests::qslimSimplifiesCube()
{
    Document doc;
    VCGMesh input;
    makeCubeMesh(input, 0.0f, 0.0f, 0.0f);
    const int inputFaces = input.FN();
    const int inputIndex = doc.addMesh(input, QStringLiteral("Cube"));
    QVERIFY(inputIndex >= 0);

    QMatrix4x4 transform;
    transform.translate(2.0f, 3.0f, 4.0f);
    doc.setMeshTransform(inputIndex, transform);

    QString filterKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id
            == QStringLiteral("simplification_quadric_edge_collapse_qslim")) {
            filterKey = info.key;
            break;
        }
    }
    QVERIFY(!filterKey.isEmpty());

    MeshFilterParameterValues params;
    params.insert(QStringLiteral("TargetFaceNum"), 6);
    const MeshFilterRunResult result = doc.runFilter(filterKey, params);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QCOMPARE(result.newMeshIndices.size(), 1);
    QCOMPARE(doc.meshCount(), 2);
    QCOMPARE(doc.mesh(inputIndex).mesh.FN(), inputFaces);

    const int outputIndex = result.newMeshIndices.front();
    QVERIFY(matrixNear(doc.meshTransform(outputIndex), transform));
    const VCGMesh &output = doc.mesh(outputIndex).mesh;
    QVERIFY(output.VN() > 0);
    QVERIFY(output.FN() > 0);
    QVERIFY(output.FN() <= 6);
}

void FilterTests::instantMeshesRemeshesCube()
{
    Document doc;
    VCGMesh input;
    makeCubeMesh(input, 0.0f, 0.0f, 0.0f);
    const int inputIndex = doc.addMesh(input, QStringLiteral("Cube"));
    QVERIFY(inputIndex >= 0);

    QString filterKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("remesh_to_quads_instant_meshes")) {
            filterKey = info.key;
            QCOMPARE(info.descriptor.provenance.project, QStringLiteral("Instant Meshes"));
            QCOMPARE(info.descriptor.references.size(), size_t(1));
            QCOMPARE(info.descriptor.references.front().doi,
                     QStringLiteral("10.1145/2816795.2818078"));
            break;
        }
    }
    QVERIFY(!filterKey.isEmpty());

    MeshFilterParameterValues params;
    params.insert(QStringLiteral("targetEdgeLength"), 0.5);
    params.insert(QStringLiteral("smoothingIterations"), 0);
    params.insert(QStringLiteral("deterministic"), true);
    params.insert(QStringLiteral("threads"), 2);
    const MeshFilterRunResult result = doc.runFilter(filterKey, params);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QCOMPARE(result.newMeshIndices.size(), 1);
    QCOMPARE(doc.meshCount(), 2);
    const VCGMesh &output = doc.mesh(result.newMeshIndices.front()).mesh;
    QVERIFY(output.VN() > 0);
    QVERIFY(output.FN() > 0);

    QString subdivisionKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id
            == QStringLiteral("meshing_tri_to_quad_by_4_8_subdivision")) {
            subdivisionKey = info.key;
            break;
        }
    }
    QVERIFY(!subdivisionKey.isEmpty());
    const MeshFilterRunResult subdivision = doc.runFilter(subdivisionKey, {});
    QVERIFY2(subdivision.success, qPrintable(subdivision.errorMessage));
}

void FilterTests::vertexDisplacementFiltersRunOnCube()
{
    Document doc;
    VCGMesh input;
    makeCubeMesh(input, 0.0f, 0.0f, 0.0f);
    QVERIFY(doc.addMesh(input, QStringLiteral("Cube")) >= 0);

    const QStringList fractalIds {
        QStringLiteral("displace_by_fractal_brownian_motion"),
        QStringLiteral("displace_by_standard_multifractal_noise"),
        QStringLiteral("displace_by_heterogeneous_multifractal_noise"),
        QStringLiteral("displace_by_hybrid_multifractal_noise"),
        QStringLiteral("displace_by_ridged_multifractal_noise")
    };
    QHash<QString, QString> fractalKeys;
    QStringList found;
    bool foundRandom = false;
    bool foundLegacyRandom = false;
    for (const auto &info : doc.filterInfos()) {
        if (info.pluginId == QStringLiteral("qmeshlab.filter.vertex_displacement")) {
            foundRandom = foundRandom
                || info.descriptor.id == QStringLiteral("displace_vertices_randomly");
            if (fractalIds.contains(info.descriptor.id)) {
                found << info.descriptor.id;
                fractalKeys.insert(info.descriptor.id, info.key);
            }
        }
        foundLegacyRandom = foundLegacyRandom
            || info.descriptor.id == QStringLiteral("apply_coord_random_displacement");
    }
    QCOMPARE(found.size(), fractalIds.size());
    QVERIFY(foundRandom);
    QVERIFY(!foundLegacyRandom);
    QCOMPARE(fractalKeys.size(), fractalIds.size());

    for (const QString &id : fractalIds) {
        Document runDoc;
        VCGMesh cube;
        makeCubeMesh(cube, 0.0f, 0.0f, 0.0f);
        QVERIFY(runDoc.addMesh(cube, QStringLiteral("Cube")) >= 0);

        std::vector<vcg::Point3f> before;
        for (const VCGVertex &vertex : runDoc.mesh(0).mesh.vert)
            before.push_back(vertex.cP());

        MeshFilterParameterValues params;
        params.insert(QStringLiteral("maxHeight"), 0.1);
        params.insert(QStringLiteral("scale"), 1.0);
        params.insert(QStringLiteral("octaves"), 3);
        params.insert(QStringLiteral("lacunarity"), 2.0);
        params.insert(QStringLiteral("fractalIncrement"), 1.2);
        if (id != QStringLiteral("displace_by_fractal_brownian_motion"))
            params.insert(QStringLiteral("offset"), 0.9);
        if (id == QStringLiteral("displace_by_ridged_multifractal_noise"))
            params.insert(QStringLiteral("gain"), 2.0);
        params.insert(QStringLiteral("seed"), 1.0);
        params.insert(QStringLiteral("normalSmoothingSteps"), 2);
        const MeshFilterRunResult result = runDoc.runFilter(fractalKeys.value(id), params);
        QVERIFY2(result.success, qPrintable(id + QStringLiteral(": ") + result.errorMessage));

        bool changed = false;
        for (size_t i = 0; i < before.size(); ++i) {
            const vcg::Point3f &position = runDoc.mesh(0).mesh.vert[i].cP();
            changed = changed || (position - before[i]).Norm() > 1e-6f;
            QVERIFY(std::isfinite(position.X()));
            QVERIFY(std::isfinite(position.Y()));
            QVERIFY(std::isfinite(position.Z()));
        }
        QVERIFY2(changed, qPrintable(id));
    }
}

void FilterTests::splitConnectedComponentsAfterDuplicateVertexRemoval()
{
    VCGMesh input;
    input.face.EnableWedgeTexCoord();
    vcg::tri::Allocator<VCGMesh>::AddVertices(input, 7);
    const std::array<vcg::Point3f, 7> vertices = {
        vcg::Point3f(0, 0, 0), vcg::Point3f(1, 0, 0), vcg::Point3f(0, 1, 0),
        vcg::Point3f(0, 0, 0),
        vcg::Point3f(3, 0, 0), vcg::Point3f(4, 0, 0), vcg::Point3f(3, 1, 0)
    };
    for (size_t i = 0; i < vertices.size(); ++i)
        input.vert[i].P() = vertices[i];
    vcg::tri::Allocator<VCGMesh>::AddFace(input, 3, 1, 2);
    vcg::tri::Allocator<VCGMesh>::AddFace(input, 4, 5, 6);
    for (VCGFace &face : input.face)
        for (int corner = 0; corner < 3; ++corner)
            face.WT(corner) = VCGFace::TexCoordType(float(corner == 1), float(corner == 2));

    Document doc;
    QVERIFY(doc.addMesh(
        input,
        QStringLiteral("Disconnected"),
        vcg::tri::io::Mask::IOM_WEDGTEXCOORD) >= 0);

    auto filterKey = [&](const QString &id) {
        for (const auto &info : doc.filterInfos())
            if (info.descriptor.id == id)
                return info.key;
        return QString();
    };

    const MeshFilterRunResult clean =
        doc.runFilter(filterKey(QStringLiteral("remove_duplicated_vertices")), {});
    QVERIFY2(clean.success, qPrintable(clean.errorMessage));

    const MeshFilterRunResult split = doc.runFilter(
        filterKey(QStringLiteral("generate_splitting_by_connected_components")), {});
    QVERIFY2(split.success, qPrintable(split.errorMessage));
    QCOMPARE(split.newMeshIndices.size(), 2);
    for (int meshIndex : split.newMeshIndices) {
        const VCGMesh &mesh = doc.mesh(meshIndex).mesh;
        QCOMPARE(mesh.FN(), 1);
        QVERIFY(vcg::tri::HasPerWedgeTexCoord(mesh));
        for (const VCGFace &face : mesh.face)
            for (int corner = 0; corner < 3; ++corner)
                QVERIFY(face.cV(corner) != nullptr);
    }
}

void FilterTests::hausdorffRunsOnTransientMeshCopies()
{
    Document doc;
    VCGMesh sampled;
    VCGMesh target;
    makeCubeMesh(sampled, 0.0f, 0.0f, 0.0f);
    makeCubeMesh(target, 0.05f, 0.0f, 0.0f);
    const int sampledIndex = doc.addMesh(sampled, QStringLiteral("Sampled"));
    const int targetIndex = doc.addMesh(target, QStringLiteral("Target"));
    QVERIFY(sampledIndex >= 0);
    QVERIFY(targetIndex >= 0);

    QString filterKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("get_hausdorff_distance")) {
            filterKey = info.key;
            break;
        }
    }
    QVERIFY(!filterKey.isEmpty());

    MeshFilterParameterValues params;
    params.insert(QStringLiteral("SampledMesh"), sampledIndex);
    params.insert(QStringLiteral("TargetMesh"), targetIndex);
    params.insert(QStringLiteral("SampleNum"), 32);
    const MeshFilterRunResult result = doc.runFilter(filterKey, params);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QVERIFY(!result.documentModified);
    QVERIFY(result.infoMessages.join(QLatin1Char('\n')).contains(QStringLiteral("Samples:")));
}

void FilterTests::cgalAlphaWrapRunsWhenAvailable()
{
    Document doc;
    const QString path = QStringLiteral(TEST_SOURCE_DIR "/tests/data/simple.off");
    QCOMPARE(doc.loadMesh(path), 0);

    QString alphaWrapKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("generate_alpha_wrap")) {
            alphaWrapKey = info.key;
            break;
        }
    }

    if (alphaWrapKey.isEmpty())
        QSKIP("CGAL Alpha Wrap plugin is not available in this build.");

    MeshFilterParameterValues params;
    params.insert(QStringLiteral("Alpha"), 0.5);
    params.insert(QStringLiteral("Offset"), 0.05);
    const int meshCountBefore = doc.meshCount();
    const MeshFilterRunResult result = doc.runFilter(alphaWrapKey, params);

    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QVERIFY(result.documentModified);
    QCOMPARE(result.newMeshIndices.size(), 1);
    QCOMPARE(doc.meshCount(), meshCountBefore + 1);

    const int generatedIndex = result.newMeshIndices.front();
    QVERIFY(generatedIndex >= 0 && generatedIndex < doc.meshCount());
    QVERIFY(doc.mesh(generatedIndex).mesh.VN() > 0);
    QVERIFY(doc.mesh(generatedIndex).mesh.FN() > 0);
}

void FilterTests::geodesicQualityFilterDoesNotBakeVertexColors()
{
    Document doc;
    const QString path = QStringLiteral(TEST_SOURCE_DIR "/tests/data/simple.off");
    QCOMPARE(doc.loadMesh(path), 0);

    QString borderGeodesicKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("compute_scalar_by_border_distance_per_vertex")) {
            borderGeodesicKey = info.key;
            break;
        }
    }

    QVERIFY(!borderGeodesicKey.isEmpty());

    const MeshFilterRunResult result = doc.runFilter(borderGeodesicKey, {});
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QVERIFY(result.documentModified);
    QCOMPARE(result.visualizationHints.size(), 1);
    QCOMPARE(result.visualizationHints.front().meshIndex, 0);
    QVERIFY(result.visualizationHints.front().attribute ==
            MeshFilterVisualizationAttribute::VertexQuality);

    const int mask = doc.mesh(0).ioMask;
    QVERIFY((mask & vcg::tri::io::Mask::IOM_VERTQUALITY) != 0);
    QVERIFY((mask & vcg::tri::io::Mask::IOM_VERTCOLOR) == 0);
}

void FilterTests::triOptimizeFiltersRunOnLoadedMesh()
{
    Document doc;
    const QString path = QStringLiteral(TEST_SOURCE_DIR "/tests/data/simple.off");
    QCOMPARE(doc.loadMesh(path), 0);
    QCOMPARE(doc.meshCount(), 1);

    QString planarKey;
    QString curvatureKey;
    QString smoothKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("meshing_edge_flip_by_planar_optimization"))
            planarKey = info.key;
        else if (info.descriptor.id == QStringLiteral("meshing_edge_flip_by_curvature_optimization"))
            curvatureKey = info.key;
        else if (info.descriptor.id == QStringLiteral("apply_coord_laplacian_smoothing_surface_preserving"))
            smoothKey = info.key;
    }

    QVERIFY(!planarKey.isEmpty());
    QVERIFY(!curvatureKey.isEmpty());
    QVERIFY(!smoothKey.isEmpty());

    {
        MeshFilterParameterValues params;
        params.insert(QStringLiteral("iterations"), 0);
        const MeshFilterRunResult result = doc.runFilter(planarKey, params);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QVERIFY(result.documentModified);
    }

    {
        const MeshFilterRunResult result = doc.runFilter(curvatureKey, {});
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QVERIFY(result.documentModified);
        QVERIFY((doc.mesh(0).ioMask & vcg::tri::io::Mask::IOM_VERTQUALITY) != 0);
    }

    {
        const MeshFilterRunResult result = doc.runFilter(smoothKey, {});
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QVERIFY(result.documentModified);
        QVERIFY((doc.mesh(0).ioMask & vcg::tri::io::Mask::IOM_VERTNORMAL) != 0);
    }
}

void FilterTests::voronoiSurfaceSamplingRunsOnCube()
{
    Document doc;
    VCGMesh cube;
    makeCubeMesh(cube, 0.0f, 0.0f, 0.0f);
    const int mask =
        vcg::tri::io::Mask::IOM_VERTCOORD
        | vcg::tri::io::Mask::IOM_VERTNORMAL
        | vcg::tri::io::Mask::IOM_FACENORMAL;
    QCOMPARE(doc.addMesh(cube, QStringLiteral("Cube"), mask), 0);

    QString voronoiSamplingKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("generate_sampling_voronoi")) {
            voronoiSamplingKey = info.key;
            break;
        }
    }

    QVERIFY(!voronoiSamplingKey.isEmpty());

    MeshFilterParameterValues params;
    params.insert(QStringLiteral("sampleNum"), 4);
    params.insert(QStringLiteral("iterNum"), 0);
    params.insert(QStringLiteral("randomSeed"), 1);
    params.insert(QStringLiteral("preprocessFlag"), false);

    const int meshCountBefore = doc.meshCount();
    const MeshFilterRunResult result = doc.runFilter(voronoiSamplingKey, params);

    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QVERIFY(result.documentModified);
    QCOMPARE(result.newMeshIndices.size(), 2);
    QCOMPARE(doc.meshCount(), meshCountBefore + 2);
    QVERIFY((doc.mesh(0).ioMask & vcg::tri::io::Mask::IOM_VERTCOLOR) != 0);
    QVERIFY((doc.mesh(0).ioMask & vcg::tri::io::Mask::IOM_VERTQUALITY) != 0);
    for (int generatedIndex : result.newMeshIndices) {
        QVERIFY(generatedIndex >= 0 && generatedIndex < doc.meshCount());
        QVERIFY(doc.mesh(generatedIndex).mesh.VN() > 0);
    }
}

void FilterTests::voronoiSolidWireframeRunsOnLoadedMesh()
{
    Document doc;
    VCGMesh disk;
    makeOpenDiskMesh(disk);
    const int mask =
        vcg::tri::io::Mask::IOM_VERTCOORD
        | vcg::tri::io::Mask::IOM_VERTNORMAL
        | vcg::tri::io::Mask::IOM_FACENORMAL;
    QCOMPARE(doc.addMesh(disk, QStringLiteral("Open Disk"), mask), 0);

    QString solidWireframeKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("generate_solid_wireframe")) {
            solidWireframeKey = info.key;
            break;
        }
    }

    QVERIFY(!solidWireframeKey.isEmpty());

    MeshFilterParameterValues params;
    params.insert(QStringLiteral("edgeCylFlag"), true);
    params.insert(QStringLiteral("vertSphFlag"), true);
    params.insert(QStringLiteral("faceExtFlag"), false);
    params.insert(QStringLiteral("edgeCylRadius"), 0.02);
    params.insert(QStringLiteral("vertSphRadius"), 0.03);
    params.insert(QStringLiteral("cylinderSideNum"), 8);

    const int meshCountBefore = doc.meshCount();
    const MeshFilterRunResult result = doc.runFilter(solidWireframeKey, params);

    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QVERIFY(result.documentModified);
    QCOMPARE(result.newMeshIndices.size(), 1);
    QCOMPARE(doc.meshCount(), meshCountBefore + 1);

    const int generatedIndex = result.newMeshIndices.front();
    QVERIFY(generatedIndex >= 0 && generatedIndex < doc.meshCount());
    QVERIFY(doc.mesh(generatedIndex).mesh.VN() > 0);
    QVERIFY(doc.mesh(generatedIndex).mesh.FN() > 0);
}

void FilterTests::icpBetweenPointCloudsUpdatesSourceTransform()
{
    Document doc;
    VCGMesh referenceCloud;
    VCGMesh sourceCloud;
    makeIcpPointCloud(referenceCloud);
    makeIcpPointCloud(sourceCloud);
    const int mask =
        vcg::tri::io::Mask::IOM_VERTCOORD
        | vcg::tri::io::Mask::IOM_VERTNORMAL;
    const int referenceIndex = doc.addMesh(referenceCloud, QStringLiteral("ICP Reference"), mask);
    const int sourceIndex = doc.addMesh(sourceCloud, QStringLiteral("ICP Source"), mask);
    QVERIFY(referenceIndex >= 0);
    QVERIFY(sourceIndex >= 0);

    QMatrix4x4 shifted;
    shifted.setToIdentity();
    shifted.translate(0.12f, -0.06f, 0.04f);
    doc.setMeshTransform(sourceIndex, shifted);

    QString icpKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("compute_matrix_by_icp_between_meshes")) {
            icpKey = info.key;
            break;
        }
    }

    QVERIFY(!icpKey.isEmpty());

    MeshFilterParameterValues params;
    params.insert(QStringLiteral("ReferenceMesh"), referenceIndex);
    params.insert(QStringLiteral("SourceMesh"), sourceIndex);
    params.insert(QStringLiteral("SampleNum"), 5);
    params.insert(QStringLiteral("SampleMode"), false);
    params.insert(QStringLiteral("UseVertexOnly"), true);
    params.insert(QStringLiteral("MinPointNum"), 3);
    params.insert(QStringLiteral("MinDistAbs"), 0.5);
    params.insert(QStringLiteral("TrgDistAbs"), 0.000001);
    params.insert(QStringLiteral("MaxIterNum"), 20);
    params.insert(QStringLiteral("PassHiFilter"), 1.0);

    const MeshFilterRunResult result = doc.runFilter(icpKey, params);

    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QVERIFY(result.documentModified);
    QMatrix4x4 identity;
    identity.setToIdentity();
    QVERIFY(matrixNear(doc.mesh(sourceIndex).transform, identity));
}

void FilterTests::translateFilterMovesOnlyCurrentMesh()
{
    Document doc;
    VCGMesh firstCube;
    VCGMesh secondCube;
    makeCubeMesh(firstCube, 0.0f, 0.0f, 0.0f);
    makeCubeMesh(secondCube, 0.0f, 0.0f, 0.0f);
    const int mask =
        vcg::tri::io::Mask::IOM_VERTCOORD
        | vcg::tri::io::Mask::IOM_VERTNORMAL
        | vcg::tri::io::Mask::IOM_FACENORMAL;
    QCOMPARE(doc.addMesh(firstCube, QStringLiteral("Cube A"), mask), 0);
    QCOMPARE(doc.addMesh(secondCube, QStringLiteral("Cube B"), mask), 1);

    QString translateKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("compute_matrix_from_translation")) {
            translateKey = info.key;
            break;
        }
    }
    QVERIFY(!translateKey.isEmpty());

    doc.setCurrentMeshIndex(1);

    MeshFilterParameterValues params;
    params.insert(QStringLiteral("traslMethod"), QStringLiteral("xyz"));
    params.insert(QStringLiteral("axis"), QVector3D(1.0f, 0.0f, 0.0f));
    params.insert(QStringLiteral("Freeze"), false);
    params.insert(QStringLiteral("allLayers"), false);

    const MeshFilterRunResult result = doc.runFilter(translateKey, params);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QVERIFY(result.documentModified);

    QMatrix4x4 identity;
    identity.setToIdentity();
    QVERIFY(matrixNear(doc.mesh(0).transform, identity));

    QMatrix4x4 expected;
    expected.setToIdentity();
    expected.translate(1.0f, 0.0f, 0.0f);
    QVERIFY(matrixNear(doc.mesh(1).transform, expected));
}

void FilterTests::packTextureImagesCreatesGutteredAtlas()
{
    Document doc;
    VCGMesh mesh;
    makeTwoTextureTriangles(mesh);
    const int meshIndex = doc.addMesh(
        mesh,
        QStringLiteral("Two textures"),
        vcg::tri::io::Mask::IOM_VERTCOORD | vcg::tri::io::Mask::IOM_WEDGTEXCOORD);
    QCOMPARE(meshIndex, 0);

    QImage red(2, 2, QImage::Format_RGBA8888);
    QImage green(2, 2, QImage::Format_RGBA8888);
    red.fill(Qt::red);
    green.fill(Qt::green);
    TextureAssociationUtils::replaceTextureAssociations(
        doc.mesh(meshIndex),
        {
            TextureAssociationUtils::makeTextureAssetFromImage(red, QStringLiteral("red.png")),
            TextureAssociationUtils::makeTextureAssetFromImage(green, QStringLiteral("green.png"))
        });

    QString filterKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("pack_textures")) {
            filterKey = info.key;
            QCOMPARE(info.descriptor.name, QStringLiteral("Pack Texture Images"));
            break;
        }
    }
    QVERIFY(!filterKey.isEmpty());

    MeshFilterParameterValues params;
    params.insert(QStringLiteral("containerNum"), 1);
    params.insert(QStringLiteral("gutter"), 2);
    const MeshFilterRunResult result = doc.runFilter(filterKey, params);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QCOMPARE(result.newMeshIndices.size(), 1);

    const auto &output = doc.mesh(result.newMeshIndices.front());
    QCOMPARE(output.textureAssets.size(), size_t(1));
    const QImage &atlas = output.textureAssets.front().image;
    QVERIFY(!atlas.isNull());
    int redPixels = 0;
    int greenPixels = 0;
    for (int y = 0; y < atlas.height(); ++y) {
        for (int x = 0; x < atlas.width(); ++x) {
            const QColor color = atlas.pixelColor(x, y);
            redPixels += color == QColor(Qt::red);
            greenPixels += color == QColor(Qt::green);
        }
    }
    QVERIFY(redPixels >= 36);
    QVERIFY(greenPixels >= 36);
    for (const VCGFace &face : output.mesh.face) {
        for (int corner = 0; corner < 3; ++corner) {
            QCOMPARE(face.cWT(corner).N(), 0);
            QVERIFY(face.cWT(corner).U() > 0.0f && face.cWT(corner).U() < 1.0f);
            QVERIFY(face.cWT(corner).V() > 0.0f && face.cWT(corner).V() < 1.0f);
        }
    }
}

void FilterTests::faceQualityFiltersAreSplit()
{
    Document doc;
    VCGMesh mesh;
    makeOpenDiskMesh(mesh);
    mesh.face.EnableWedgeTexCoord();
    for (VCGFace &face : mesh.face) {
        for (int corner = 0; corner < 3; ++corner) {
            face.WT(corner).U() = face.cP(corner).X();
            face.WT(corner).V() = face.cP(corner).Y();
            face.WT(corner).N() = 0;
        }
    }
    QCOMPARE(
        doc.addMesh(
            mesh,
            QStringLiteral("Parameterized disk"),
            vcg::tri::io::Mask::IOM_VERTCOORD | vcg::tri::io::Mask::IOM_WEDGTEXCOORD),
        0);

    QString geometricFilterKey;
    QString textureFilterKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("compute_scalar_by_geometric_measure_per_face"))
            geometricFilterKey = info.key;
        else if (info.descriptor.id == QStringLiteral("compute_scalar_by_texture_distortion_per_face"))
            textureFilterKey = info.key;
    }
    QVERIFY(!geometricFilterKey.isEmpty());
    QVERIFY(!textureFilterKey.isEmpty());

    const MeshFilterRunResult geometricResult =
        doc.runFilter(geometricFilterKey, MeshFilterParameterValues{});
    QVERIFY2(geometricResult.success, qPrintable(geometricResult.errorMessage));

    const std::array<std::pair<QString, float>, 5> textureMetrics = {{
        { QStringLiteral("angle"), 0.0f },
        { QStringLiteral("area"), 0.0f },
        { QStringLiteral("edge"), 0.0f },
        { QStringLiteral("l2_stretch"), 1.0f },
        { QStringLiteral("linf_stretch"), 1.0f }
    }};
    for (const auto &[metric, expected] : textureMetrics) {
        MeshFilterParameterValues params;
        params.insert(QStringLiteral("metric"), metric);
        const MeshFilterRunResult result = doc.runFilter(textureFilterKey, params);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        for (const VCGFace &face : doc.mesh(0).mesh.face) {
            QVERIFY(std::isfinite(face.cQ()));
            QVERIFY(std::abs(face.cQ() - expected) < 1e-6f);
        }
    }

    for (VCGFace &face : doc.mesh(0).mesh.face)
        for (int corner = 0; corner < 3; ++corner)
            face.WT(corner).P() *= 7.0f;
    for (const auto &[metric, expected] : textureMetrics) {
        MeshFilterParameterValues params;
        params.insert(QStringLiteral("metric"), metric);
        const MeshFilterRunResult result = doc.runFilter(textureFilterKey, params);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        for (const VCGFace &face : doc.mesh(0).mesh.face)
            QVERIFY(std::abs(face.cQ() - expected) < 1e-5f);
    }

    for (VCGFace &face : doc.mesh(0).mesh.face)
        for (int corner = 0; corner < 3; ++corner)
            face.WT(corner).U() *= 2.0f;
    const std::array<std::pair<QString, float>, 2> anisotropicStretch = {{
        { QStringLiteral("l2_stretch"), std::sqrt(1.25f) },
        { QStringLiteral("linf_stretch"), std::sqrt(2.0f) }
    }};
    for (const auto &[metric, expected] : anisotropicStretch) {
        MeshFilterParameterValues params;
        params.insert(QStringLiteral("metric"), metric);
        const MeshFilterRunResult result = doc.runFilter(textureFilterKey, params);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        for (const VCGFace &face : doc.mesh(0).mesh.face)
            QVERIFY(std::abs(face.cQ() - expected) < 1e-5f);
    }
}

void FilterTests::libiglParametrizationFiltersRunWhenAvailable()
{
    Document doc;
    VCGMesh disk;
    makeOpenDiskMesh(disk);
    const int mask =
        vcg::tri::io::Mask::IOM_VERTCOORD
        | vcg::tri::io::Mask::IOM_VERTNORMAL
        | vcg::tri::io::Mask::IOM_FACENORMAL;
    QCOMPARE(doc.addMesh(disk, QStringLiteral("Open Disk"), mask), 0);

    QString harmonicKey;
    QString lscmKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("compute_texcoord_parametrization_harmonic"))
            harmonicKey = info.key;
        else if (info.descriptor.id == QStringLiteral("compute_texcoord_parametrization_least_squares_conformal_maps"))
            lscmKey = info.key;
    }

    if (harmonicKey.isEmpty() || lscmKey.isEmpty())
        QSKIP("libigl parametrization plugin is not available in this build.");

    {
        MeshFilterParameterValues params;
        params.insert(QStringLiteral("harm_function"), 1);
        const MeshFilterRunResult result = doc.runFilter(harmonicKey, params);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QVERIFY(result.documentModified);
        QVERIFY((doc.mesh(0).ioMask & vcg::tri::io::Mask::IOM_VERTTEXCOORD) != 0);
    }

    {
        const MeshFilterRunResult result = doc.runFilter(lscmKey, {});
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QVERIFY(result.documentModified);
        QVERIFY((doc.mesh(0).ioMask & vcg::tri::io::Mask::IOM_VERTTEXCOORD) != 0);
    }
}

void FilterTests::meshBooleanFiltersRunWhenAvailable()
{
    Document doc;
    const int mask =
        vcg::tri::io::Mask::IOM_VERTCOORD
        | vcg::tri::io::Mask::IOM_VERTNORMAL
        | vcg::tri::io::Mask::IOM_FACENORMAL;
    VCGMesh firstCube;
    VCGMesh secondCube;
    makeCubeMesh(firstCube, 0.0f, 0.0f, 0.0f);
    makeCubeMesh(secondCube, 0.5f, 0.5f, 0.5f);
    const int firstIndex = doc.addMesh(firstCube, QStringLiteral("Cube A"), mask);
    const int secondIndex = doc.addMesh(secondCube, QStringLiteral("Cube B"), mask);
    QVERIFY(firstIndex >= 0);
    QVERIFY(secondIndex >= 0);

    QString intersectionKey;
    QString unionKey;
    QString differenceKey;
    QString xorKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("generate_boolean_intersection"))
            intersectionKey = info.key;
        else if (info.descriptor.id == QStringLiteral("generate_boolean_union"))
            unionKey = info.key;
        else if (info.descriptor.id == QStringLiteral("generate_boolean_difference"))
            differenceKey = info.key;
        else if (info.descriptor.id == QStringLiteral("generate_boolean_xor"))
            xorKey = info.key;
    }

    if (unionKey.isEmpty())
        QSKIP("libigl/CGAL mesh boolean plugin is not available in this build.");
    QVERIFY(!intersectionKey.isEmpty());
    QVERIFY(!differenceKey.isEmpty());
    QVERIFY(!xorKey.isEmpty());

    MeshFilterParameterValues params;
    params.insert(QStringLiteral("first_mesh"), firstIndex);
    params.insert(QStringLiteral("second_mesh"), secondIndex);
    const int meshCountBefore = doc.meshCount();
    const MeshFilterRunResult result = doc.runFilter(unionKey, params);

    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QVERIFY(result.documentModified);
    QCOMPARE(result.newMeshIndices.size(), 1);
    QCOMPARE(doc.meshCount(), meshCountBefore + 1);

    const int generatedIndex = result.newMeshIndices.front();
    QVERIFY(generatedIndex >= 0 && generatedIndex < doc.meshCount());
    QVERIFY(doc.mesh(generatedIndex).mesh.VN() > 0);
    QVERIFY(doc.mesh(generatedIndex).mesh.FN() > 0);
}

QTEST_MAIN(FilterTests)
#include "test_filters.moc"
