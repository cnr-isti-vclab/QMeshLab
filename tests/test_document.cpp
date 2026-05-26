#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QFile>
#include <QTemporaryDir>
#include <cmath>

#include "document.h"
#include <vcg/complex/allocate.h>
#include <wrap/io_trimesh/io_mask.h>

class DocumentTests : public QObject
{
    Q_OBJECT

private slots:
    void cameraShotProjectsThroughImageCenter();
    void cameraShotUnprojectsImageCenter();
    void cameraShotRenderMatricesMatchProjection();
    void logReplaceLastEntryOnCarriageReturn();
    void loadMeshAddsLayerAndEmitsSignal();
    void addRasterImageCreatesDocumentLayer();
    void currentLayerKindFollowsMeshAndRasterSelection();
    void loadRasterImageReadsFile();
    void rasterCameraUndoRedoRestoresShot();
    void undoRedoRestoresMeshList();
    void undoTreeBranchingPreservesAlternateFuture();
    void openDialogFilterContainsKnownFormats();
    void savePlyPreservesWedgeTexcoordsWhenVertexTexcoordsExist();
    void benchmarkLoadMesh();
};

void DocumentTests::cameraShotProjectsThroughImageCenter()
{
    CameraShot shot = CameraShot::defaultPerspectiveForImageSize(QSize(1000, 500));
    QVERIFY(shot.isValid());
    QCOMPARE(shot.viewportPx(), QSize(1000, 500));
    QCOMPARE(shot.cameraType(), CameraShot::CameraType::Perspective);

    const QVector2D projected = shot.project(QVector3D(0.0f, 0.0f, -10.0f));
    QVERIFY(std::abs(projected.x() - 500.0f) < 1e-3f);
    QVERIFY(std::abs(projected.y() - 250.0f) < 1e-3f);
    QVERIFY(std::abs(shot.depth(QVector3D(0.0f, 0.0f, -10.0f)) - 10.0f) < 1e-3f);
}

void DocumentTests::cameraShotUnprojectsImageCenter()
{
    CameraShot shot = CameraShot::defaultPerspectiveForImageSize(QSize(1000, 500));
    QVERIFY(shot.isValid());

    const QVector3D world = shot.unproject(QVector2D(500.0f, 250.0f), 10.0f);
    QVERIFY(std::abs(world.x()) < 1e-3f);
    QVERIFY(std::abs(world.y()) < 1e-3f);
    QVERIFY(std::abs(world.z() + 10.0f) < 1e-3f);

    const QVector2D projected = shot.project(world);
    QVERIFY(std::abs(projected.x() - 500.0f) < 1e-3f);
    QVERIFY(std::abs(projected.y() - 250.0f) < 1e-3f);
}

void DocumentTests::cameraShotRenderMatricesMatchProjection()
{
    CameraShot shot = CameraShot::defaultPerspectiveForImageSize(QSize(1000, 500));
    QVERIFY(shot.isValid());

    const float nearPlane = 0.1f;
    const float farPlane = 100.0f;
    const QMatrix4x4 proj = shot.projectionMatrix(nearPlane, farPlane);
    const QMatrix4x4 view = shot.viewMatrix();
    const QMatrix4x4 mvp = proj * view;

    // Verify clip_w is positive for visible points (z_view must be negative).
    // If clip_w <= 0, depth is inverted and all visible geometry is GPU-clipped.
    const QVector3D testPoint(0.0f, 0.0f, -10.0f);
    const QVector4D clip0 = view * QVector4D(testPoint, 1.0f);
    QVERIFY2(
        clip0.z() < 0.0f,
        qPrintable(QStringLiteral("z in view space must be negative for visible "
                                  "geometry, got %1")
                       .arg(clip0.z())));
    const QVector4D clipProj = proj * clip0;
    QVERIFY2(
        clipProj.w() > 0.0f,
        qPrintable(QStringLiteral("clip_w must be positive for visible geometry "
                                  "(depth convention check), got %1")
                       .arg(clipProj.w())));
    const float zNdc = clipProj.z() / clipProj.w();
    QVERIFY2(
        zNdc >= -1.0f && zNdc <= 1.0f,
        qPrintable(QStringLiteral("z_ndc must be within [-1, 1] for visible "
                                  "geometry, got %1")
                       .arg(zNdc)));

    auto matrixProject = [&](const QVector3D &world) {
        QVector4D clip = mvp * QVector4D(world, 1.0f);
        if (std::abs(clip.w()) > 1e-6f)
            clip /= clip.w();
        // VCG's shot.project() uses y=0-at-bottom convention; match it here.
        return QVector2D(
            (clip.x() * 0.5f + 0.5f) * 1000.0f,
            (clip.y() * 0.5f + 0.5f) * 500.0f);
    };

    const QVector3D points[] = {
        QVector3D(0.0f, 0.0f, -10.0f),
        QVector3D(1.0f, 0.5f, -10.0f),
        QVector3D(-0.75f, -0.25f, -8.0f)
    };
    for (const QVector3D &point : points) {
        const QVector2D expected = shot.project(point);
        const QVector2D actual = matrixProject(point);
        QVERIFY(std::isfinite(actual.x()));
        QVERIFY(std::isfinite(actual.y()));
        QVERIFY2(
            std::abs(actual.x() - expected.x()) < 1e-3f,
            qPrintable(QStringLiteral("x actual=%1 expected=%2")
                           .arg(actual.x(), 0, 'f', 6)
                           .arg(expected.x(), 0, 'f', 6)));
        QVERIFY2(
            std::abs(actual.y() - expected.y()) < 1e-3f,
            qPrintable(QStringLiteral("y actual=%1 expected=%2")
                           .arg(actual.y(), 0, 'f', 6)
                           .arg(expected.y(), 0, 'f', 6)));
    }
}

void DocumentTests::logReplaceLastEntryOnCarriageReturn()
{
    Document doc;

    doc.writeLog(QStringLiteral("first"));
    doc.writeLog(QStringLiteral("progress\r"));

    const auto &log = doc.logMessages();
    QCOMPARE(log.size(), size_t(1));
    QCOMPARE(log.back().message, QStringLiteral("progress"));
    QCOMPARE(log.back().source, Document::LogSource::Application);
}

void DocumentTests::loadMeshAddsLayerAndEmitsSignal()
{
    Document doc;
    QSignalSpy addedSpy(&doc, &Document::meshAdded);

    const QString path = QStringLiteral(TEST_SOURCE_DIR "/tests/data/simple.off");
    const int err = doc.loadMesh(path);

    QCOMPARE(err, 0);
    QCOMPARE(doc.meshCount(), 1);
    QCOMPARE(addedSpy.count(), 1);
    QCOMPARE(doc.mesh(0).name, QStringLiteral("simple.off"));

    const auto &log = doc.logMessages();
    QVERIFY(!log.empty());
}

void DocumentTests::addRasterImageCreatesDocumentLayer()
{
    Document doc;
    QSignalSpy addedSpy(&doc, &Document::rasterAdded);
    QSignalSpy currentSpy(&doc, &Document::currentRasterChanged);

    QImage image(8, 4, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    const int index = doc.addRasterImage(image, QStringLiteral("photo"));

    QCOMPARE(index, 0);
    QCOMPARE(doc.rasterCount(), 1);
    QCOMPARE(doc.currentRasterIndex(), 0);
    QCOMPARE(doc.currentLayerKind(), Document::CurrentLayerKind::Raster);
    QCOMPARE(addedSpy.count(), 1);
    QCOMPARE(currentSpy.count(), 1);

    const auto &entry = doc.raster(0);
    QCOMPARE(entry.name, QStringLiteral("photo"));
    QCOMPARE(entry.visible, true);
    QCOMPARE(entry.planes.size(), size_t(1));
    QVERIFY(entry.currentPlane());
    QVERIFY(entry.currentPlane()->semantic == Document::RasterPlaneSemantic::RGBA);
    QCOMPARE(entry.currentPlane()->size, QSize(8, 4));
    QVERIFY(entry.currentPlane()->hasImage());

    QVERIFY(doc.canUndo());
    QVERIFY(doc.undo());
    QCOMPARE(doc.rasterCount(), 0);
    QVERIFY(doc.redo());
    QCOMPARE(doc.rasterCount(), 1);
    QCOMPARE(doc.raster(0).currentPlane()->size, QSize(8, 4));
}

void DocumentTests::currentLayerKindFollowsMeshAndRasterSelection()
{
    Document doc;
    VCGMesh mesh;
    vcg::tri::Allocator<VCGMesh>::AddVertex(mesh, VCGMesh::CoordType(0.0f, 0.0f, 0.0f));
    QCOMPARE(doc.addMesh(mesh, QStringLiteral("mesh")), 0);
    QCOMPARE(doc.currentMeshIndex(), 0);
    QCOMPARE(doc.currentLayerKind(), Document::CurrentLayerKind::Mesh);

    QImage image(2, 2, QImage::Format_RGBA8888);
    image.fill(Qt::white);
    QCOMPARE(doc.addRasterImage(image, QStringLiteral("raster")), 0);
    QCOMPARE(doc.currentRasterIndex(), 0);
    QCOMPARE(doc.currentLayerKind(), Document::CurrentLayerKind::Raster);

    doc.setCurrentMeshIndex(0);
    QCOMPARE(doc.currentLayerKind(), Document::CurrentLayerKind::Mesh);

    doc.setCurrentRasterIndex(0);
    QCOMPARE(doc.currentLayerKind(), Document::CurrentLayerKind::Raster);
}

void DocumentTests::loadRasterImageReadsFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("photo.png"));

    QImage image(5, 3, QImage::Format_RGBA8888);
    image.fill(Qt::green);
    QVERIFY(image.save(path));

    Document doc;
    QSignalSpy addedSpy(&doc, &Document::rasterAdded);
    const int index = doc.loadRasterImage(path);

    QCOMPARE(index, 0);
    QCOMPARE(addedSpy.count(), 1);
    QCOMPARE(doc.rasterCount(), 1);
    QCOMPARE(doc.raster(0).name, QStringLiteral("photo.png"));
    QCOMPARE(doc.raster(0).sourcePath, path);
    QVERIFY(doc.raster(0).currentPlane());
    QCOMPARE(doc.raster(0).currentPlane()->size, QSize(5, 3));
    QCOMPARE(doc.raster(0).currentPlane()->sourcePath, path);
}

void DocumentTests::rasterCameraUndoRedoRestoresShot()
{
    Document doc;
    QImage image(16, 8, QImage::Format_RGBA8888);
    image.fill(Qt::blue);
    QCOMPARE(doc.addRasterImage(image, QStringLiteral("camera_raster")), 0);
    QVERIFY(!doc.raster(0).shot.isValid());

    CameraShot shot = CameraShot::defaultPerspectiveForImageSize(image.size());
    shot.setViewPoint(QVector3D(1.0f, 2.0f, 3.0f));
    doc.setRasterShot(0, shot);
    QVERIFY(doc.raster(0).shot.isValid());
    QCOMPARE(doc.raster(0).shot.viewPoint(), QVector3D(1.0f, 2.0f, 3.0f));

    QVERIFY(doc.undo());
    QCOMPARE(doc.rasterCount(), 1);
    QVERIFY(!doc.raster(0).shot.isValid());

    QVERIFY(doc.redo());
    QCOMPARE(doc.rasterCount(), 1);
    QVERIFY(doc.raster(0).shot.isValid());
    QCOMPARE(doc.raster(0).shot.viewPoint(), QVector3D(1.0f, 2.0f, 3.0f));
}

void DocumentTests::undoRedoRestoresMeshList()
{
    Document doc;
    const QString path = QStringLiteral(TEST_SOURCE_DIR "/tests/data/simple.off");

    QCOMPARE(doc.meshCount(), 0);
    QVERIFY(!doc.canUndo());
    QVERIFY(!doc.canRedo());

    QCOMPARE(doc.loadMesh(path), 0);
    QCOMPARE(doc.meshCount(), 1);
    QVERIFY(doc.canUndo());
    QVERIFY(!doc.canRedo());

    QVERIFY(doc.undo());
    QCOMPARE(doc.meshCount(), 0);
    QVERIFY(!doc.canUndo());
    QVERIFY(doc.canRedo());

    QVERIFY(doc.redo());
    QCOMPARE(doc.meshCount(), 1);
    QVERIFY(doc.canUndo());
}

void DocumentTests::undoTreeBranchingPreservesAlternateFuture()
{
    Document doc;
    const QString path = QStringLiteral(TEST_SOURCE_DIR "/tests/data/simple.off");

    // Load two meshes — that creates two undo steps on the main branch.
    QCOMPARE(doc.loadMesh(path), 0);  // node A: 1 mesh
    QCOMPARE(doc.loadMesh(path), 0);  // node B: 2 meshes
    QCOMPARE(doc.meshCount(), 2);

    // Undo once → back to 1 mesh.
    QVERIFY(doc.undo());
    QCOMPARE(doc.meshCount(), 1);
    QVERIFY(doc.canRedo());  // node B is still there

    // Load again from this point → creates a NEW branch child, node C.
    QCOMPARE(doc.loadMesh(path), 0);  // node C: 2 meshes (on alternate branch)
    QCOMPARE(doc.meshCount(), 2);

    // The tree should have at least 4 nodes: root, A, B, C.
    const auto infos = doc.undoTreeInfo();
    QVERIFY(infos.size() >= 4);

    // The original redo branch (node B) must still be in the tree.
    int branchCount = 0;
    for (const auto &info : infos)
        if (!info.label.isEmpty() && !info.isOnCurrentPath)
            ++branchCount;
    QVERIFY(branchCount >= 1);

    // The current node must be C (most recent).
    int currentId = -1;
    for (const auto &info : infos)
        if (info.isCurrent) { currentId = info.nodeId; break; }
    QVERIFY(currentId >= 0);

    // Jump back to node B using jumpToUndoNode.
    int nodeBId = -1;
    for (const auto &info : infos)
        if (!info.isCurrent && !info.isOnCurrentPath && !info.label.isEmpty())
            { nodeBId = info.nodeId; break; }
    QVERIFY(nodeBId >= 0);
    QVERIFY(doc.jumpToUndoNode(nodeBId));
    QCOMPARE(doc.meshCount(), 2);
    QVERIFY(doc.undoCurrentNodeId() == nodeBId);
}

void DocumentTests::openDialogFilterContainsKnownFormats()
{
    Document doc;
    const QString filter = doc.openDialogFilter();
    const QStringList filters = filter.split(QStringLiteral(";;"), Qt::SkipEmptyParts);

    QVERIFY(!filters.isEmpty());
    QVERIFY(filters.first().contains(QStringLiteral("*.ply")));
    QVERIFY(filters.first().contains(QStringLiteral("*.obj")));
    QVERIFY(filter.contains(QStringLiteral("All Files (*)")));
}

void DocumentTests::savePlyPreservesWedgeTexcoordsWhenVertexTexcoordsExist()
{
    VCGMesh mesh;

    vcg::tri::Allocator<VCGMesh>::AddVertex(mesh, VCGMesh::CoordType(0.0f, 0.0f, 0.0f));
    vcg::tri::Allocator<VCGMesh>::AddVertex(mesh, VCGMesh::CoordType(1.0f, 0.0f, 0.0f));
    vcg::tri::Allocator<VCGMesh>::AddVertex(mesh, VCGMesh::CoordType(0.0f, 1.0f, 0.0f));
    vcg::tri::Allocator<VCGMesh>::AddFace(mesh, size_t(0), size_t(1), size_t(2));
    mesh.vert.EnableTexCoord();
    mesh.face.EnableWedgeTexCoord();

    // Deliberately make vertex UVs different from wedge UVs. ASCII VCGLib PLY
    // export can otherwise accidentally write VT values in the face texcoord list.
    mesh.vert[0].T().U() = 0.9f;
    mesh.vert[0].T().V() = 0.9f;
    mesh.vert[1].T().U() = 0.8f;
    mesh.vert[1].T().V() = 0.8f;
    mesh.vert[2].T().U() = 0.7f;
    mesh.vert[2].T().V() = 0.7f;
    mesh.face[0].WT(0).U() = 0.1f;
    mesh.face[0].WT(0).V() = 0.2f;
    mesh.face[0].WT(1).U() = 0.3f;
    mesh.face[0].WT(1).V() = 0.4f;
    mesh.face[0].WT(2).U() = 0.5f;
    mesh.face[0].WT(2).V() = 0.6f;
    QVERIFY(vcg::tri::HasPerVertexTexCoord(mesh));
    QVERIFY(vcg::tri::HasPerWedgeTexCoord(mesh));

    Document doc;
    const int meshIndex = doc.addMesh(
        mesh,
        QStringLiteral("wedge_uv"),
        vcg::tri::io::Mask::IOM_VERTCOORD
            | vcg::tri::io::Mask::IOM_FACEINDEX
            | vcg::tri::io::Mask::IOM_VERTTEXCOORD
            | vcg::tri::io::Mask::IOM_WEDGTEXCOORD);
    QVERIFY(meshIndex >= 0);
    QVERIFY(vcg::tri::HasPerVertexTexCoord(doc.mesh(meshIndex).mesh));
    QVERIFY(vcg::tri::HasPerWedgeTexCoord(doc.mesh(meshIndex).mesh));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString plyPath = dir.filePath(QStringLiteral("wedge_uv.ply"));

    MeshIOSaveOptions options;
    options.binary = false;
    options.mask = vcg::tri::io::Mask::IOM_VERTCOORD
        | vcg::tri::io::Mask::IOM_FACEINDEX
        | vcg::tri::io::Mask::IOM_WEDGTEXCOORD;
    QCOMPARE(doc.saveMesh(meshIndex, plyPath, options), 0);

    QFile plyFile(plyPath);
    QVERIFY(plyFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString ply = QString::fromUtf8(plyFile.readAll());
    QVERIFY2(
        ply.contains(QStringLiteral("property list uchar float texcoord")),
        qPrintable(ply));
    QVERIFY(ply.contains(QStringLiteral("6 0.100000 0.200000 0.300000 0.400000 0.500000 0.600000")));
    QVERIFY(!ply.contains(QStringLiteral("0.900000 0.900000")));
}

void DocumentTests::benchmarkLoadMesh()
{
    const QString path = QStringLiteral(TEST_SOURCE_DIR "/tests/data/simple.off");
    int err = -1;

    QBENCHMARK {
        Document doc;
        err = doc.loadMesh(path);
    }

    QCOMPARE(err, 0);
}

QTEST_MAIN(DocumentTests)
#include "test_document.moc"
