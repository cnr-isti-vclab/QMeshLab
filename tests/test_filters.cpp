#include <QtTest/QtTest>

#include <QSet>

#include <map>

#include "document.h"

#include <vcg/complex/algorithms/stat.h>
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

void makeIsolatedFoldMesh(VCGMesh &mesh)
{
    mesh.Clear();
    const std::array<vcg::Point3f, 6> vertices = {
        vcg::Point3f(0.0f, 0.0f, 0.0f), vcg::Point3f(2.0f, 0.0f, 0.0f),
        vcg::Point3f(1.0f, 2.0f, 0.0f), vcg::Point3f(1.0f, 4.0f, 0.0f),
        vcg::Point3f(-1.5f, -1.0f, 0.0f), vcg::Point3f(3.5f, -1.0f, 0.0f)
    };
    for (const vcg::Point3f &point : vertices)
        vcg::tri::Allocator<VCGMesh>::AddVertex(mesh, point);

    // Face 0 points upward, while its three consistently oriented neighbours
    // overlap it and point downward.  Each opposite vertex lies strictly inside
    // the corresponding neighbour, so the central fold is repairable by a flip.
    vcg::tri::Allocator<VCGMesh>::AddFace(mesh, 0, 1, 2);
    vcg::tri::Allocator<VCGMesh>::AddFace(mesh, 1, 0, 3);
    vcg::tri::Allocator<VCGMesh>::AddFace(mesh, 2, 1, 4);
    vcg::tri::Allocator<VCGMesh>::AddFace(mesh, 0, 2, 5);
    vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
    vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(mesh);
}

bool isSelectedEdge(const VCGMesh &mesh, int firstVertex, int secondVertex)
{
    for (const VCGFace &face : mesh.face) {
        for (int edge = 0; edge < 3; ++edge) {
            const int a = int(vcg::tri::Index(mesh, face.cV0(edge)));
            const int b = int(vcg::tri::Index(mesh, face.cV1(edge)));
            if (((a == firstVertex && b == secondVertex)
                    || (a == secondVertex && b == firstVertex))
                && face.IsFaceEdgeS(edge))
                return true;
        }
    }
    return false;
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

QString filterKeyForId(const Document &doc, const QString &filterId);

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
    void catmullClarkSubdividesCube();
    void polygonFaceCountCacheTracksFilterChanges();
    void isolatedFoldRepairPreservesFlagsAndConverges();
    void isolatedFoldRepairRejectsUnsupportedMeshes();
    void vertexDisplacementFiltersRunOnCube();
    void sphericalCapPointCreationIsAreaUniform();
    void splitConnectedComponentsAfterDuplicateVertexRemoval();
    void hausdorffRunsOnTransientMeshCopies();
    void cgalAlphaWrapRunsWhenAvailable();
    void cgalAlphaWrapAcceptsPointClouds();
    void convexHullOfIcosahedronIsTheIcosahedron();
    void selectVisibleVerticesSelectsNearSideOnly();
    void selectVisibleVerticesRejectsEnclosedViewpoint();
    void alphaShapeConvergesToConvexHull();
    void alphaShapeHandlesALargePointSet();
    void voronoiFilteringReconstructsASphere();
    void advancingFrontReconstructsASphere();
    void scaleSpaceReconstructsASphere();
    void orientNormalsFlipsInvertedNormals();
    void cgalPoissonReconstructsASphere();
    void kineticReconstructsABox();
    void trueFormAlignmentRecoversAKnownTransform();
    void trueFormBooleansAgreeWithVolume();
    void trueFormCsgExpressionMatchesPairwiseBooleans();
    void trueFormCurveFamilyProducesPolylines();
    void newMeshFiltersReportTheirLayers();
    void trueFormDistanceAndContainment();
    void trueFormAttributeFiltersBehaveAsDocumented();
    void trueFormRemeshingChangesResolutionAsAsked();
    void trueFormOrientationAndEdgeSelection();
    void trueFormRepairAndIsobands();
    void trueFormImproveTriangulationRaisesQuality();
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
    void ambientOcclusionIsScaleInvariant();
    void ambientOcclusionSupportsPointCloudsAndDirectionalLighting();
    void randomSeedMakesSamplingReproducible();
    void randomSeedZeroVariesBetweenRuns();
    void randomSeedControlsExpressionRnd();
    void randomizedFiltersDeclareARandomSeed();
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

void FilterTests::catmullClarkSubdividesCube()
{
    Document doc;
    VCGMesh cube;
    makeCubeMesh(cube, 0.0f, 0.0f, 0.0f);
    QVERIFY(doc.addMesh(cube, QStringLiteral("Cube")) >= 0);

    QString filterKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("meshing_surface_subdivision_catmull_clark")) {
            filterKey = info.key;
            break;
        }
    }
    QVERIFY(!filterKey.isEmpty());
    const MeshFilterRunResult result = doc.runFilter(filterKey, {});
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QVERIFY(doc.mesh(doc.currentMeshIndex()).mesh.FN() > 12);
}

void FilterTests::polygonFaceCountCacheTracksFilterChanges()
{
    VCGMesh quad;
    for (const vcg::Point3f &point : {
             vcg::Point3f(0, 0, 0), vcg::Point3f(1, 0, 0),
             vcg::Point3f(1, 1, 0), vcg::Point3f(0, 1, 0) })
        vcg::tri::Allocator<VCGMesh>::AddVertex(quad, point);
    vcg::tri::Allocator<VCGMesh>::AddFace(quad, 0, 1, 2);
    vcg::tri::Allocator<VCGMesh>::AddFace(quad, 0, 2, 3);
    quad.face[0].SetF(2);
    quad.face[1].SetF(0);

    Document doc;
    const int meshIndex = doc.addMesh(
        quad, QStringLiteral("Quad"), vcg::tri::io::Mask::IOM_BITPOLYGONAL);
    QCOMPARE(doc.mesh(meshIndex).polygonFaceCount, 1);

    QString toTrianglesKey;
    QString toQuadsKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("meshing_poly_to_tri"))
            toTrianglesKey = info.key;
        else if (info.descriptor.id == QStringLiteral("meshing_tri_to_quad_by_smart_triangle_pairing"))
            toQuadsKey = info.key;
    }
    QVERIFY(!toTrianglesKey.isEmpty());
    QVERIFY(!toQuadsKey.isEmpty());

    QVERIFY2(doc.runFilter(toTrianglesKey, {}).success, "Convert to triangles failed");
    QCOMPARE(doc.mesh(meshIndex).polygonFaceCount, -1);
    QVERIFY(!(doc.mesh(meshIndex).ioMask & vcg::tri::io::Mask::IOM_BITPOLYGONAL));

    QVERIFY2(doc.runFilter(toQuadsKey, {}).success, "Convert to quads failed");
    QCOMPARE(doc.mesh(meshIndex).polygonFaceCount, 1);
    QVERIFY(doc.mesh(meshIndex).ioMask & vcg::tri::io::Mask::IOM_BITPOLYGONAL);
}

void FilterTests::isolatedFoldRepairPreservesFlagsAndConverges()
{
    VCGMesh folded;
    makeIsolatedFoldMesh(folded);
    folded.face[0].SetV(); // Scratch visited state must not affect detection.
    folded.face[1].SetS();
    folded.face[0].SetFaceEdgeS(2); // Boundary edge 2-0.
    folded.face[1].SetFaceEdgeS(1); // Boundary edge 0-3.

    Document doc;
    const int meshIndex = doc.addMesh(folded, QStringLiteral("Isolated fold"));
    QString filterKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("remove_folded_faces_by_edge_flip")) {
            filterKey = info.key;
            break;
        }
    }
    QVERIFY(!filterKey.isEmpty());

    const MeshFilterRunResult result = doc.runFilter(filterKey, {});
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QVERIFY(result.documentModified);

    const VCGMesh &repaired = doc.mesh(meshIndex).mesh;
    QCOMPARE(repaired.FN(), 4);
    QVERIFY(repaired.face[0].IsV());
    QVERIFY(repaired.face[1].IsS());
    QVERIFY(!repaired.face[0].IsS());
    QVERIFY(isSelectedEdge(repaired, 2, 0));
    QVERIFY(isSelectedEdge(repaired, 0, 3));

    // The monotone local score must leave no further repair for a second run.
    const MeshFilterRunResult second = doc.runFilter(filterKey, {});
    QVERIFY2(second.success, qPrintable(second.errorMessage));
    QVERIFY(!second.documentModified);
}

void FilterTests::isolatedFoldRepairRejectsUnsupportedMeshes()
{
    QString filterKey;
    Document registry;
    for (const auto &info : registry.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("remove_folded_faces_by_edge_flip")) {
            filterKey = info.key;
            break;
        }
    }
    QVERIFY(!filterKey.isEmpty());

    VCGMesh polygon;
    makeIsolatedFoldMesh(polygon);
    polygon.face[0].SetF(0);
    Document polygonDoc;
    polygonDoc.addMesh(
        polygon, QStringLiteral("Polygonal"), vcg::tri::io::Mask::IOM_BITPOLYGONAL);
    const MeshFilterRunResult polygonResult = polygonDoc.runFilter(filterKey, {});
    QVERIFY(!polygonResult.success);
    QVERIFY(polygonResult.errorMessage.contains(QStringLiteral("triangle mesh")));

    VCGMesh textured;
    makeIsolatedFoldMesh(textured);
    textured.face.EnableWedgeTexCoord();
    Document texturedDoc;
    texturedDoc.addMesh(
        textured, QStringLiteral("Textured"), vcg::tri::io::Mask::IOM_WEDGTEXCOORD);
    const MeshFilterRunResult texturedResult = texturedDoc.runFilter(filterKey, {});
    QVERIFY(!texturedResult.success);
    QVERIFY(texturedResult.errorMessage.contains(QStringLiteral("texture seams")));

    VCGMesh nonManifold;
    for (const vcg::Point3f &point : {
             vcg::Point3f(0, 0, 0), vcg::Point3f(1, 0, 0), vcg::Point3f(0, 1, 0),
             vcg::Point3f(0, -1, 0), vcg::Point3f(0, 0, 1) })
        vcg::tri::Allocator<VCGMesh>::AddVertex(nonManifold, point);
    vcg::tri::Allocator<VCGMesh>::AddFace(nonManifold, 0, 1, 2);
    vcg::tri::Allocator<VCGMesh>::AddFace(nonManifold, 1, 0, 3);
    vcg::tri::Allocator<VCGMesh>::AddFace(nonManifold, 0, 1, 4);
    Document nonManifoldDoc;
    nonManifoldDoc.addMesh(nonManifold, QStringLiteral("Non-manifold"));
    const MeshFilterRunResult nonManifoldResult = nonManifoldDoc.runFilter(filterKey, {});
    QVERIFY(!nonManifoldResult.success);
    QVERIFY(nonManifoldResult.errorMessage.contains(QStringLiteral("2-manifold")));

    VCGMesh unoriented;
    makeIsolatedFoldMesh(unoriented);
    std::swap(unoriented.face[1].V(0), unoriented.face[1].V(1));
    Document unorientedDoc;
    unorientedDoc.addMesh(unoriented, QStringLiteral("Unoriented"));
    const MeshFilterRunResult unorientedResult = unorientedDoc.runFilter(filterKey, {});
    QVERIFY(!unorientedResult.success);
    QVERIFY(unorientedResult.errorMessage.contains(QStringLiteral("oriented")));
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

void FilterTests::sphericalCapPointCreationIsAreaUniform()
{
    Document probe;
    const QString filterKey = filterKeyForId(
        probe, QStringLiteral("create_points_on_a_spherical_cap"));
    QVERIFY(!filterKey.isEmpty());

    const vcg::Point3f axis = vcg::Normalized(vcg::Point3f(1.0f, 2.0f, 3.0f));
    const float halfAngle = vcg::math::ToRad(35.0f);
    const float expectedMeanProjection = (1.0f + std::cos(halfAngle)) * 0.5f;
    for (const QString &technique : { QStringLiteral("fibonacci"), QStringLiteral("montecarlo") }) {
        Document doc;
        MeshFilterParameterValues params;
        params.insert(QStringLiteral("point_num"), 1000);
        params.insert(QStringLiteral("direction"), QVector3D(1.0f, 2.0f, 3.0f));
        params.insert(QStringLiteral("angle"), 35.0);
        params.insert(QStringLiteral("technique"), technique);
        params.insert(QStringLiteral("randomSeed"), 12345);
        const MeshFilterRunResult result = doc.runFilter(filterKey, params);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.newMeshIndices.size(), 1);

        const VCGMesh &points = doc.mesh(result.newMeshIndices.front()).mesh;
        QCOMPARE(points.VN(), 1000);
        QCOMPARE(points.FN(), 0);
        double meanProjection = 0.0;
        for (const VCGVertex &vertex : points.vert) {
            QVERIFY(std::abs(vertex.cP().Norm() - 1.0f) < 1e-5f);
            QVERIFY(axis * vertex.cP() >= std::cos(halfAngle) - 1e-5f);
            QVERIFY((vertex.cN() - vertex.cP()).Norm() < 1e-5f);
            meanProjection += axis * vertex.cP();
        }
        meanProjection /= points.VN();
        QVERIFY(std::abs(meanProjection - expectedMeanProjection) < 0.01);
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
    const std::size_t historySize = doc.undoTreeInfo().size();
    const MeshFilterRunResult result = doc.runFilter(filterKey, params);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QVERIFY(!result.documentModified);
    QVERIFY(result.infoMessages.join(QLatin1Char('\n')).contains(QStringLiteral("Samples:")));
    QCOMPARE(doc.undoTreeInfo().size(), historySize);
    const auto informationalActions = doc.undoNodeScriptActions(doc.undoCurrentNodeId());
    QVERIFY(std::any_of(
        informationalActions.begin(), informationalActions.end(),
        [&filterKey](const ScriptAction &action) { return action.filterKey == filterKey; }));

    params.insert(QStringLiteral("SaveSample"), true);
    const int meshCountBeforeSamples = doc.meshCount();
    const MeshFilterRunResult savedResult = doc.runFilter(filterKey, params);
    QVERIFY2(savedResult.success, qPrintable(savedResult.errorMessage));
    QVERIFY(savedResult.documentModified);
    QCOMPARE(doc.meshCount(), meshCountBeforeSamples + 2);
    QCOMPARE(doc.undoTreeInfo().size(), historySize + 1);
    QCOMPARE(doc.undoText(), QStringLiteral("Hausdorff Distance"));
    const auto savedActions = doc.undoNodeScriptActions(doc.undoCurrentNodeId());
    QCOMPARE(int(std::count_if(
        savedActions.begin(), savedActions.end(),
        [&filterKey](const ScriptAction &action) { return action.filterKey == filterKey; })), 2);
    QVERIFY(doc.undo());
    QCOMPARE(doc.meshCount(), meshCountBeforeSamples);
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

// Alpha wrapping does not need faces: CGAL wraps a bare point set just as well, and
// needs no normals because the strictly positive offset defines the envelope. This is
// what makes the filter a genuine point-cloud reconstruction method.
void FilterTests::cgalAlphaWrapAcceptsPointClouds()
{
    Document doc;
    const QString path = QStringLiteral(TEST_SOURCE_DIR "/tests/data/simple.off");
    QCOMPARE(doc.loadMesh(path), 0);

    QString alphaWrapKey;
    QString removeFacesKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("generate_alpha_wrap"))
            alphaWrapKey = info.key;
        else if (info.descriptor.id == QStringLiteral("delete_all_faces"))
            removeFacesKey = info.key;
    }

    if (alphaWrapKey.isEmpty())
        QSKIP("CGAL Alpha Wrap plugin is not available in this build.");
    QVERIFY(!removeFacesKey.isEmpty());

    // Turn the loaded mesh into a pure point cloud through the normal filter path.
    QVERIFY2(doc.runFilter(removeFacesKey, {}).success, "Remove All Faces failed");
    QCOMPARE(doc.mesh(doc.currentMeshIndex()).mesh.FN(), 0);
    QVERIFY(doc.mesh(doc.currentMeshIndex()).mesh.VN() > 0);

    MeshFilterParameterValues params;
    params.insert(QStringLiteral("Alpha"), 0.5);
    params.insert(QStringLiteral("Offset"), 0.05);
    const int meshCountBefore = doc.meshCount();
    const MeshFilterRunResult result = doc.runFilter(alphaWrapKey, params);

    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QCOMPARE(result.newMeshIndices.size(), 1);
    QCOMPARE(doc.meshCount(), meshCountBefore + 1);

    const int generatedIndex = result.newMeshIndices.front();
    QVERIFY(doc.mesh(generatedIndex).mesh.VN() > 0);
    QVERIFY(doc.mesh(generatedIndex).mesh.FN() > 0);
}

// An icosahedron is convex and simplicial, so it is its own convex hull: the result must
// come back with exactly the same 12 vertices and 20 faces. That also satisfies the
// invariant F == 2V - 4 that holds for any triangulated convex hull, which is asserted
// separately so the intent survives if the input mesh is ever swapped.
void FilterTests::convexHullOfIcosahedronIsTheIcosahedron()
{
    Document doc;

    QString icosaKey;
    QString hullKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("create_icosahedron"))
            icosaKey = info.key;
        else if (info.descriptor.id == QStringLiteral("create_convex_hull"))
            hullKey = info.key;
    }
    QVERIFY(!icosaKey.isEmpty());
    QVERIFY(!hullKey.isEmpty());

    QVERIFY2(doc.runFilter(icosaKey, {}).success, "create_icosahedron failed");
    const int sourceIndex = doc.currentMeshIndex();
    const int sourceVN = doc.mesh(sourceIndex).mesh.VN();
    const int sourceFN = doc.mesh(sourceIndex).mesh.FN();
    QCOMPARE(sourceVN, 12);
    QCOMPARE(sourceFN, 20);

    const MeshFilterRunResult result = doc.runFilter(hullKey, {});
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QCOMPARE(result.newMeshIndices.size(), 1);

    const VCGMesh &hull = doc.mesh(result.newMeshIndices.front()).mesh;
    QCOMPARE(hull.VN(), 12);
    QCOMPARE(hull.FN(), 20);
    QCOMPARE(hull.FN(), 2 * hull.VN() - 4);

    // The hull is computed on a copy: vcglib compacts its input and clears the visited
    // flags, which would silently reindex the source layer.
    QCOMPARE(doc.mesh(sourceIndex).mesh.VN(), sourceVN);
    QCOMPARE(doc.mesh(sourceIndex).mesh.FN(), sourceFN);
}

// From a viewpoint outside the shape, hidden point removal must select some but not all
// vertices, and every selected one must lie on the near side of the centre.
void FilterTests::selectVisibleVerticesSelectsNearSideOnly()
{
    Document doc;

    QString icosaKey;
    QString visibleKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("create_icosahedron"))
            icosaKey = info.key;
        else if (info.descriptor.id == QStringLiteral("select_visible_vertices"))
            visibleKey = info.key;
    }
    QVERIFY(!icosaKey.isEmpty());
    QVERIFY(!visibleKey.isEmpty());
    QVERIFY2(doc.runFilter(icosaKey, {}).success, "create_icosahedron failed");

    const int index = doc.currentMeshIndex();
    const vcg::Point3f viewpoint(0.0f, 0.0f, 10.0f);

    MeshFilterParameterValues params;
    params.insert(QStringLiteral("radiusThreshold"), 0.0);
    params.insert(QStringLiteral("usecamera"), false);
    params.insert(QStringLiteral("viewpoint"), QVector3D(0.0f, 0.0f, 10.0f));

    const MeshFilterRunResult result = doc.runFilter(visibleKey, params);
    QVERIFY2(result.success, qPrintable(result.errorMessage));

    const VCGMesh &mesh = doc.mesh(index).mesh;
    int selected = 0;
    for (const VCGVertex &v : mesh.vert)
        if (v.IsS()) {
            ++selected;
            // Near side: the icosahedron is centred on the origin, so a vertex facing a
            // viewpoint at +Z cannot have a strongly negative z.
            QVERIFY(v.cP().Z() > -1e-4f);
        }
    QVERIFY(selected > 0);
    QVERIFY(selected < mesh.VN());
}

// The default viewpoint is (0,0,0), which is inside any centred mesh — and hidden point
// removal is undefined there. vcglib's ComputePointVisibility asserts and aborts in that
// case, so the filter composes ComputeConvexHull itself; this pins the clean failure and
// guards against anyone "simplifying" it back to the wrapper.
void FilterTests::selectVisibleVerticesRejectsEnclosedViewpoint()
{
    Document doc;

    QString icosaKey;
    QString visibleKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("create_icosahedron"))
            icosaKey = info.key;
        else if (info.descriptor.id == QStringLiteral("select_visible_vertices"))
            visibleKey = info.key;
    }
    QVERIFY(!icosaKey.isEmpty());
    QVERIFY(!visibleKey.isEmpty());
    QVERIFY2(doc.runFilter(icosaKey, {}).success, "create_icosahedron failed");
    const int index = doc.currentMeshIndex();

    MeshFilterParameterValues params;
    params.insert(QStringLiteral("radiusThreshold"), 0.0);
    params.insert(QStringLiteral("usecamera"), false);
    params.insert(QStringLiteral("viewpoint"), QVector3D(0.0f, 0.0f, 0.0f));

    const MeshFilterRunResult result = doc.runFilter(visibleKey, params);
    QVERIFY(!result.success);
    QVERIFY(result.errorMessage.contains(QStringLiteral("enclosed")));

    // A failed filter must leave no partial selection behind.
    const VCGMesh &mesh = doc.mesh(index).mesh;
    for (const VCGVertex &v : mesh.vert)
        QVERIFY(!v.IsS());
}

// As alpha grows the alpha shape converges to the convex hull, so a large alpha on an
// icosahedron must give back its 20 faces. A small alpha must not.
void FilterTests::alphaShapeConvergesToConvexHull()
{
    Document doc;

    QString icosaKey;
    QString alphaKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("create_icosahedron"))
            icosaKey = info.key;
        else if (info.descriptor.id == QStringLiteral("generate_alpha_shape"))
            alphaKey = info.key;
    }
    QVERIFY(!icosaKey.isEmpty());
    if (alphaKey.isEmpty())
        QSKIP("CGAL plugin is not available in this build.");
    QVERIFY2(doc.runFilter(icosaKey, {}).success, "create_icosahedron failed");

    // alpha is an absperc bounded by the bounding-box diagonal, which for a convex point
    // set is comfortably past the largest Delaunay circumradius — so the shape has
    // converged to the hull.
    MeshFilterParameterValues big;
    big.insert(QStringLiteral("alpha"),
               double(doc.mesh(doc.currentMeshIndex()).mesh.bbox.Diag()));
    big.insert(QStringLiteral("output"), QStringLiteral("shape"));
    const MeshFilterRunResult result = doc.runFilter(alphaKey, big);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QCOMPARE(result.newMeshIndices.size(), 1);

    const VCGMesh &shape = doc.mesh(result.newMeshIndices.front()).mesh;
    QCOMPARE(shape.VN(), 12);
    QCOMPARE(shape.FN(), 20);

    // Face scalar carries the circumradius, so it must be populated and positive.
    for (const VCGFace &f : shape.face)
        QVERIFY(f.cQ() > 0.0f);
}

// Regression: the facet handles point into the CGAL triangulation, so the output mesh
// must be built while it is still alive. Consuming them afterwards is a use-after-free
// that a 12-vertex icosahedron hides (the freed cells are simply not reused yet) and a
// real model reliably crashes on. A subdivided sphere allocates enough to expose it,
// and the interpolating property gives a cheap correctness check: every output vertex
// is an input point, so all of them must sit on the source bounding box.
void FilterTests::alphaShapeHandlesALargePointSet()
{
    Document doc;

    QString sphereKey;
    QString alphaKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("create_sphere"))
            sphereKey = info.key;
        else if (info.descriptor.id == QStringLiteral("generate_alpha_shape"))
            alphaKey = info.key;
    }
    QVERIFY(!sphereKey.isEmpty());
    if (alphaKey.isEmpty())
        QSKIP("CGAL plugin is not available in this build.");

    QVERIFY2(doc.runFilter(sphereKey, {}).success, "create_sphere failed");
    const VCGMesh &source = doc.mesh(doc.currentMeshIndex()).mesh;
    const int sourceVN = source.VN();
    QVERIFY2(sourceVN > 500, "sphere too small to exercise the allocator");
    const vcg::Box3f sourceBox = source.bbox;

    MeshFilterParameterValues params;
    params.insert(QStringLiteral("alpha"), double(sourceBox.Diag()));
    params.insert(QStringLiteral("output"), QStringLiteral("shape"));
    const MeshFilterRunResult result = doc.runFilter(alphaKey, params);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QCOMPARE(result.newMeshIndices.size(), 1);

    const VCGMesh &shape = doc.mesh(result.newMeshIndices.front()).mesh;
    QVERIFY(shape.FN() > 0);
    QVERIFY(shape.VN() > 0);
    // Interpolating: output vertices are a subset of the input points.
    QVERIFY(shape.VN() <= sourceVN);

    // A dangling handle yields garbage coordinates rather than input points.
    vcg::Box3f tolerant = sourceBox;
    tolerant.Offset(sourceBox.Diag() * 0.001f);
    for (const VCGVertex &v : shape.vert) {
        const vcg::Point3f &p = v.cP();
        QVERIFY(std::isfinite(p.X()) && std::isfinite(p.Y()) && std::isfinite(p.Z()));
        QVERIFY2(tolerant.IsIn(p), "alpha shape vertex outside the input bounding box");
    }
}

// The crust needs a closed, well-sampled surface, so a subdivided sphere is the fair
// test. It is an interpolating reconstruction: every output vertex must be an input
// point, so the vertex count can never exceed the input's.
void FilterTests::voronoiFilteringReconstructsASphere()
{
    Document doc;

    QString sphereKey;
    QString voronoiKey;
    QString removeFacesKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("create_sphere"))
            sphereKey = info.key;
        else if (info.descriptor.id == QStringLiteral("generate_voronoi_filtering"))
            voronoiKey = info.key;
        else if (info.descriptor.id == QStringLiteral("delete_all_faces"))
            removeFacesKey = info.key;
    }
    QVERIFY(!sphereKey.isEmpty());
    QVERIFY(!removeFacesKey.isEmpty());
    if (voronoiKey.isEmpty())
        QSKIP("CGAL plugin is not available in this build.");

    QVERIFY2(doc.runFilter(sphereKey, {}).success, "create_sphere failed");
    // Strip the faces: the point of this filter is that it works from points alone.
    QVERIFY2(doc.runFilter(removeFacesKey, {}).success, "Remove All Faces failed");
    const int sourceVN = doc.mesh(doc.currentMeshIndex()).mesh.VN();
    QCOMPARE(doc.mesh(doc.currentMeshIndex()).mesh.FN(), 0);
    QVERIFY(sourceVN >= 4);

    MeshFilterParameterValues params;
    params.insert(QStringLiteral("threshold"), 10.0);
    const MeshFilterRunResult result = doc.runFilter(voronoiKey, params);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QCOMPARE(result.newMeshIndices.size(), 1);

    const VCGMesh &crust = doc.mesh(result.newMeshIndices.front()).mesh;
    QVERIFY(crust.FN() > 0);
    QVERIFY(crust.VN() > 0);
    // Interpolating: output vertices are a subset of the input points.
    QVERIFY(crust.VN() <= sourceVN);
}

// Advancing front is interpolating, so like the crust its vertices are a subset of the
// input points — which is also the cheap check that it did not invent geometry.
void FilterTests::advancingFrontReconstructsASphere()
{
    Document doc;

    QString sphereKey;
    QString frontKey;
    QString removeFacesKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("create_sphere"))
            sphereKey = info.key;
        else if (info.descriptor.id == QStringLiteral("generate_advancing_front_reconstruction"))
            frontKey = info.key;
        else if (info.descriptor.id == QStringLiteral("delete_all_faces"))
            removeFacesKey = info.key;
    }
    QVERIFY(!sphereKey.isEmpty());
    QVERIFY(!removeFacesKey.isEmpty());
    if (frontKey.isEmpty())
        QSKIP("CGAL plugin is not available in this build.");

    QVERIFY2(doc.runFilter(sphereKey, {}).success, "create_sphere failed");
    QVERIFY2(doc.runFilter(removeFacesKey, {}).success, "Remove All Faces failed");
    const VCGMesh &source = doc.mesh(doc.currentMeshIndex()).mesh;
    const int sourceVN = source.VN();
    const vcg::Box3f sourceBox = source.bbox;
    QCOMPARE(source.FN(), 0);

    MeshFilterParameterValues params;
    params.insert(QStringLiteral("radiusRatioBound"), 5.0);
    params.insert(QStringLiteral("beta"), 30.0);
    const MeshFilterRunResult result = doc.runFilter(frontKey, params);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QCOMPARE(result.newMeshIndices.size(), 1);

    const VCGMesh &surface = doc.mesh(result.newMeshIndices.front()).mesh;
    QVERIFY(surface.FN() > 0);
    QVERIFY(surface.VN() > 0);
    QVERIFY(surface.VN() <= sourceVN);

    vcg::Box3f tolerant = sourceBox;
    tolerant.Offset(sourceBox.Diag() * 0.001f);
    for (const VCGVertex &v : surface.vert)
        QVERIFY2(tolerant.IsIn(v.cP()), "advancing front vertex outside the input bbox");
}

// Scale space is the one reconstruction here whose output vertices are *not* the input
// points: smoothing moves them before meshing. So the subset check does not apply — but
// the result must still stay near the input, and both meshers must produce a surface.
void FilterTests::scaleSpaceReconstructsASphere()
{
    Document doc;

    QString sphereKey;
    QString scaleSpaceKey;
    QString removeFacesKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("create_sphere"))
            sphereKey = info.key;
        else if (info.descriptor.id == QStringLiteral("generate_scale_space_reconstruction"))
            scaleSpaceKey = info.key;
        else if (info.descriptor.id == QStringLiteral("delete_all_faces"))
            removeFacesKey = info.key;
    }
    QVERIFY(!sphereKey.isEmpty());
    QVERIFY(!removeFacesKey.isEmpty());
    if (scaleSpaceKey.isEmpty())
        QSKIP("CGAL plugin is not available in this build.");

    QVERIFY2(doc.runFilter(sphereKey, {}).success, "create_sphere failed");
    QVERIFY2(doc.runFilter(removeFacesKey, {}).success, "Remove All Faces failed");
    const vcg::Box3f sourceBox = doc.mesh(doc.currentMeshIndex()).mesh.bbox;

    // Both meshers must work; the alpha one additionally exercises the absperc bound.
    const QVector<QString> meshers{ QStringLiteral("alpha_shape"),
                                    QStringLiteral("advancing_front") };
    for (const QString &mesher : meshers) {
        MeshFilterParameterValues params;
        params.insert(QStringLiteral("iterations"), 2);
        params.insert(QStringLiteral("mesher"), mesher);
        params.insert(QStringLiteral("alpha"), double(sourceBox.Diag()) * 0.25);
        const MeshFilterRunResult result = doc.runFilter(scaleSpaceKey, params);
        QVERIFY2(result.success, qPrintable(QStringLiteral("%1: %2")
                                                .arg(mesher, result.errorMessage)));
        QCOMPARE(result.newMeshIndices.size(), 1);

        const VCGMesh &surface = doc.mesh(result.newMeshIndices.front()).mesh;
        QVERIFY2(surface.FN() > 0, qPrintable(mesher));
        QVERIFY2(surface.VN() > 0, qPrintable(mesher));

        // Smoothing moves points, so allow generous slack — this only catches garbage.
        vcg::Box3f tolerant = sourceBox;
        tolerant.Offset(sourceBox.Diag() * 0.5f);
        for (const VCGVertex &v : surface.vert)
            QVERIFY2(tolerant.IsIn(v.cP()), qPrintable(mesher));
    }
}

// A sphere's normals should all point away from its centre. Flipping half of them gives
// an inconsistently oriented field of exactly the kind normal estimation produces, and
// MST orientation must repair it — that is the whole point of the filter.
void FilterTests::orientNormalsFlipsInvertedNormals()
{
    Document doc;

    QString sphereKey;
    QString orientKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("create_sphere"))
            sphereKey = info.key;
        else if (info.descriptor.id == QStringLiteral("compute_normal_orientation_per_vertex"))
            orientKey = info.key;
    }
    QVERIFY(!sphereKey.isEmpty());
    if (orientKey.isEmpty())
        QSKIP("CGAL plugin is not available in this build.");

    QVERIFY2(doc.runFilter(sphereKey, {}).success, "create_sphere failed");
    const int index = doc.currentMeshIndex();

    // Corrupt the orientation: flip every other normal.
    {
        VCGMesh &mesh = doc.mesh(index).mesh;
        const vcg::Point3f centre = mesh.bbox.Center();
        int flipped = 0;
        for (std::size_t i = 0; i < mesh.vert.size(); ++i) {
            // Start from the outward normal so the test does not depend on the primitive.
            mesh.vert[i].N() = (mesh.vert[i].cP() - centre).Normalize();
            if (i % 2 == 0) {
                mesh.vert[i].N() = -mesh.vert[i].cN();
                ++flipped;
            }
        }
        QVERIFY(flipped > 0);
    }

    MeshFilterParameterValues params;
    params.insert(QStringLiteral("neighbors"), 18);
    const MeshFilterRunResult result = doc.runFilter(orientKey, params);
    QVERIFY2(result.success, qPrintable(result.errorMessage));

    // Every normal must now agree with the outward radial direction, up to a global
    // sign: MST orientation makes the field consistent, not necessarily outward.
    const VCGMesh &mesh = doc.mesh(index).mesh;
    const vcg::Point3f centre = mesh.bbox.Center();
    int agreeing = 0;
    int total = 0;
    for (const VCGVertex &v : mesh.vert) {
        if (v.IsD())
            continue;
        const vcg::Point3f radial = (v.cP() - centre).Normalize();
        if (radial.dot(v.cN()) > 0.0f)
            ++agreeing;
        ++total;
    }
    QVERIFY(total > 0);
    // Consistent means all-with or all-against; either way one of the two counts is 0.
    QVERIFY2(agreeing == total || agreeing == 0,
             qPrintable(QStringLiteral("%1 of %2 normals point outward — the field is "
                                       "still inconsistent").arg(agreeing).arg(total)));
}

// CGAL's Poisson needs oriented normals, so this also exercises the pairing with the
// orientation filter above: estimate-free radial normals, then reconstruct.
void FilterTests::cgalPoissonReconstructsASphere()
{
    Document doc;

    QString sphereKey;
    QString poissonKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("create_sphere"))
            sphereKey = info.key;
        else if (info.descriptor.id == QStringLiteral("generate_poisson_reconstruction_cgal"))
            poissonKey = info.key;
    }
    QVERIFY(!sphereKey.isEmpty());
    if (poissonKey.isEmpty())
        QSKIP("CGAL plugin is not available in this build.");

    QVERIFY2(doc.runFilter(sphereKey, {}).success, "create_sphere failed");
    const int index = doc.currentMeshIndex();
    vcg::Box3f sourceBox;
    {
        VCGMesh &mesh = doc.mesh(index).mesh;
        sourceBox = mesh.bbox;
        const vcg::Point3f centre = mesh.bbox.Center();
        for (VCGVertex &v : mesh.vert)
            v.N() = (v.cP() - centre).Normalize();
    }

    MeshFilterParameterValues params;
    params.insert(QStringLiteral("smAngle"), 20.0);
    params.insert(QStringLiteral("smRadius"), 30.0);
    params.insert(QStringLiteral("smDistance"), 0.375);
    const MeshFilterRunResult result = doc.runFilter(poissonKey, params);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QCOMPARE(result.newMeshIndices.size(), 1);

    const VCGMesh &surface = doc.mesh(result.newMeshIndices.front()).mesh;
    QVERIFY(surface.FN() > 0);
    QVERIFY(surface.VN() > 0);

    // Approximating: vertices are new points, but must still bracket the input sphere.
    vcg::Box3f tolerant = sourceBox;
    tolerant.Offset(sourceBox.Diag() * 0.5f);
    for (const VCGVertex &v : surface.vert)
        QVERIFY2(tolerant.IsIn(v.cP()), "poisson vertex far outside the input bbox");
}

// Kinetic reconstruction is piecewise planar by construction, so a sphere is the wrong
// test — a subdivided box is the fair one: six large planar regions with clean normals.
void FilterTests::kineticReconstructsABox()
{
    Document doc;

    QString boxKey;
    QString subdivideKey;
    QString kineticKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("create_box"))
            boxKey = info.key;
        else if (info.descriptor.id == QStringLiteral("meshing_surface_subdivision_midpoint"))
            subdivideKey = info.key;
        else if (info.descriptor.id == QStringLiteral("generate_kinetic_reconstruction"))
            kineticKey = info.key;
    }
    QVERIFY(!boxKey.isEmpty());
    QVERIFY(!subdivideKey.isEmpty());
    if (kineticKey.isEmpty())
        QSKIP("CGAL plugin is not available in this build.");

    QVERIFY2(doc.runFilter(boxKey, {}).success, "create_box failed");
    const int index = doc.currentMeshIndex();

    // Densify so each face carries enough samples to be detected as a planar region.
    for (int i = 0; i < 4; ++i) {
        MeshFilterParameterValues sub;
        sub.insert(QStringLiteral("Iterations"), 1);
        if (!doc.runFilter(subdivideKey, sub).success)
            break;
    }
    VCGMesh &source = doc.mesh(index).mesh;
    QVERIFY2(source.VN() > 300, "box not dense enough to detect planes");
    const vcg::Box3f sourceBox = source.bbox;
    vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalized(source);

    MeshFilterParameterValues params;
    params.insert(QStringLiteral("maximumDistance"), double(sourceBox.Diag()) * 0.02);
    params.insert(QStringLiteral("maximumAngle"), 15.0);
    params.insert(QStringLiteral("lambda"), 0.5);
    params.insert(QStringLiteral("minimumRegionSize"), 20);
    params.insert(QStringLiteral("kNeighbors"), 12);
    params.insert(QStringLiteral("intersections"), 1);
    const MeshFilterRunResult result = doc.runFilter(kineticKey, params);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QCOMPARE(result.newMeshIndices.size(), 1);

    const VCGMesh &surface = doc.mesh(result.newMeshIndices.front()).mesh;
    QVERIFY(surface.FN() > 0);
    QVERIFY(surface.VN() > 0);

    vcg::Box3f tolerant = sourceBox;
    tolerant.Offset(sourceBox.Diag() * 0.5f);
    for (const VCGVertex &v : surface.vert)
        QVERIFY2(tolerant.IsIn(v.cP()), "kinetic vertex far outside the input bbox");
}

// Alignment filters must recover a transform we applied ourselves. A sphere is displaced
// and rotated, then each filter is asked to put it back; success is measured by how close
// the realigned world-space points land on the original, relative to the model size.
void FilterTests::trueFormAlignmentRecoversAKnownTransform()
{
    Document doc;

    QString sphereKey, obbKey, icpKey, correspondingKey;
    for (const auto &info : doc.filterInfos()) {
        const QString id = info.descriptor.id;
        if (id == QStringLiteral("create_sphere")) sphereKey = info.key;
        else if (id == QStringLiteral("compute_matrix_by_obb_alignment")) obbKey = info.key;
        else if (id == QStringLiteral("compute_matrix_by_icp_trueform")) icpKey = info.key;
        else if (id == QStringLiteral("compute_matrix_by_corresponding_points")) correspondingKey = info.key;
    }
    QVERIFY(!sphereKey.isEmpty());
    if (icpKey.isEmpty())
        QSKIP("TrueForm filter plugin is not available in this build.");
    QVERIFY(!obbKey.isEmpty());
    QVERIFY(!correspondingKey.isEmpty());

    QVERIFY2(doc.runFilter(sphereKey, {}).success, "create_sphere failed");
    const int referenceIndex = doc.currentMeshIndex();
    const VCGMesh &reference = doc.mesh(referenceIndex).mesh;
    const float diagonal = reference.bbox.Diag();
    QVERIFY(diagonal > 0.0f);

    // A second copy of the same sphere, displaced. Vertex order is identical, which is
    // what makes the corresponding-points filter applicable too.
    const int sourceIndex = doc.addMesh(reference, QStringLiteral("moved"));
    QVERIFY(sourceIndex >= 0);
    QMatrix4x4 applied;
    applied.translate(0.35f * diagonal, -0.2f * diagonal, 0.1f * diagonal);
    applied.rotate(25.0f, 0.3f, 1.0f, 0.2f);
    doc.setMeshTransform(sourceIndex, applied);

    // Largest world-space gap between correspondingly-indexed vertices.
    const auto maxDeviation = [&doc, sourceIndex, referenceIndex]() {
        const auto &src = doc.mesh(sourceIndex);
        const auto &ref = doc.mesh(referenceIndex);
        float worst = 0.0f;
        const int n = std::min(src.mesh.VN(), ref.mesh.VN());
        for (int i = 0; i < n; ++i) {
            const vcg::Point3f &a = src.mesh.vert[i].cP();
            const vcg::Point3f &b = ref.mesh.vert[i].cP();
            const QVector3D pa = src.transform.map(QVector3D(a.X(), a.Y(), a.Z()));
            const QVector3D pb = ref.transform.map(QVector3D(b.X(), b.Y(), b.Z()));
            worst = std::max(worst, (pa - pb).length());
        }
        return worst;
    };
    QVERIFY2(maxDeviation() > 0.1f * diagonal, "the displacement should start far off");

    MeshFilterParameterValues shared;
    shared.insert(QStringLiteral("sourceMesh"), sourceIndex);
    shared.insert(QStringLiteral("referenceMesh"), referenceIndex);

    // Exact correspondences: this must land essentially on top of the original.
    MeshFilterParameterValues corresponding = shared;
    corresponding.insert(QStringLiteral("allowScale"), false);
    QVERIFY2(doc.runFilter(correspondingKey, corresponding).success, "corresponding-points failed");
    QVERIFY2(maxDeviation() < 1e-3f * diagonal,
             qPrintable(QStringLiteral("corresponding points left %1").arg(maxDeviation())));

    // Uniform scale is the capability ICP cannot offer; on identical shapes it must
    // resolve to a scale of 1 and stay put.
    corresponding.insert(QStringLiteral("allowScale"), true);
    QVERIFY2(doc.runFilter(correspondingKey, corresponding).success, "similarity fit failed");
    QVERIFY2(maxDeviation() < 1e-3f * diagonal,
             qPrintable(QStringLiteral("similarity left %1").arg(maxDeviation())));

    // Displace again and let ICP find its own correspondences.
    doc.setMeshTransform(sourceIndex, applied);
    QVERIFY(maxDeviation() > 0.1f * diagonal);
    MeshFilterParameterValues icp = shared;
    icp.insert(QStringLiteral("metric"), QStringLiteral("point_to_point"));
    icp.insert(QStringLiteral("coarseInit"), true);
    icp.insert(QStringLiteral("maxIterations"), 60);
    icp.insert(QStringLiteral("samples"), 0);
    QVERIFY2(doc.runFilter(icpKey, icp).success, "icp failed");
    // A sphere is rotationally symmetric, so ICP can only be asked to recover position.
    const auto centreGap = [&doc, sourceIndex, referenceIndex]() {
        const auto &src = doc.mesh(sourceIndex);
        const auto &ref = doc.mesh(referenceIndex);
        const vcg::Point3f a = src.mesh.bbox.Center();
        const vcg::Point3f b = ref.mesh.bbox.Center();
        return (src.transform.map(QVector3D(a.X(), a.Y(), a.Z()))
                - ref.transform.map(QVector3D(b.X(), b.Y(), b.Z()))).length();
    };
    QVERIFY2(centreGap() < 0.05f * diagonal,
             qPrintable(QStringLiteral("icp left the centres %1 apart").arg(centreGap())));

    // The coarse filter on its own should also bring the centres together.
    doc.setMeshTransform(sourceIndex, applied);
    QVERIFY2(doc.runFilter(obbKey, shared).success, "obb alignment failed");
    QVERIFY2(centreGap() < 0.05f * diagonal,
             qPrintable(QStringLiteral("obb left the centres %1 apart").arg(centreGap())));
}

// Two unit boxes overlapping in half their extent. The boolean results have volumes we
// can state exactly, which checks the operations themselves rather than merely that they
// produced some geometry.
void FilterTests::trueFormBooleansAgreeWithVolume()
{
    Document doc;

    QString boxKey, unionKey, interKey, diffKey, xorKey, shellKey;
    for (const auto &info : doc.filterInfos()) {
        const QString id = info.descriptor.id;
        if (id == QStringLiteral("create_box")) boxKey = info.key;
        else if (id == QStringLiteral("generate_boolean_union_trueform")) unionKey = info.key;
        else if (id == QStringLiteral("generate_boolean_intersection_trueform")) interKey = info.key;
        else if (id == QStringLiteral("generate_boolean_difference_trueform")) diffKey = info.key;
        else if (id == QStringLiteral("generate_boolean_xor_trueform")) xorKey = info.key;
        else if (id == QStringLiteral("generate_outer_shell")) shellKey = info.key;
    }
    QVERIFY(!boxKey.isEmpty());
    if (unionKey.isEmpty())
        QSKIP("TrueForm filter plugin is not available in this build.");

    QVERIFY2(doc.runFilter(boxKey, {}).success, "create_box failed");
    const int a = doc.currentMeshIndex();
    const float side = doc.mesh(a).mesh.bbox.DimX();
    QVERIFY(side > 0.0f);
    const double unit = double(side) * double(side) * double(side);

    // Second box, displaced half a side along X: overlap is exactly half of each.
    const int b = doc.addMesh(doc.mesh(a).mesh, QStringLiteral("shifted"));
    QVERIFY(b >= 0);
    QMatrix4x4 shift;
    shift.translate(side * 0.5f, 0.0f, 0.0f);
    doc.setMeshTransform(b, shift);

    const auto volumeOf = [&doc](int index) {
        return double(std::abs(vcg::tri::Stat<VCGMesh>::ComputeMeshVolume(doc.mesh(index).mesh)));
    };

    MeshFilterParameterValues p;
    p.insert(QStringLiteral("firstMesh"), a);
    p.insert(QStringLiteral("secondMesh"), b);

    struct Case { const QString *key; double expected; const char *name; };
    const Case cases[] = {
        { &unionKey,  1.5 * unit, "union" },
        { &interKey,  0.5 * unit, "intersection" },
        { &diffKey,   0.5 * unit, "difference" },
        { &xorKey,    1.0 * unit, "symmetric difference" },
    };
    for (const Case &c : cases) {
        const MeshFilterRunResult r = doc.runFilter(*c.key, p);
        QVERIFY2(r.success, qPrintable(QStringLiteral("%1: %2").arg(c.name, r.errorMessage)));
        QCOMPARE(r.newMeshIndices.size(), 1);
        const double got = volumeOf(r.newMeshIndices.front());
        QVERIFY2(std::abs(got - c.expected) < 0.02 * unit,
                 qPrintable(QStringLiteral("%1: volume %2, expected %3")
                                .arg(c.name).arg(got).arg(c.expected)));
    }

    // The outer shell of a single box is the box itself.
    MeshFilterParameterValues shellParams;
    shellParams.insert(QStringLiteral("sourceMesh"), a);
    const MeshFilterRunResult shell = doc.runFilter(shellKey, shellParams);
    QVERIFY2(shell.success, qPrintable(shell.errorMessage));
    QVERIFY(std::abs(volumeOf(shell.newMeshIndices.front()) - unit) < 0.02 * unit);
}

// The CSG evaluator and the pairwise booleans must agree where they overlap, and the
// expression must also handle a three-operand case that no single boolean can express.
void FilterTests::trueFormCsgExpressionMatchesPairwiseBooleans()
{
    Document doc;

    QString boxKey, csgKey, unionKey;
    for (const auto &info : doc.filterInfos()) {
        const QString id = info.descriptor.id;
        if (id == QStringLiteral("create_box")) boxKey = info.key;
        else if (id == QStringLiteral("generate_csg_expression")) csgKey = info.key;
        else if (id == QStringLiteral("generate_boolean_union_trueform")) unionKey = info.key;
    }
    QVERIFY(!boxKey.isEmpty());
    if (csgKey.isEmpty())
        QSKIP("TrueForm filter plugin is not available in this build.");

    QVERIFY2(doc.runFilter(boxKey, {}).success, "create_box failed");
    const int a = doc.currentMeshIndex();
    const float side = doc.mesh(a).mesh.bbox.DimX();
    const double unit = double(side) * double(side) * double(side);

    const int b = doc.addMesh(doc.mesh(a).mesh, QStringLiteral("b"));
    QMatrix4x4 shiftB;
    shiftB.translate(side * 0.5f, 0.0f, 0.0f);
    doc.setMeshTransform(b, shiftB);

    const auto volumeOf = [&doc](int index) {
        return double(std::abs(vcg::tri::Stat<VCGMesh>::ComputeMeshVolume(doc.mesh(index).mesh)));
    };

    // "0 | 1" must reproduce the pairwise union exactly.
    MeshFilterParameterValues pairwise;
    pairwise.insert(QStringLiteral("firstMesh"), a);
    pairwise.insert(QStringLiteral("secondMesh"), b);
    const MeshFilterRunResult viaBoolean = doc.runFilter(unionKey, pairwise);
    QVERIFY2(viaBoolean.success, qPrintable(viaBoolean.errorMessage));

    MeshFilterParameterValues expr;
    expr.insert(QStringLiteral("expression"), QStringLiteral("%1 | %2").arg(a).arg(b));
    const MeshFilterRunResult viaCsg = doc.runFilter(csgKey, expr);
    QVERIFY2(viaCsg.success, qPrintable(viaCsg.errorMessage));
    QVERIFY2(std::abs(volumeOf(viaBoolean.newMeshIndices.front())
                      - volumeOf(viaCsg.newMeshIndices.front())) < 0.02 * unit,
             "CSG union disagrees with the pairwise union");

    // Three operands in one arrangement: (0 | 1) - 2, which no single boolean expresses.
    const int c = doc.addMesh(doc.mesh(a).mesh, QStringLiteral("c"));
    QMatrix4x4 shiftC;
    shiftC.translate(side * 1.25f, 0.0f, 0.0f);
    doc.setMeshTransform(c, shiftC);

    MeshFilterParameterValues three;
    three.insert(QStringLiteral("expression"), QStringLiteral("(%1 | %2) - %3").arg(a).arg(b).arg(c));
    const MeshFilterRunResult combined = doc.runFilter(csgKey, three);
    QVERIFY2(combined.success, qPrintable(combined.errorMessage));
    // union is 1.5 units; c removes the quarter-unit it overlaps with b's far end.
    const double got = volumeOf(combined.newMeshIndices.front());
    QVERIFY2(std::abs(got - 1.25 * unit) < 0.05 * unit,
             qPrintable(QStringLiteral("(A|B)-C volume %1, expected %2").arg(got).arg(1.25 * unit)));

    // A malformed expression must be reported, not silently ignored.
    MeshFilterParameterValues bad;
    bad.insert(QStringLiteral("expression"), QStringLiteral("0 | "));
    QVERIFY(!doc.runFilter(csgKey, bad).success);
    bad.insert(QStringLiteral("expression"), QStringLiteral("0 | 999"));
    QVERIFY(!doc.runFilter(csgKey, bad).success);
}

// The curve filters and the sweep that consumes their output. Each case is chosen so the
// expected answer is known: two overlapping boxes cross in a closed loop, a sphere's
// height field contours into rings, and a clean mesh self-intersects nowhere.
void FilterTests::trueFormCurveFamilyProducesPolylines()
{
    Document doc;

    QString boxKey, sphereKey, interKey, selfKey, isoKey, tubeKey, borderKey;
    for (const auto &info : doc.filterInfos()) {
        const QString id = info.descriptor.id;
        if (id == QStringLiteral("create_box")) boxKey = info.key;
        else if (id == QStringLiteral("create_sphere")) sphereKey = info.key;
        else if (id == QStringLiteral("generate_polyline_from_mesh_intersection")) interKey = info.key;
        else if (id == QStringLiteral("generate_polyline_from_self_intersections")) selfKey = info.key;
        else if (id == QStringLiteral("generate_polyline_from_scalar_isocontour")) isoKey = info.key;
        else if (id == QStringLiteral("generate_tube_from_polyline")) tubeKey = info.key;
        else if (id == QStringLiteral("compute_scalar_by_border_distance_per_vertex")) borderKey = info.key;
    }
    QVERIFY(!boxKey.isEmpty());
    if (interKey.isEmpty())
        QSKIP("TrueForm filter plugin is not available in this build.");

    QVERIFY2(doc.runFilter(boxKey, {}).success, "create_box failed");
    const int a = doc.currentMeshIndex();
    const float side = doc.mesh(a).mesh.bbox.DimX();
    const int b = doc.addMesh(doc.mesh(a).mesh, QStringLiteral("shifted"));
    QMatrix4x4 shift;
    shift.translate(side * 0.5f, 0.0f, 0.0f);
    doc.setMeshTransform(b, shift);

    // Two overlapping boxes intersect along a closed loop of edges.
    MeshFilterParameterValues pair;
    pair.insert(QStringLiteral("firstMesh"), a);
    pair.insert(QStringLiteral("secondMesh"), b);
    const MeshFilterRunResult crossing = doc.runFilter(interKey, pair);
    QVERIFY2(crossing.success, qPrintable(crossing.errorMessage));
    QCOMPARE(crossing.newMeshIndices.size(), 1);
    const int curveIndex = crossing.newMeshIndices.front();
    QVERIFY2(doc.mesh(curveIndex).mesh.EN() > 0, "intersection curve has no edges");
    QCOMPARE(doc.mesh(curveIndex).mesh.FN(), 0); // a polyline, not a surface

    // Sweeping that curve must give a solid.
    MeshFilterParameterValues tube;
    tube.insert(QStringLiteral("sourceMesh"), curveIndex);
    tube.insert(QStringLiteral("radius"), double(side) * 0.02);
    tube.insert(QStringLiteral("segments"), 8);
    const MeshFilterRunResult swept = doc.runFilter(tubeKey, tube);
    QVERIFY2(swept.success, qPrintable(swept.errorMessage));
    QVERIFY(doc.mesh(swept.newMeshIndices.front()).mesh.FN() > 0);

    // Sweeping something that is not a polyline must be refused, not crash.
    MeshFilterParameterValues badTube;
    badTube.insert(QStringLiteral("sourceMesh"), a);
    badTube.insert(QStringLiteral("radius"), double(side) * 0.02);
    QVERIFY(!doc.runFilter(tubeKey, badTube).success);

    // A single clean box does not intersect itself.
    MeshFilterParameterValues single;
    single.insert(QStringLiteral("sourceMesh"), a);
    QVERIFY2(!doc.runFilter(selfKey, single).success,
             "a clean box should report no self-intersections");

    // Contours of a scalar field: a border-distance field on an open surface.
    if (!sphereKey.isEmpty() && !borderKey.isEmpty()) {
        QVERIFY2(doc.runFilter(sphereKey, {}).success, "create_sphere failed");
        const int sphere = doc.currentMeshIndex();

        // Without a scalar field the filter must explain itself rather than fail blankly.
        MeshFilterParameterValues iso;
        iso.insert(QStringLiteral("sourceMesh"), sphere);
        iso.insert(QStringLiteral("contourCount"), 5);
        const MeshFilterRunResult constantField = doc.runFilter(isoKey, iso);
        if (!constantField.success)
            QVERIFY(constantField.errorMessage.contains(QStringLiteral("constant")));
    }
}

// Contract check across every filter declaring outputDomain == NewMeshes.
//
// FilterCreationTests already enforces this, but only for pure generators
// (inputDomain == None). Every NewMeshes filter that takes input — the booleans, the
// reconstructions, the polyline family, the samplers — sits outside that harness, which
// is where a filter can create a layer and forget to report it. The symptom lives
// entirely in the return value, so the document looks correct and only a caller that
// asks "which layer did you just make?" notices.
//
// Filters that cannot run on this input are skipped rather than failed: the assertion is
// conditional — *if* you succeeded and you declared NewMeshes, you must report the layers.
void FilterTests::newMeshFiltersReportTheirLayers()
{
    // A document with enough variety that most filters have something to chew on: a
    // scalar field, two overlapping solids for the binary operators, and a polyline.
    const auto buildInputs = [](Document &doc) -> bool {
        QString sphereKey, boxKey, borderKey, sectionKey;
        for (const auto &info : doc.filterInfos()) {
            const QString id = info.descriptor.id;
            if (id == QStringLiteral("create_sphere")) sphereKey = info.key;
            else if (id == QStringLiteral("create_box")) boxKey = info.key;
            else if (id == QStringLiteral("compute_scalar_by_border_distance_per_vertex")) borderKey = info.key;
            else if (id == QStringLiteral("generate_polyline_from_planar_section")) sectionKey = info.key;
        }
        if (boxKey.isEmpty() || sphereKey.isEmpty())
            return false;

        if (!doc.runFilter(boxKey, {}).success)
            return false;
        const int box = doc.currentMeshIndex();
        const float side = doc.mesh(box).mesh.bbox.DimX();

        // A second solid, overlapping, so binary operators have a meaningful pair.
        const int shifted = doc.addMesh(doc.mesh(box).mesh, QStringLiteral("shifted box"));
        if (shifted < 0)
            return false;
        QMatrix4x4 shift;
        shift.translate(side * 0.5f, 0.0f, 0.0f);
        doc.setMeshTransform(shifted, shift);

        // Normals and a scalar field on the current layer.
        if (!sectionKey.isEmpty())
            doc.runFilter(sectionKey, {});
        if (!borderKey.isEmpty())
            doc.runFilter(borderKey, {});
        doc.setCurrentMeshIndex(box);
        return true;
    };

    Document probe;
    QStringList offenders;
    QStringList ran;
    int skipped = 0;

    for (const auto &info : probe.filterInfos()) {
        if (info.descriptor.outputDomain != MeshFilterOutputDomain::NewMeshes)
            continue;
        // Two explicit exclusions, both with reasons rather than convenience:
        //  - quadwild shells out to an external binary that may not be installed, and is
        //    far too slow for a contract sweep;
        //  - generate_voronoi_atlas_parametrization *aborts* (vcglib rect_packer.h
        //    assertion) rather than returning an error when its parametrization
        //    degenerates on simple synthetic input, which would take the whole suite
        //    down with SIGABRT. That is a separate bug in filter_texture.
        if (info.descriptor.id.contains(QStringLiteral("quadwild"))
            || info.descriptor.id == QStringLiteral("generate_voronoi_atlas_parametrization"))
            continue;

        // Four filters whose default parameters are sized for real meshes rather than
        // for the input, so they cost the same on a box as on a scan: measured at 60 s,
        // 31 s, 22 s and 12 s respectively, together four fifths of the sweep. The
        // contract being checked here is about the return value and does not depend on
        // the algorithm running at full resolution.
        static const QSet<QString> kTooSlowForASweep = {
            QStringLiteral("remesh_to_quads_instant_meshes"),
            QStringLiteral("generate_marching_cubes_rimls"),
            QStringLiteral("generate_marching_cubes_apss"),
            QStringLiteral("generate_surface_reconstruction_vcg"),
        };
        if (kTooSlowForASweep.contains(info.descriptor.id))
            continue;

        Document doc;
        if (!buildInputs(doc))
            QSKIP("Could not build the standard inputs for this build.");

        const int before = doc.meshCount();
        const MeshFilterRunResult result = doc.runFilter(info.key, {});
        if (!result.success) {
            ++skipped; // cannot run on this input; not what this test is about
            continue;
        }
        ran << info.descriptor.name;

        if (result.newMeshIndices.isEmpty()) {
            offenders << QStringLiteral("%1 (%2): succeeded but reported no new layer")
                             .arg(info.descriptor.name, info.descriptor.id);
            continue;
        }
        for (int index : result.newMeshIndices) {
            if (index < 0 || index >= doc.meshCount()) {
                offenders << QStringLiteral("%1 (%2): reported out-of-range layer %3")
                                 .arg(info.descriptor.name, info.descriptor.id).arg(index);
            }
        }
        // Not layer-count arithmetic: Flatten Visible Layers legitimately reports one
        // new layer while the document shrinks, because it merges the originals away.
        // What must hold is that every reported index names a real layer with content.
        for (int index : result.newMeshIndices) {
            if (index >= 0 && index < doc.meshCount() && doc.mesh(index).mesh.VN() <= 0) {
                offenders << QStringLiteral("%1 (%2): reported layer %3, which is empty")
                                 .arg(info.descriptor.name, info.descriptor.id).arg(index);
            }
        }
        (void) before;
    }

    qDebug() << "NewMeshes contract:" << ran.size() << "filter(s) exercised,"
             << skipped << "not runnable on the standard inputs";
    QVERIFY2(offenders.isEmpty(), qPrintable(offenders.join(QStringLiteral("\n"))));
    QVERIFY2(ran.size() >= 5, "too few NewMeshes filters were exercised to be meaningful");
}

// A small sphere entirely inside a big box: every vertex is inside, the signed distance
// is negative everywhere, and the distances have a known magnitude. Moving the sphere
// clear of the box must flip all three answers.
void FilterTests::trueFormDistanceAndContainment()
{
    Document doc;

    QString boxKey, sphereKey, distKey, insideKey, chamferKey;
    for (const auto &info : doc.filterInfos()) {
        const QString id = info.descriptor.id;
        if (id == QStringLiteral("create_box")) boxKey = info.key;
        else if (id == QStringLiteral("create_sphere")) sphereKey = info.key;
        else if (id == QStringLiteral("compute_scalar_by_signed_distance_per_vertex")) distKey = info.key;
        else if (id == QStringLiteral("select_vertices_inside_mesh")) insideKey = info.key;
        else if (id == QStringLiteral("compute_chamfer_distance")) chamferKey = info.key;
    }
    QVERIFY(!boxKey.isEmpty() && !sphereKey.isEmpty());
    if (distKey.isEmpty())
        QSKIP("TrueForm filter plugin is not available in this build.");

    QVERIFY2(doc.runFilter(boxKey, {}).success, "create_box failed");
    const int box = doc.currentMeshIndex();
    const float side = doc.mesh(box).mesh.bbox.DimX();

    // A sphere scaled well inside the box.
    QVERIFY2(doc.runFilter(sphereKey, {}).success, "create_sphere failed");
    const int sphere = doc.currentMeshIndex();
    QMatrix4x4 shrink;
    shrink.scale(side * 0.2f / doc.mesh(sphere).mesh.bbox.Diag());
    doc.setMeshTransform(sphere, shrink);

    MeshFilterParameterValues p;
    p.insert(QStringLiteral("sourceMesh"), sphere);
    p.insert(QStringLiteral("referenceMesh"), box);

    // Inside: every distance negative.
    QVERIFY2(doc.runFilter(distKey, p).success, "signed distance failed");
    {
        const VCGMesh &m = doc.mesh(sphere).mesh;
        int positive = 0;
        for (const VCGVertex &v : m.vert)
            if (!v.IsD() && v.cQ() >= 0.0f)
                ++positive;
        QVERIFY2(positive == 0,
                 qPrintable(QStringLiteral("%1 vertex(es) reported outside a box that "
                                           "encloses them").arg(positive)));
    }

    // ... and every vertex selected as inside.
    QVERIFY2(doc.runFilter(insideKey, p).success, "containment failed");
    {
        const VCGMesh &m = doc.mesh(sphere).mesh;
        int unselected = 0;
        for (const VCGVertex &v : m.vert)
            if (!v.IsD() && !v.IsS())
                ++unselected;
        QCOMPARE(unselected, 0);
    }

    // Move it clear of the box: the answers must invert.
    QMatrix4x4 outside;
    outside.translate(side * 5.0f, 0.0f, 0.0f);
    outside.scale(side * 0.2f / doc.mesh(sphere).mesh.bbox.Diag());
    doc.setMeshTransform(sphere, outside);

    QVERIFY2(doc.runFilter(distKey, p).success, "signed distance failed");
    {
        const VCGMesh &m = doc.mesh(sphere).mesh;
        int negative = 0;
        for (const VCGVertex &v : m.vert)
            if (!v.IsD() && v.cQ() < 0.0f)
                ++negative;
        QCOMPARE(negative, 0);
    }
    QVERIFY2(doc.runFilter(insideKey, p).success, "containment failed");
    {
        const VCGMesh &m = doc.mesh(sphere).mesh;
        int selected = 0;
        for (const VCGVertex &v : m.vert)
            if (!v.IsD() && v.IsS())
                ++selected;
        QCOMPARE(selected, 0);
    }

    // Chamfer distance to itself is zero; to the displaced copy it is not.
    MeshFilterParameterValues self;
    self.insert(QStringLiteral("sourceMesh"), box);
    self.insert(QStringLiteral("referenceMesh"), box);
    QVERIFY2(!doc.runFilter(chamferKey, self).success, "a layer against itself should be refused");

    MeshFilterParameterValues apart;
    apart.insert(QStringLiteral("sourceMesh"), sphere);
    apart.insert(QStringLiteral("referenceMesh"), box);
    apart.insert(QStringLiteral("symmetric"), true);
    const MeshFilterRunResult chamfer = doc.runFilter(chamferKey, apart);
    QVERIFY2(chamfer.success, qPrintable(chamfer.errorMessage));
    QVERIFY(!chamfer.documentModified); // a measurement must not alter the document
    QVERIFY(chamfer.infoMessages.size() >= 3);
}

// The competing per-vertex operators. Each is checked against a property that follows
// from what it claims to do, rather than merely that it ran: Laplacian smoothing shrinks
// a sphere, Taubin does not, Gaussian curvature of a sphere is positive everywhere, and
// recomputed normals on a sphere point outward.
void FilterTests::trueFormAttributeFiltersBehaveAsDocumented()
{
    Document doc;

    QString sphereKey, laplacianKey, taubinKey, curvatureKey, normalsKey;
    for (const auto &info : doc.filterInfos()) {
        const QString id = info.descriptor.id;
        if (id == QStringLiteral("create_sphere")) sphereKey = info.key;
        else if (id == QStringLiteral("apply_laplacian_smoothing_trueform")) laplacianKey = info.key;
        else if (id == QStringLiteral("apply_taubin_smoothing_trueform")) taubinKey = info.key;
        else if (id == QStringLiteral("compute_scalar_by_curvature_trueform")) curvatureKey = info.key;
        else if (id == QStringLiteral("compute_normals_trueform")) normalsKey = info.key;
    }
    QVERIFY(!sphereKey.isEmpty());
    if (laplacianKey.isEmpty())
        QSKIP("TrueForm filter plugin is not available in this build.");

    const auto meshVolume = [](const VCGMesh &m) {
        return double(std::abs(vcg::tri::Stat<VCGMesh>::ComputeMeshVolume(m)));
    };

    // Laplacian smoothing shrinks a closed surface; that is its documented drawback.
    {
        Document d;
        QVERIFY2(d.runFilter(sphereKey, {}).success, "create_sphere failed");
        const int s = d.currentMeshIndex();
        const double before = meshVolume(d.mesh(s).mesh);
        MeshFilterParameterValues p;
        p.insert(QStringLiteral("iterations"), 20);
        p.insert(QStringLiteral("lambda"), 0.5);
        QVERIFY2(d.runFilter(laplacianKey, p).success, "laplacian failed");
        const double after = meshVolume(d.mesh(s).mesh);
        QVERIFY2(after < before * 0.99,
                 qPrintable(QStringLiteral("laplacian did not shrink: %1 -> %2").arg(before).arg(after)));
    }

    // Taubin is the volume-preserving one: same iterations, far less loss.
    {
        Document d;
        QVERIFY2(d.runFilter(sphereKey, {}).success, "create_sphere failed");
        const int s = d.currentMeshIndex();
        const double before = meshVolume(d.mesh(s).mesh);
        MeshFilterParameterValues p;
        p.insert(QStringLiteral("iterations"), 20);
        p.insert(QStringLiteral("lambda"), 0.5);
        p.insert(QStringLiteral("kpb"), 0.1);
        QVERIFY2(d.runFilter(taubinKey, p).success, "taubin failed");
        const double after = meshVolume(d.mesh(s).mesh);
        QVERIFY2(after > before * 0.9,
                 qPrintable(QStringLiteral("taubin lost too much volume: %1 -> %2").arg(before).arg(after)));
    }

    // Gaussian curvature of a convex closed surface is positive everywhere.
    {
        Document d;
        QVERIFY2(d.runFilter(sphereKey, {}).success, "create_sphere failed");
        const int s = d.currentMeshIndex();
        MeshFilterParameterValues p;
        p.insert(QStringLiteral("measure"), QStringLiteral("gaussian"));
        p.insert(QStringLiteral("ring"), 2);
        QVERIFY2(d.runFilter(curvatureKey, p).success, "curvature failed");
        int nonPositive = 0;
        int total = 0;
        for (const VCGVertex &v : d.mesh(s).mesh.vert) {
            if (v.IsD())
                continue;
            ++total;
            if (!(v.cQ() > 0.0f))
                ++nonPositive;
        }
        QVERIFY(total > 0);
        // Allow a few boundary-ish estimates to misbehave, but not the bulk.
        QVERIFY2(nonPositive < total / 10,
                 qPrintable(QStringLiteral("%1 of %2 gaussian curvatures were not positive")
                                .arg(nonPositive).arg(total)));
    }

    // Recomputed vertex normals on a sphere point away from the centre.
    {
        Document d;
        QVERIFY2(d.runFilter(sphereKey, {}).success, "create_sphere failed");
        const int s = d.currentMeshIndex();
        for (VCGVertex &v : d.mesh(s).mesh.vert)
            v.N() = vcg::Point3f(0.0f, 0.0f, 0.0f); // clear so the filter must do the work
        MeshFilterParameterValues p;
        p.insert(QStringLiteral("target"), QStringLiteral("vertex"));
        QVERIFY2(d.runFilter(normalsKey, p).success, "normals failed");
        const VCGMesh &m = d.mesh(s).mesh;
        const vcg::Point3f centre = m.bbox.Center();
        int inward = 0;
        int total = 0;
        for (const VCGVertex &v : m.vert) {
            if (v.IsD())
                continue;
            ++total;
            if ((v.cP() - centre).Normalize().dot(v.cN()) <= 0.0f)
                ++inward;
        }
        QVERIFY(total > 0);
        QCOMPARE(inward, 0);
    }
}

// The three remeshing filters, each checked against the thing it promises: decimation
// hits a face count, isotropic remeshing equalises edge lengths, and simplification by
// error bound stays within its bound. All three must keep the shape recognisable.
void FilterTests::trueFormRemeshingChangesResolutionAsAsked()
{
    Document doc;

    QString sphereKey, isoKey, simplifyKey, decimateKey;
    for (const auto &info : doc.filterInfos()) {
        const QString id = info.descriptor.id;
        if (id == QStringLiteral("create_sphere")) sphereKey = info.key;
        else if (id == QStringLiteral("remeshing_isotropic_trueform")) isoKey = info.key;
        else if (id == QStringLiteral("simplification_by_error_trueform")) simplifyKey = info.key;
        else if (id == QStringLiteral("simplification_by_decimation_trueform")) decimateKey = info.key;
    }
    QVERIFY(!sphereKey.isEmpty());
    if (isoKey.isEmpty())
        QSKIP("TrueForm filter plugin is not available in this build.");

    // Decimation to a stated proportion.
    {
        Document d;
        QVERIFY2(d.runFilter(sphereKey, {}).success, "create_sphere failed");
        const int s = d.currentMeshIndex();
        const int before = d.mesh(s).mesh.FN();
        const float diagonalBefore = d.mesh(s).mesh.bbox.Diag();
        MeshFilterParameterValues p;
        p.insert(QStringLiteral("targetProportion"), 0.5);
        QVERIFY2(d.runFilter(decimateKey, p).success, "decimate failed");
        const int after = d.mesh(s).mesh.FN();
        QVERIFY2(after < before, "decimation did not reduce the face count");
        QVERIFY2(after > before / 4, "decimation overshot badly");
        // The shape must survive: the bounding box should be about the same size.
        QVERIFY(std::abs(d.mesh(s).mesh.bbox.Diag() - diagonalBefore) < 0.1f * diagonalBefore);
    }

    // Isotropic remeshing towards a target edge length: edge lengths should cluster
    // around it far more tightly than the original's did.
    {
        Document d;
        QVERIFY2(d.runFilter(sphereKey, {}).success, "create_sphere failed");
        const int s = d.currentMeshIndex();
        const float target = d.mesh(s).mesh.bbox.Diag() * 0.05f;

        MeshFilterParameterValues p;
        p.insert(QStringLiteral("targetLength"), double(target));
        p.insert(QStringLiteral("iterations"), 3);
        QVERIFY2(d.runFilter(isoKey, p).success, "isotropic remesh failed");

        const VCGMesh &m = d.mesh(s).mesh;
        QVERIFY(m.FN() > 0);
        double sum = 0.0;
        int count = 0;
        for (const VCGFace &f : m.face) {
            if (f.IsD())
                continue;
            for (int k = 0; k < 3; ++k) {
                sum += double((f.cV((k + 1) % 3)->cP() - f.cV(k)->cP()).Norm());
                ++count;
            }
        }
        QVERIFY(count > 0);
        const double mean = sum / count;
        // Within a factor of two of the request is a fair bar for a 3-iteration run.
        QVERIFY2(mean > 0.5 * double(target) && mean < 2.0 * double(target),
                 qPrintable(QStringLiteral("mean edge %1, target %2").arg(mean).arg(double(target))));
    }

    // Error-bound simplification: a loose bound must remove more than a tight one.
    {
        const auto facesAfter = [&](double errorRelative) {
            Document d;
            if (!d.runFilter(sphereKey, {}).success)
                return -1;
            MeshFilterParameterValues p;
            p.insert(QStringLiteral("errorRelative"), errorRelative);
            if (!d.runFilter(simplifyKey, p).success)
                return -1;
            return d.mesh(d.currentMeshIndex()).mesh.FN();
        };
        const int tight = facesAfter(0.0005);
        const int loose = facesAfter(0.05);
        QVERIFY2(tight > 0 && loose > 0, "error-bound simplification failed");
        QVERIFY2(loose < tight,
                 qPrintable(QStringLiteral("a looser bound kept more faces: %1 vs %2")
                                .arg(loose).arg(tight)));
    }
}

// Orientation and edge selection, each against a property that must hold afterwards:
// an inside-out box comes back with positive volume, a box has exactly twelve creases at
// ninety degrees and none at a threshold above that, and a clean mesh has no non-manifold
// edges.
void FilterTests::trueFormOrientationAndEdgeSelection()
{
    Document doc;

    QString boxKey, sphereKey, coherentKey, outwardKey, creaseKey, nonManifoldKey;
    for (const auto &info : doc.filterInfos()) {
        const QString id = info.descriptor.id;
        if (id == QStringLiteral("create_box")) boxKey = info.key;
        else if (id == QStringLiteral("create_sphere")) sphereKey = info.key;
        else if (id == QStringLiteral("orient_faces_coherently_trueform")) coherentKey = info.key;
        else if (id == QStringLiteral("orient_faces_outward_trueform")) outwardKey = info.key;
        else if (id == QStringLiteral("select_crease_edges_trueform")) creaseKey = info.key;
        else if (id == QStringLiteral("select_non_manifold_edges_trueform")) nonManifoldKey = info.key;
    }
    QVERIFY(!boxKey.isEmpty());
    if (outwardKey.isEmpty())
        QSKIP("TrueForm filter plugin is not available in this build.");

    const auto signedVolume = [](const VCGMesh &m) {
        return double(vcg::tri::Stat<VCGMesh>::ComputeMeshVolume(m));
    };
    const auto countSelectedEdges = [](const VCGMesh &m) {
        int n = 0;
        for (const VCGFace &f : m.face) {
            if (f.IsD())
                continue;
            for (int k = 0; k < 3; ++k)
                if (f.IsFaceEdgeS(k))
                    ++n;
        }
        return n;
    };

    // An inside-out box must come back with positive volume.
    {
        Document d;
        QVERIFY2(d.runFilter(boxKey, {}).success, "create_box failed");
        const int s = d.currentMeshIndex();
        // Invert every face so the solid is wound inwards.
        for (VCGFace &f : d.mesh(s).mesh.face) {
            if (!f.IsD())
                std::swap(f.V(1), f.V(2));
        }
        QVERIFY2(signedVolume(d.mesh(s).mesh) < 0.0, "the box should start inside out");
        QVERIFY2(d.runFilter(outwardKey, {}).success, "orient outward failed");
        QVERIFY2(signedVolume(d.mesh(s).mesh) > 0.0,
                 "orienting outward left the box inside out");
    }

    // Coherent orientation: measured as winding consistency itself rather than through
    // the volume. A consistently wound surface traverses every interior edge once in each
    // direction, so a directed edge seen twice the same way is an inconsistency.
    {
        Document d;
        QVERIFY2(d.runFilter(boxKey, {}).success, "create_box failed");
        const int s = d.currentMeshIndex();

        const auto inconsistentEdges = [](const VCGMesh &m) {
            const VCGVertex *base = m.vert.empty() ? nullptr : &m.vert.front();
            std::map<std::pair<std::size_t, std::size_t>, int> directed;
            for (const VCGFace &f : m.face) {
                if (f.IsD() || !base)
                    continue;
                for (int k = 0; k < 3; ++k) {
                    const auto a = std::size_t(f.cV(k) - base);
                    const auto b = std::size_t(f.cV((k + 1) % 3) - base);
                    ++directed[{ a, b }];
                }
            }
            int bad = 0;
            for (const auto &[edge, count] : directed) {
                if (count > 1)
                    bad += count - 1; // the same direction traversed more than once
            }
            return bad;
        };

        QCOMPARE(inconsistentEdges(d.mesh(s).mesh), 0); // the box starts clean
        int i = 0;
        for (VCGFace &f : d.mesh(s).mesh.face) {
            if (!f.IsD() && (i++ % 2 == 0))
                std::swap(f.V(1), f.V(2)); // scramble half the windings
        }
        const int before = inconsistentEdges(d.mesh(s).mesh);
        QVERIFY2(before > 0, "scrambling should have produced inconsistencies");

        QVERIFY2(d.runFilter(coherentKey, {}).success, "orient coherently failed");
        const int after = inconsistentEdges(d.mesh(s).mesh);

        // Zero, because the filter iterates to a fixed point: a single call to
        // tf::orient_faces_consistently only partly repairs a badly mixed winding (it
        // indexes an edge link built from the pre-flip winding while reversing faces in
        // place), but each call rebuilds that link, so repeating converges. On this box
        // the sequence is 14 -> 8 -> 3 -> 0.
        QVERIFY2(after == 0,
                 qPrintable(QStringLiteral("%1 inconsistent edge(s) remain, from %2")
                                .arg(after).arg(before)));
    }

    // A box has twelve ninety-degree creases, and none above ninety.
    {
        Document d;
        QVERIFY2(d.runFilter(boxKey, {}).success, "create_box failed");
        const int s = d.currentMeshIndex();

        MeshFilterParameterValues p;
        p.insert(QStringLiteral("angle"), 60.0);
        QVERIFY2(d.runFilter(creaseKey, p).success, "crease selection failed");
        // Each of the 12 box edges is shared by two faces, so 24 face-edge marks.
        QCOMPARE(countSelectedEdges(d.mesh(s).mesh), 24);

        p.insert(QStringLiteral("angle"), 120.0);
        QVERIFY2(d.runFilter(creaseKey, p).success, "crease selection failed");
        QCOMPARE(countSelectedEdges(d.mesh(s).mesh), 0);
    }

    // A clean sphere has no non-manifold edges.
    if (!sphereKey.isEmpty()) {
        Document d;
        QVERIFY2(d.runFilter(sphereKey, {}).success, "create_sphere failed");
        const int s = d.currentMeshIndex();
        QVERIFY2(d.runFilter(nonManifoldKey, {}).success, "non-manifold selection failed");
        QCOMPARE(countSelectedEdges(d.mesh(s).mesh), 0);
    }
}

// The last three: welding a soup restores connectivity, resolving self-intersections
// splits the crossing faces, and cutting along contours adds geometry without changing
// the surface's extent.
void FilterTests::trueFormRepairAndIsobands()
{
    Document doc;

    QString boxKey, sphereKey, cleanKey, resolveKey, cutKey, borderKey, unweldKey;
    for (const auto &info : doc.filterInfos()) {
        const QString id = info.descriptor.id;
        if (id == QStringLiteral("create_box")) boxKey = info.key;
        else if (id == QStringLiteral("create_sphere")) sphereKey = info.key;
        else if (id == QStringLiteral("remove_duplicate_vertices_trueform")) cleanKey = info.key;
        else if (id == QStringLiteral("repair_self_intersections")) resolveKey = info.key;
        else if (id == QStringLiteral("cut_along_scalar_isocontour")) cutKey = info.key;
        else if (id == QStringLiteral("compute_scalar_by_border_distance_per_vertex")) borderKey = info.key;
        else if (id == QStringLiteral("meshing_vertex_unreferenced_split")) unweldKey = info.key;
    }
    QVERIFY(!boxKey.isEmpty());
    if (cleanKey.isEmpty())
        QSKIP("TrueForm filter plugin is not available in this build.");

    // Welding: split every face into its own vertices, then weld them back.
    {
        Document d;
        QVERIFY2(d.runFilter(boxKey, {}).success, "create_box failed");
        const int s = d.currentMeshIndex();
        const int weldedV = d.mesh(s).mesh.VN();

        // Unweld by hand: give each face its own copy of its three vertices.
        {
            VCGMesh soup;
            for (const VCGFace &f : d.mesh(s).mesh.face) {
                if (f.IsD())
                    continue;
                const int base = soup.VN();
                vcg::tri::Allocator<VCGMesh>::AddVertices(soup, 3);
                for (int k = 0; k < 3; ++k)
                    soup.vert[std::size_t(base + k)].P() = f.cV(k)->cP();
                vcg::tri::Allocator<VCGMesh>::AddFace(soup, base, base + 1, base + 2);
            }
            vcg::tri::UpdateBounding<VCGMesh>::Box(soup);
            const int soupIndex = d.addMesh(soup, QStringLiteral("soup"));
            QVERIFY(soupIndex >= 0);
            d.setCurrentMeshIndex(soupIndex);
            QVERIFY2(d.mesh(soupIndex).mesh.VN() > weldedV, "the soup should have more vertices");

            QVERIFY2(d.runFilter(cleanKey, {}).success, "clean failed");
            QCOMPARE(d.mesh(soupIndex).mesh.VN(), weldedV);
            QCOMPARE(d.mesh(soupIndex).mesh.FN(), 12);
        }
    }

    // Resolving self-intersections: two boxes merged into one layer cross each other, so
    // the arrangement must split faces and produce more of them than it started with.
    {
        Document d;
        QVERIFY2(d.runFilter(boxKey, {}).success, "create_box failed");
        const int a = d.currentMeshIndex();
        const float side = d.mesh(a).mesh.bbox.DimX();

        VCGMesh crossing;
        vcg::tri::Append<VCGMesh, VCGMesh>::MeshCopy(crossing, d.mesh(a).mesh);
        const int base = crossing.VN();
        vcg::tri::Append<VCGMesh, VCGMesh>::Mesh(crossing, d.mesh(a).mesh);
        for (int i = base; i < crossing.VN(); ++i)
            crossing.vert[std::size_t(i)].P().X() += side * 0.5f;
        vcg::tri::UpdateBounding<VCGMesh>::Box(crossing);
        const int crossIndex = d.addMesh(crossing, QStringLiteral("crossing"));
        QVERIFY(crossIndex >= 0);
        const int before = d.mesh(crossIndex).mesh.FN();

        MeshFilterParameterValues p;
        p.insert(QStringLiteral("sourceMesh"), crossIndex);
        const MeshFilterRunResult r = d.runFilter(resolveKey, p);
        QVERIFY2(r.success, qPrintable(r.errorMessage));
        QCOMPARE(r.newMeshIndices.size(), 1);
        QVERIFY2(d.mesh(r.newMeshIndices.front()).mesh.FN() > before,
                 "resolving should have split the crossing faces");
    }

    // Cutting along contours adds geometry but must not move the surface.
    if (!sphereKey.isEmpty() && !borderKey.isEmpty()) {
        Document d;
        QVERIFY2(d.runFilter(sphereKey, {}).success, "create_sphere failed");
        const int s = d.currentMeshIndex();
        // Give it a non-constant scalar: height along Y.
        const vcg::Point3f centre = d.mesh(s).mesh.bbox.Center();
        for (VCGVertex &v : d.mesh(s).mesh.vert)
            v.Q() = v.cP().Y() - centre.Y();
        const vcg::Box3f before = d.mesh(s).mesh.bbox;
        const int beforeF = d.mesh(s).mesh.FN();

        MeshFilterParameterValues p;
        p.insert(QStringLiteral("sourceMesh"), s);
        p.insert(QStringLiteral("contourCount"), 4);
        const MeshFilterRunResult r = d.runFilter(cutKey, p);
        QVERIFY2(r.success, qPrintable(r.errorMessage));
        const VCGMesh &cut = d.mesh(r.newMeshIndices.front()).mesh;
        QVERIFY2(cut.FN() > beforeF, "cutting should have added faces");
        // Same surface, so the same extent.
        QVERIFY(std::abs(cut.bbox.DimX() - before.DimX()) < 0.02f * before.DimX());
        QVERIFY(std::abs(cut.bbox.DimY() - before.DimY()) < 0.02f * before.DimY());
    }
}

// Improving a triangulation must raise triangle quality while keeping the vertex count
// and the surface. A sphere whose vertices have been jittered gives it something to fix.
void FilterTests::trueFormImproveTriangulationRaisesQuality()
{
    Document doc;

    QString sphereKey, improveKey;
    for (const auto &info : doc.filterInfos()) {
        const QString id = info.descriptor.id;
        if (id == QStringLiteral("create_sphere")) sphereKey = info.key;
        else if (id == QStringLiteral("improve_triangulation_trueform")) improveKey = info.key;
    }
    QVERIFY(!sphereKey.isEmpty());
    if (improveKey.isEmpty())
        QSKIP("TrueForm filter plugin is not available in this build.");

    QVERIFY2(doc.runFilter(sphereKey, {}).success, "create_sphere failed");
    const int s = doc.currentMeshIndex();

    // Smallest angle over the whole mesh, in radians: the quantity the min-angle
    // objective is supposed to raise.
    const auto worstAngle = [](const VCGMesh &m) {
        double worst = 3.15;
        for (const VCGFace &f : m.face) {
            if (f.IsD())
                continue;
            for (int k = 0; k < 3; ++k) {
                const vcg::Point3f a = f.cV((k + 1) % 3)->cP() - f.cV(k)->cP();
                const vcg::Point3f b = f.cV((k + 2) % 3)->cP() - f.cV(k)->cP();
                const double na = double(a.Norm());
                const double nb = double(b.Norm());
                if (na < 1e-12 || nb < 1e-12)
                    return 0.0;
                const double cosine = std::clamp(double(a.dot(b)) / (na * nb), -1.0, 1.0);
                worst = std::min(worst, std::acos(cosine));
            }
        }
        return worst;
    };

    // Jitter the vertices tangentially so the triangulation degrades but the shape does not.
    const vcg::Point3f centre = doc.mesh(s).mesh.bbox.Center();
    const float radius = doc.mesh(s).mesh.bbox.Diag() * 0.5f;
    int i = 0;
    for (VCGVertex &v : doc.mesh(s).mesh.vert) {
        if (v.IsD())
            continue;
        const float wobble = 0.12f * radius * ((i % 3) - 1);
        v.P() = v.cP() + vcg::Point3f(wobble, -wobble, wobble * 0.5f);
        ++i;
    }
    vcg::tri::UpdateBounding<VCGMesh>::Box(doc.mesh(s).mesh);

    const int beforeV = doc.mesh(s).mesh.VN();
    const int beforeF = doc.mesh(s).mesh.FN();
    const double before = worstAngle(doc.mesh(s).mesh);

    MeshFilterParameterValues p;
    p.insert(QStringLiteral("objective"), QStringLiteral("min_angle"));
    p.insert(QStringLiteral("iterations"), 5);
    p.insert(QStringLiteral("relaxationIterations"), 3);
    QVERIFY2(doc.runFilter(improveKey, p).success, "improve failed");

    const VCGMesh &after = doc.mesh(s).mesh;
    // Refines rather than rebuilds: the counts must not change.
    QCOMPARE(after.VN(), beforeV);
    QCOMPARE(after.FN(), beforeF);
    QVERIFY2(worstAngle(after) > before,
             qPrintable(QStringLiteral("worst angle %1 -> %2, no improvement")
                            .arg(before).arg(worstAngle(after))));
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

namespace {

QString filterKeyForId(const Document &doc, const QString &filterId)
{
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == filterId)
            return info.key;
    }
    return {};
}

} // namespace

// A convex closed surface cannot occlude itself: every ray leaving a face's outward
// hemisphere escapes, so the ambient occlusion value is the same for every face
// regardless of how far the mesh sits from the origin. It used not to be — the ray
// self-intersection offset was a fixed 1e-4, which is only a couple of float ULPs
// once coordinates reach the hundreds, so rays hit their own originating face and
// those faces read as fully occluded. On Laurana (bbox diagonal ~892) that was 4.5%
// of faces rendering black.
void FilterTests::ambientOcclusionIsScaleInvariant()
{
    Document probe;
    const QString aoKey = filterKeyForId(
        probe, QStringLiteral("compute_face_ambient_occlusion"));
    if (aoKey.isEmpty())
        QSKIP("Embree plugin is not available in this build.");
    const QString sphereKey = filterKeyForId(probe, QStringLiteral("create_sphere"));
    QVERIFY(!sphereKey.isEmpty());

    // Spread of the per-face AO value, as a fraction of its mean, plus the number of
    // faces that came out fully occluded (which on a convex body must be none).
    const auto aoSpread = [&](float scale, int &fullyOccluded) {
        Document doc;
        if (!doc.runFilter(sphereKey, {}).success)
            return -1.0;
        VCGMesh &mesh = doc.mesh(0).mesh;
        for (VCGVertex &v : mesh.vert)
            v.P() *= scale;
        vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);

        MeshFilterParameterValues params;
        params.insert(QStringLiteral("rays"), 64);
        if (!doc.runFilter(aoKey, params).success)
            return -1.0;

        double sum = 0.0;
        int count = 0;
        fullyOccluded = 0;
        for (const VCGFace &f : doc.mesh(0).mesh.face) {
            if (f.IsD())
                continue;
            sum += f.cQ();
            ++count;
            if (f.cQ() <= 0.0f)
                ++fullyOccluded;
        }
        if (count == 0 || sum <= 0.0)
            return -1.0;
        const double mean = sum / count;
        double var = 0.0;
        for (const VCGFace &f : doc.mesh(0).mesh.face)
            if (!f.IsD())
                var += (f.cQ() - mean) * (f.cQ() - mean);
        return std::sqrt(var / count) / mean;
    };

    // 0.4% is the residual from discretizing the hemisphere into 64 fixed directions;
    // 5% leaves headroom for that without admitting self-intersection noise, which
    // ran to 31% on Laurana and 86% on a sphere at this scale.
    for (float scale : { 1.0f, 100.0f, 1000.0f, 10000.0f }) {
        int fullyOccluded = -1;
        const double spread = aoSpread(scale, fullyOccluded);
        QVERIFY2(spread >= 0.0, "ambient occlusion run failed");
        QVERIFY2(
            spread < 0.05,
            qPrintable(QStringLiteral("AO spread %1 at scale %2 (expected < 0.05)")
                           .arg(spread).arg(scale)));
        QCOMPARE(fullyOccluded, 0);
    }
}

// Point samples do not occlude one another, so point-cloud AO traces against a
// separate surface. Exercise all three normal sources, including absent normals,
// while the translated occluder also verifies layer-transform handling.
void FilterTests::ambientOcclusionSupportsPointCloudsAndDirectionalLighting()
{
    Document doc;
    const QString aoKey = filterKeyForId(
        doc, QStringLiteral("compute_point_cloud_ambient_occlusion"));
    if (aoKey.isEmpty())
        QSKIP("Embree plugin is not available in this build.");

    VCGMesh occluder;
    makeCubeMesh(occluder, 0.0f, 0.0f, 0.0f);
    const int normalMask = vcg::tri::io::Mask::IOM_VERTCOORD
        | vcg::tri::io::Mask::IOM_VERTNORMAL
        | vcg::tri::io::Mask::IOM_FACENORMAL;
    QCOMPARE(doc.addMesh(occluder, QStringLiteral("Occluder"), normalMask), 0);
    QMatrix4x4 occluderTransform;
    occluderTransform.translate(10.0f, 0.0f, 0.0f);
    doc.setMeshTransform(0, occluderTransform);

    VCGMesh points;
    vcg::tri::Allocator<VCGMesh>::AddVertices(points, 2);
    points.vert[0].P() = vcg::Point3f(10.5f, 0.5f, 0.5f);
    points.vert[1].P() = vcg::Point3f(10.5f, 0.5f, 2.0f);
    for (VCGVertex &v : points.vert)
        v.N() = vcg::Point3f(0.0f, 0.0f, 1.0f);
    QCOMPARE(doc.addMesh(
        points, QStringLiteral("Oriented points"),
        vcg::tri::io::Mask::IOM_VERTCOORD | vcg::tri::io::Mask::IOM_VERTNORMAL), 1);
    doc.setCurrentMeshIndex(1);

    MeshFilterParameterValues params;
    params.insert(QStringLiteral("occluder_mesh"), 0);
    params.insert(QStringLiteral("normal_source"), QStringLiteral("point_normals"));
    params.insert(QStringLiteral("rays"), 64);
    params.insert(QStringLiteral("directional_bias"), 1.0);
    params.insert(QStringLiteral("cone_angle"), 5.0);
    params.insert(QStringLiteral("cone_direction"), QVector3D(0.0f, 0.0f, 1.0f));
    MeshFilterRunResult result = doc.runFilter(aoKey, params);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QCOMPARE(doc.mesh(1).mesh.face.size(), size_t(0));
    QCOMPARE(doc.mesh(1).mesh.vert[0].Q(), 0.0f);
    QVERIFY(doc.mesh(1).mesh.vert[1].Q() > 1.0f);

    params.insert(QStringLiteral("cone_direction"), QVector3D(0.0f, 0.0f, -1.0f));
    result = doc.runFilter(aoKey, params);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QCOMPARE(doc.mesh(1).mesh.vert[1].Q(), 0.0f);

    // The two remaining modes must not depend on target normals.
    for (VCGVertex &vertex : doc.mesh(1).mesh.vert)
        vertex.N() = vcg::Point3f(0.0f, 0.0f, 0.0f);

    params.insert(QStringLiteral("normal_source"), QStringLiteral("closest_occluder_surface"));
    params.insert(QStringLiteral("cone_direction"), QVector3D(0.0f, 0.0f, 1.0f));
    result = doc.runFilter(aoKey, params);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QCOMPARE(doc.mesh(1).mesh.vert[0].Q(), 0.0f);
    QVERIFY(doc.mesh(1).mesh.vert[1].Q() > 1.0f);

    params.insert(QStringLiteral("normal_source"), QStringLiteral("no_normal_spherical"));
    params.insert(QStringLiteral("directional_bias"), 0.0);
    result = doc.runFilter(aoKey, params);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QCOMPARE(doc.mesh(1).mesh.vert[0].Q(), 0.0f);
    QVERIFY(doc.mesh(1).mesh.vert[1].Q() > 1.0f);
}

namespace {

// Monte Carlo sampling of a cube, returned as the flattened sample coordinates so
// two runs can be compared exactly.
std::vector<float> montecarloSamples(const QString &key, int randomSeed)
{
    Document doc;
    VCGMesh cube;
    makeCubeMesh(cube, 0.0f, 0.0f, 0.0f);
    doc.addMesh(cube, QStringLiteral("Cube"), vcg::tri::io::Mask::IOM_VERTCOORD);

    MeshFilterParameterValues params;
    params.insert(QStringLiteral("SampleNum"), 200);
    params.insert(QStringLiteral("randomSeed"), randomSeed);
    const MeshFilterRunResult result = doc.runFilter(key, params);
    if (!result.success || result.newMeshIndices.isEmpty())
        return {};

    std::vector<float> coords;
    for (const VCGVertex &vertex : doc.mesh(result.newMeshIndices.front()).mesh.vert) {
        coords.push_back(vertex.cP()[0]);
        coords.push_back(vertex.cP()[1]);
        coords.push_back(vertex.cP()[2]);
    }
    return coords;
}

} // namespace

void FilterTests::randomSeedMakesSamplingReproducible()
{
    Document probe;
    const QString key = filterKeyForId(probe, QStringLiteral("generate_sampling_montecarlo"));
    QVERIFY(!key.isEmpty());

    const std::vector<float> first = montecarloSamples(key, 12345);
    const std::vector<float> second = montecarloSamples(key, 12345);
    QVERIFY(!first.empty());
    QCOMPARE(first, second);

    // A different seed must actually change the sampling, otherwise the parameter
    // would be silently ignored and the test above would pass vacuously.
    const std::vector<float> other = montecarloSamples(key, 999);
    QCOMPARE(other.size(), first.size());
    QVERIFY(other != first);
}

void FilterTests::randomSeedZeroVariesBetweenRuns()
{
    Document probe;
    const QString key = filterKeyForId(probe, QStringLiteral("generate_sampling_montecarlo"));
    QVERIFY(!key.isEmpty());

    const std::vector<float> first = montecarloSamples(key, 0);
    const std::vector<float> second = montecarloSamples(key, 0);
    QVERIFY(!first.empty());
    QCOMPARE(second.size(), first.size());
    QVERIFY(first != second);
}

void FilterTests::randomSeedControlsExpressionRnd()
{
    Document probe;
    const QString key = filterKeyForId(probe, QStringLiteral("per_vertex_quality_function"));
    QVERIFY(!key.isEmpty());

    // Write rnd() into the per-vertex scalar so the drawn values land somewhere we
    // can read back and compare.
    const auto qualityAfterRun = [&key](int randomSeed) {
        Document doc;
        VCGMesh cube;
        makeCubeMesh(cube, 0.0f, 0.0f, 0.0f);
        doc.addMesh(cube, QStringLiteral("Cube"), vcg::tri::io::Mask::IOM_VERTCOORD);

        MeshFilterParameterValues params;
        params.insert(QStringLiteral("q"), QStringLiteral("rnd()"));
        params.insert(QStringLiteral("randomSeed"), randomSeed);
        const MeshFilterRunResult result = doc.runFilter(key, params);

        std::vector<float> values;
        if (!result.success)
            return values;
        for (const VCGVertex &vertex : doc.mesh(0).mesh.vert)
            values.push_back(vertex.cQ());
        return values;
    };

    const std::vector<float> pinnedA = qualityAfterRun(4242);
    const std::vector<float> pinnedB = qualityAfterRun(4242);
    QVERIFY(!pinnedA.empty());
    QCOMPARE(pinnedA, pinnedB);

    const std::vector<float> autoA = qualityAfterRun(0);
    const std::vector<float> autoB = qualityAfterRun(0);
    QCOMPARE(autoA.size(), pinnedA.size());
    QVERIFY(autoA != autoB);
}

// Guard against a randomized filter being added later without a seed: every filter
// listed here was audited to draw from a generator, so each must expose the
// conventional control. Extend the list when a new randomized filter appears.
void FilterTests::randomizedFiltersDeclareARandomSeed()
{
    const QStringList randomizedFilterIds{
        QStringLiteral("generate_sampling_element"),
        QStringLiteral("generate_sampling_montecarlo"),
        QStringLiteral("generate_sampling_stratified_triangle"),
        QStringLiteral("generate_sampling_poisson_disk"),
        QStringLiteral("generate_simplified_point_cloud"),
        QStringLiteral("get_hausdorff_distance"),
        QStringLiteral("generate_sampling_voronoi"),
        QStringLiteral("generate_sampling_volumetric"),
        QStringLiteral("generate_voronoi_scaffolding"),
        QStringLiteral("generate_voronoi_atlas_parametrization"),
        QStringLiteral("compute_curvature_principal_directions_per_vertex"),
        QStringLiteral("apply_color_noising_per_vertex"),
        QStringLiteral("compute_color_scattering_per_mesh"),
        QStringLiteral("create_sphere_points"),
        QStringLiteral("create_points_on_a_spherical_cap"),
        QStringLiteral("displace_vertices_randomly"),
        QStringLiteral("compute_matrix_by_icp_between_meshes"),
        QStringLiteral("compute_matrix_by_mesh_global_alignment"),
        QStringLiteral("apply_texmap_defragmentation"),
        QStringLiteral("apply_small_islands_remover"),
        // filter_expression: randomness is opt-in through the formula's rnd() /
        // randInt() helpers, but it still has to be seedable. grid_generator is
        // excluded on purpose — it is the one filter there with no expression.
        QStringLiteral("conditional_vertex_selection"),
        QStringLiteral("conditional_face_selection"),
        QStringLiteral("per_vertex_geometric_function"),
        QStringLiteral("per_vertex_normal_function"),
        QStringLiteral("per_face_normal_function"),
        QStringLiteral("per_vertex_color_function"),
        QStringLiteral("per_face_color_function"),
        QStringLiteral("per_vertex_quality_function"),
        QStringLiteral("per_face_quality_function"),
        QStringLiteral("per_vertex_texture_function"),
        QStringLiteral("per_wedge_texture_function"),
        QStringLiteral("define_per_vertex_scalar_attribute"),
        QStringLiteral("define_per_face_scalar_attribute"),
        QStringLiteral("define_per_vertex_point_attribute"),
        QStringLiteral("define_per_face_point_attribute"),
        QStringLiteral("implicit_surface"),
        QStringLiteral("refine_user_defined"),
    };

    Document doc;
    for (const QString &filterId : randomizedFilterIds) {
        bool found = false;
        for (const auto &info : doc.filterInfos()) {
            if (info.descriptor.id != filterId)
                continue;
            found = true;
            const auto &parameters = info.descriptor.parameters;
            const auto seedParam = std::find_if(
                parameters.begin(),
                parameters.end(),
                [](const auto &p) { return p.id == QStringLiteral("randomSeed"); });
            QVERIFY2(
                seedParam != parameters.end(),
                qPrintable(filterId + QStringLiteral(" declares no randomSeed parameter")));
            QCOMPARE(seedParam->type, MeshFilterParameterType::Int);
            QCOMPARE(seedParam->defaultValue.toInt(), 0);
            break;
        }
        QVERIFY2(found, qPrintable(filterId + QStringLiteral(" is not registered")));
    }
}

QTEST_MAIN(FilterTests)
#include "test_filters.moc"
