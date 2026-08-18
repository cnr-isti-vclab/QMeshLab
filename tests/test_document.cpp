#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <cmath>

#include "document.h"
#include <vcg/complex/allocate.h>
#include <vcg/space/planar_polygon_tessellation.h>
#include <wrap/io_trimesh/export_obj.h>
#include <wrap/io_trimesh/import_obj.h>
#include <wrap/io_trimesh/io_mask.h>

namespace {

QVector3D firstVertexPosition(const VCGMesh &mesh)
{
    for (const VCGVertex &vertex : mesh.vert) {
        if (!vertex.IsD())
            return QVector3D(vertex.cP().X(), vertex.cP().Y(), vertex.cP().Z());
    }
    return {};
}

}

class DocumentTests : public QObject
{
    Q_OBJECT

private slots:
    // Document and built-in I/O integration tests.
    void cameraShotProjectsThroughImageCenter();
    void cameraShotUnprojectsImageCenter();
    void cameraShotRenderMatricesMatchProjection();
    void logReplaceLastEntryOnCarriageReturn();
    void logEntriesCarryASeverityLevel();
    void logEntriesCarryATimestampOutsideTheMessage();
    void everyFilterRunReportsItsDuration();
    void undoRestoresDynamicFilterBounds();
    void progressLogIsTransientAndRemovedOnCompletion();
    void loadMeshAddsLayerAndEmitsSignal();
    void planarPolygonTessellationHandlesConcavity();
    void loadConcavePolygonFormatsPreserveFauxEdges();
    void loadObjWithMissingMaterialLibrary();
    void addRasterImageCreatesDocumentLayer();
    void currentLayerKindFollowsMeshAndRasterSelection();
    void loadRasterImageReadsFile();
    void loadMeshLabProjectLoadsMeshesAndTransforms();
    void loadMeshLabProjectLoadsRastersAndCamera();
    void rasterCameraUndoRedoRestoresShot();
    void undoRedoRestoresMeshList();
    void undoTreeBranchingPreservesAlternateFuture();
    void openDialogFilterContainsKnownFormats();
    void saveAndLoad3MFRoundTrip();
    void trueFormRoundTripsObjAndStl();
    void saveAndLoadEmbeddedGLBTexture();
    void savePlyPreservesWedgeTexcoordsWhenVertexTexcoordsExist();
    void savePolygonalFormatsHonorFauxEdgesAndTriangulationOption();
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

// The level is stored, defaults to Info, and is ordered loudest-first so a verbosity
// threshold is a plain comparison (the log panel filters on level <= threshold).
void DocumentTests::logEntriesCarryASeverityLevel()
{
    Document doc;
    QSignalSpy addedSpy(&doc, &Document::logMessageAdded);

    doc.writeLog(QStringLiteral("plain"));
    doc.writeLog(
        QStringLiteral("broke"), Document::LogSource::Application, Document::LogLevel::Error);
    doc.writeLog(
        QStringLiteral("timing 3 ms"), Document::LogSource::VCG, Document::LogLevel::Debug);

    const auto &log = doc.logMessages();
    QCOMPARE(log.size(), size_t(3));
    QCOMPARE(log[0].level, Document::LogLevel::Info);
    QCOMPARE(log[1].level, Document::LogLevel::Error);
    QCOMPARE(log[2].level, Document::LogLevel::Debug);
    QCOMPARE(log[2].source, Document::LogSource::VCG);

    QCOMPARE(addedSpy.count(), 3);
    QCOMPARE(addedSpy.at(1).at(2).value<Document::LogLevel>(), Document::LogLevel::Error);

    QVERIFY(Document::LogLevel::Error < Document::LogLevel::Warning);
    QVERIFY(Document::LogLevel::Warning < Document::LogLevel::Info);
    QVERIFY(Document::LogLevel::Info < Document::LogLevel::Debug);
}

// The stamp lives on the entry, not inside the text: the panel formats it (or omits it)
// according to log.timestamp, and searching or asserting on a message sees the message.
void DocumentTests::logEntriesCarryATimestampOutsideTheMessage()
{
    const qint64 before = QDateTime::currentMSecsSinceEpoch();
    Document doc;
    doc.writeLog(QStringLiteral("hello"));
    const qint64 after = QDateTime::currentMSecsSinceEpoch();

    const Document::LogEntry &entry = doc.logMessages().back();
    QCOMPARE(entry.message, QStringLiteral("hello"));
    QVERIFY(entry.epochMs >= before);
    QVERIFY(entry.epochMs <= after);
    // Startup precedes any message, so an elapsed offset is never negative.
    QVERIFY(Document::applicationStartMSecsSinceEpoch() > 0);
    QVERIFY(entry.epochMs >= Document::applicationStartMSecsSinceEpoch());
}

// The duration is logged by Document::runFilter itself, so it is reported no matter which
// entry point invoked the filter — the menu, the panel's Apply, Python or a tool — and at
// Info, so it is visible without turning on debug logging.
void DocumentTests::everyFilterRunReportsItsDuration()
{
    Document doc;
    QVERIFY(doc.loadMesh(QStringLiteral(TEST_SOURCE_DIR "/tests/data/simple.off")) == 0);

    QString filterKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("mesh_info")) {
            filterKey = info.key;
            break;
        }
    }
    QVERIFY(!filterKey.isEmpty());

    doc.clearLog();
    QVERIFY2(doc.runFilter(filterKey, {}).success, "mesh_info filter did not run");

    const Document::LogEntry *durationEntry = nullptr;
    for (const auto &entry : doc.logMessages()) {
        if (entry.message.contains(QStringLiteral("completed in")))
            durationEntry = &entry;
    }
    QVERIFY2(durationEntry, "no completion line was logged for the filter run");
    QCOMPARE(durationEntry->level, Document::LogLevel::Info);
    // Named by its display name, and carrying a millisecond figure.
    QVERIFY2(
        durationEntry->message.contains(QStringLiteral(" ms")),
        qPrintable(durationEntry->message));
    static const QRegularExpression msFigure(QStringLiteral("completed in \\d+\\.\\d\\d ms$"));
    QVERIFY2(msFigure.match(durationEntry->message).hasMatch(), qPrintable(durationEntry->message));

    // A failed run is timed too, so a filter that dies still says how long it took.
    doc.clearLog();
    MeshFilterParameterValues bad;
    bad.insert(QStringLiteral("nonexistentParameter"), 1.0);
    doc.runFilter(filterKey, bad);
    bool sawFailureDuration = false;
    for (const auto &entry : doc.logMessages()) {
        if (entry.message.contains(QStringLiteral("failed after")))
            sawFailureDuration = true;
    }
    QVERIFY(sawFailureDuration);
}

// Dynamic parameter bounds (@faceCount and friends) are resolved from the current mesh.
// An undo changes the mesh under them, but every refresh trigger in the UI bails out while
// isRestoringUndoRedo() is true, so without undoRestoreCompleted() the bounds stay frozen
// at the simplified mesh and a decimation dialog caps the target at the *old* face count.
void DocumentTests::undoRestoresDynamicFilterBounds()
{
    Document doc;
    QVERIFY(doc.loadMesh(QStringLiteral(TEST_SOURCE_DIR "/tests/sample_mesh/sphere_40kv.ply")) == 0);
    const int originalFaces = doc.mesh(0).mesh.FN();
    QVERIFY(originalFaces > 100);

    auto targetFaceBounds = [&doc]() {
        for (const auto &info : doc.filterInfos()) {
            if (info.descriptor.id != QStringLiteral("meshing_decimation_quadric_edge_collapse"))
                continue;
            for (const auto &p : info.descriptor.parameters) {
                if (p.id == QStringLiteral("TargetFaceNum"))
                    return std::pair<int, int>(p.defaultValue.toInt(), p.maxValue.toInt());
            }
        }
        return std::pair<int, int>(-1, -1);
    };

    const auto beforeBounds = targetFaceBounds();
    QCOMPARE(beforeBounds.second, originalFaces);
    QCOMPARE(beforeBounds.first, std::max(1, originalFaces / 2));

    QString decimationKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("meshing_decimation_quadric_edge_collapse")) {
            decimationKey = info.key;
            break;
        }
    }
    QVERIFY(!decimationKey.isEmpty());

    MeshFilterParameterValues params;
    params.insert(QStringLiteral("TargetFaceNum"), originalFaces / 4);
    QVERIFY2(doc.runFilter(decimationKey, params).success, "decimation did not run");
    QVERIFY(doc.mesh(0).mesh.FN() < originalFaces);
    QCOMPARE(targetFaceBounds().second, doc.mesh(0).mesh.FN());

    // The restore-completed signal is the only notification that arrives with the
    // restoring flag already cleared, so it is what a view can safely refresh from.
    QSignalSpy restoredSpy(&doc, &Document::undoRestoreCompleted);
    QVERIFY(doc.canUndo());
    QVERIFY(doc.undo());
    QCOMPARE(doc.mesh(0).mesh.FN(), originalFaces);
    QCOMPARE(restoredSpy.count(), 1);

    // Re-resolved after the undo, the bounds match the restored mesh again.
    const auto afterBounds = targetFaceBounds();
    QCOMPARE(afterBounds.second, originalFaces);
    QCOMPARE(afterBounds.first, beforeBounds.first);
}

// Progress occupies a single transient line that overwrites itself and is gone once
// the operation ends, so a finished filter leaves no progress trace in the log.
void DocumentTests::progressLogIsTransientAndRemovedOnCompletion()
{
    Document doc;
    QVERIFY(doc.loadMesh(QStringLiteral(TEST_SOURCE_DIR "/tests/sample_mesh/sphere_40kv.ply")) >= 0);

    QString decimationKey;
    for (const auto &info : doc.filterInfos()) {
        if (info.descriptor.id == QStringLiteral("meshing_decimation_quadric_edge_collapse")) {
            decimationKey = info.key;
            break;
        }
    }
    QVERIFY(!decimationKey.isEmpty());

    doc.clearLog();
    QSignalSpy addedSpy(&doc, &Document::logMessageAdded);
    QSignalSpy removedSpy(&doc, &Document::logLastEntryRemoved);

    MeshFilterParameterValues params;
    params.insert(QStringLiteral("TargetFaceNum"), 2000);
    const MeshFilterRunResult result = doc.runFilter(decimationKey, params);
    QVERIFY2(result.success, qPrintable(result.errorMessage));

    // The filter reported progress...
    int progressAdds = 0;
    int progressReplacements = 0;
    for (const QList<QVariant> &call : addedSpy) {
        const QString message = call.at(0).toString();
        if (!message.contains(QStringLiteral("Progress ")) && !message.contains(QStringLiteral("% - ")))
            continue;
        ++progressAdds;
        // (message, source, level, replaceLast)
        if (call.at(3).toBool())
            ++progressReplacements;
    }
    QVERIFY2(progressAdds > 0, "the filter never reported progress, so nothing was tested");
    // ...and every tick after the first overwrote the line instead of appending.
    QCOMPARE(progressReplacements, progressAdds - 1);

    // ...and the line was taken away at the end.
    QVERIFY(removedSpy.count() >= 1);
    for (const auto &entry : doc.logMessages()) {
        const QString message = entry.message;
        QVERIFY2(
            !message.contains(QStringLiteral("Progress "))
                && !message.contains(QStringLiteral("% - ")),
            qPrintable(QStringLiteral("progress line survived: %1").arg(message)));
    }
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

void DocumentTests::planarPolygonTessellationHandlesConcavity()
{
    const std::vector<std::vector<vcg::Point3d>> polygons = {
        { { 0, 100, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 1, 0, 0 } },
        { { 0, 0, 0 }, { 3, 0, 0 }, { 3, 3, 0 }, { 2, 3, 0 },
          { 2, 1, 0 }, { 1, 1, 0 }, { 1, 3, 0 }, { 0, 3, 0 } },
        { { 0, 1, 0 }, { 2, 1, 0 }, { 2, 0, 0 }, { 1, 0, 0 }, { 0, 0, 0 } }
    };

    for (const auto &polygon : polygons) {
        std::vector<int> triangles;
        QVERIFY(vcg::TessellatePlanarPolygon3(polygon, triangles));
        QCOMPARE(triangles.size(), 3 * (polygon.size() - 2));

        double polygonDoubleArea = 0.0;
        for (size_t i = 0; i < polygon.size(); ++i)
            polygonDoubleArea += polygon[i].X() * polygon[(i + 1) % polygon.size()].Y()
                - polygon[i].Y() * polygon[(i + 1) % polygon.size()].X();

        double triangleDoubleArea = 0.0;
        for (size_t i = 0; i < triangles.size(); i += 3) {
            const vcg::Point3d &a = polygon[triangles[i]];
            const vcg::Point3d &b = polygon[triangles[i + 1]];
            const vcg::Point3d &c = polygon[triangles[i + 2]];
            const double area = ((b - a) ^ (c - a)).Z();
            QVERIFY(area * polygonDoubleArea > 0.0);
            triangleDoubleArea += area;
        }
        QVERIFY(std::abs(triangleDoubleArea - polygonDoubleArea) < 1e-9);
    }

    const std::vector<vcg::Point3d> vertical = {
        { 0, 0, 0 }, { 2, 0, 0 }, { 2, 0, 2 }, { 1, 0, 0.5 }, { 0, 0, 2 }
    };
    std::vector<int> verticalTriangles;
    QVERIFY(vcg::TessellatePlanarPolygon3(vertical, verticalTriangles));
    QCOMPARE(verticalTriangles.size(), size_t(9));

    const std::vector<vcg::Point3d> duplicate = {
        { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }
    };
    std::vector<int> fallbackTriangles;
    bool usedFallback = false;
    QVERIFY(vcg::TessellatePlanarPolygon3(duplicate, fallbackTriangles, &usedFallback));
    QVERIFY(usedFallback);
    QCOMPARE(fallbackTriangles.size(), size_t(6));

    const std::vector<vcg::Point3d> nonPlanarQuad = {
        { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0.1 }, { 0, 1, 0 }
    };
    std::vector<int> projectedTriangles;
    usedFallback = true;
    QVERIFY(vcg::TessellatePlanarPolygon3(nonPlanarQuad, projectedTriangles, &usedFallback));
    QVERIFY(!usedFallback);
    QCOMPARE(projectedTriangles.size(), size_t(6));

    const std::vector<vcg::Point3d> selfIntersecting = {
        { 0, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 }, { 1, 0, 0 }
    };
    fallbackTriangles.clear();
    usedFallback = false;
    QVERIFY(vcg::TessellatePlanarPolygon3(
        selfIntersecting, fallbackTriangles, &usedFallback));
    QVERIFY(usedFallback);
    QCOMPARE(fallbackTriangles.size(), size_t(6));
}

void DocumentTests::loadConcavePolygonFormatsPreserveFauxEdges()
{
    for (const QString &extension : { QStringLiteral("off"), QStringLiteral("obj"), QStringLiteral("ply") }) {
        Document doc;
        const bool forceVcgObj = extension == QLatin1String("obj");
        const QString oldObjPreference = doc.preferredImportPluginForExtension(QStringLiteral("obj"));
        const auto restorePreference = qScopeGuard([&] {
            if (forceVcgObj)
                doc.setPreferredImportPluginForExtension(QStringLiteral("obj"), oldObjPreference);
        });
        if (forceVcgObj)
            doc.setPreferredImportPluginForExtension(extension, QStringLiteral("io_vcg"));

        const QString path = QStringLiteral(TEST_SOURCE_DIR "/tests/data/concave_polygon.%1").arg(extension);
        QCOMPARE(doc.loadMesh(path), 0);
        const Document::MeshEntry &entry = doc.mesh(0);
        QCOMPARE(entry.mesh.VN(), 8);
        QCOMPARE(entry.mesh.FN(), 3);
        QVERIFY(entry.ioMask & vcg::tri::io::Mask::IOM_BITPOLYGONAL);

        const VCGVertex *vertexBase = entry.mesh.vert.data();
        const QSet<int> polygonVertices = { 2, 4, 5, 6, 7 };
        int fauxEdgeCount = 0;
        double triangulatedArea = 0.0;
        for (const VCGFace &face : entry.mesh.face) {
            for (int corner = 0; corner < 3; ++corner) {
                QVERIFY(polygonVertices.contains(int(face.cV(corner) - vertexBase)));
                fauxEdgeCount += face.IsF(corner) ? 1 : 0;
            }
            triangulatedArea += vcg::DoubleArea(face) * 0.5;
        }

        // A pentagon has two internal diagonals, each marked faux on both incident triangles.
        QCOMPARE(fauxEdgeCount, 4);
        // Ear clipping covers the concave polygon without the overlap produced by a fan.
        QVERIFY(std::abs(triangulatedArea - 2.5) < 1e-6);
        if (extension == QLatin1String("ply"))
            for (const VCGFace &face : entry.mesh.face)
                QVERIFY(face.cC() == vcg::Color4b(10, 20, 30, 40));
    }

    VCGMesh fallbackQuad;
    int mask = 0;
    const QByteArray path = QFile::encodeName(
        QStringLiteral(TEST_SOURCE_DIR "/tests/data/quad_projection_fallback.obj"));
    vcg::tri::io::ImporterOBJ<VCGMesh>::LoadMask(path.constData(), mask);
    const int warning = vcg::tri::io::ImporterOBJ<VCGMesh>::Open(
        fallbackQuad, path.constData(), mask);
    QVERIFY(warning != 0);
    QVERIFY(!vcg::tri::io::ImporterOBJ<VCGMesh>::ErrorCritical(warning));
    QCOMPARE(fallbackQuad.FN(), 2);
    int fauxEdgeCount = 0;
    for (const VCGFace &face : fallbackQuad.face)
        for (int edge = 0; edge < 3; ++edge)
            fauxEdgeCount += face.IsF(edge) ? 1 : 0;
    QCOMPARE(fauxEdgeCount, 2);
}

void DocumentTests::loadObjWithMissingMaterialLibrary()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("missing_material.obj"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(
        "mtllib absent.mtl\n"
        "usemtl absent\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");
    file.close();

    Document doc;
    const QString oldPreference = doc.preferredImportPluginForExtension(QStringLiteral("obj"));
    const auto restorePreference = qScopeGuard([&] {
        doc.setPreferredImportPluginForExtension(QStringLiteral("obj"), oldPreference);
    });

    for (const QString &pluginId : {
             QStringLiteral("io_obj_rapidobj"),
             QStringLiteral("io_vcg") }) {
        doc.setPreferredImportPluginForExtension(QStringLiteral("obj"), pluginId);
        QCOMPARE(doc.loadMesh(path), 0);
        QCOMPARE(doc.mesh(doc.meshCount() - 1).mesh.FN(), 1);
    }

    QVERIFY(std::any_of(
        doc.logMessages().cbegin(),
        doc.logMessages().cend(),
        [](const Document::LogEntry &entry) {
            return entry.message.contains(QStringLiteral("Load warning:"))
                && entry.message.contains(QStringLiteral("default white material"));
        }));
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
    QCOMPARE(doc.currentLayerKind(), CurrentLayerKind::Raster);
    QCOMPARE(addedSpy.count(), 1);
    QCOMPARE(currentSpy.count(), 1);

    const auto &entry = doc.raster(0);
    QCOMPARE(entry.name, QStringLiteral("photo"));
    QCOMPARE(entry.visible, true);
    QCOMPARE(entry.planes.size(), size_t(1));
    QVERIFY(entry.currentPlane());
    QVERIFY(entry.currentPlane()->semantic == RasterPlaneSemantic::RGBA);
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
    QCOMPARE(doc.currentLayerKind(), CurrentLayerKind::Mesh);

    QImage image(2, 2, QImage::Format_RGBA8888);
    image.fill(Qt::white);
    QCOMPARE(doc.addRasterImage(image, QStringLiteral("raster")), 0);
    QCOMPARE(doc.currentRasterIndex(), 0);
    QCOMPARE(doc.currentLayerKind(), CurrentLayerKind::Raster);

    doc.setCurrentMeshIndex(0);
    QCOMPARE(doc.currentLayerKind(), CurrentLayerKind::Mesh);

    doc.setCurrentRasterIndex(0);
    QCOMPARE(doc.currentLayerKind(), CurrentLayerKind::Raster);
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

void DocumentTests::loadMeshLabProjectLoadsMeshesAndTransforms()
{
    Document doc;
    const QString path = QStringLiteral(TEST_SOURCE_DIR "/tests/rangemaps/partial.mlp");

    QCOMPARE(doc.loadMeshLabProject(path), 0);
    QCOMPARE(doc.meshCount(), 12);
    QCOMPARE(doc.rasterCount(), 0);

    const Document::MeshEntry &first = doc.mesh(0);
    QCOMPARE(first.name, QStringLiteral("lato225.ply"));
    QCOMPARE(QFileInfo(first.sourcePath).fileName(), QStringLiteral("lato225.ply"));
    QVERIFY(std::abs(first.transform(0, 0) - (-0.54984f)) < 1e-4f);
    QVERIFY(std::abs(first.transform(0, 3) - (-657.674f)) < 1e-3f);

    const Document::MeshEntry &last = doc.mesh(11);
    QCOMPARE(last.name, QStringLiteral("faccia000.ply"));
    QVERIFY(std::abs(last.transform(2, 2) - 0.999041f) < 1e-4f);
}

void DocumentTests::loadMeshLabProjectLoadsRastersAndCamera()
{
    Document doc;
    const QString path = QStringLiteral(TEST_SOURCE_DIR "/tests/COLOR_gargoyle/gargoyle_final.mlp");

    QCOMPARE(doc.loadMeshLabProject(path), 0);
    QCOMPARE(doc.meshCount(), 1);
    QCOMPARE(doc.rasterCount(), 11);

    const Document::MeshEntry &mesh = doc.mesh(0);
    QCOMPARE(mesh.name, QStringLiteral("gargo3M.ply"));

    const Document::RasterEntry &raster = doc.raster(0);
    QCOMPARE(raster.name, QStringLiteral("DSC_0033.JPG"));
    QVERIFY(raster.shot.isValid());
    QCOMPARE(raster.shot.viewportPx(), QSize(3000, 2008));
    QVERIFY(std::abs(raster.shot.focalMm() - 149.109f) < 1e-3f);
    QVERIFY(raster.currentPlane());
    QCOMPARE(raster.currentPlane()->size, QSize(3000, 2008));
    QCOMPARE(QFileInfo(raster.currentPlane()->sourcePath).fileName(), QStringLiteral("DSC_0033.JPG"));
    QVERIFY(raster.currentPlane()->hasImage());
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
    QVERIFY(filters.first().contains(QStringLiteral("*.mlp")));
    QVERIFY(filter.contains(QStringLiteral("MeshLab Project (*.mlp)")));
    QVERIFY(filter.contains(QStringLiteral("All Files (*)")));
    QVERIFY(filter.contains(QStringLiteral("3MF Files (*.3mf)")));
}

void DocumentTests::saveAndLoad3MFRoundTrip()
{
    VCGMesh mesh;
    vcg::tri::Allocator<VCGMesh>::AddVertex(mesh, VCGMesh::CoordType(0.0f, 0.0f, 0.0f));
    vcg::tri::Allocator<VCGMesh>::AddVertex(mesh, VCGMesh::CoordType(2.0f, 0.0f, 0.0f));
    vcg::tri::Allocator<VCGMesh>::AddVertex(mesh, VCGMesh::CoordType(0.0f, 3.0f, 0.0f));
    vcg::tri::Allocator<VCGMesh>::AddVertex(mesh, VCGMesh::CoordType(2.0f, 3.0f, 0.0f));
    vcg::tri::Allocator<VCGMesh>::AddFace(mesh, size_t(0), size_t(1), size_t(2));
    vcg::tri::Allocator<VCGMesh>::AddFace(mesh, size_t(1), size_t(3), size_t(2));
    mesh.face[0].C() = vcg::Color4b(255, 0, 0, 255);
    mesh.face[1].C() = vcg::Color4b(0, 0, 255, 128);

    Document source;
    const int meshIndex = source.addMesh(
        mesh, QStringLiteral("triangles"), vcg::tri::io::Mask::IOM_FACECOLOR);
    QVERIFY(meshIndex >= 0);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("roundtrip.3mf"));
    MeshIOSaveOptions options;
    options.mask = vcg::tri::io::Mask::IOM_VERTCOORD
        | vcg::tri::io::Mask::IOM_FACEINDEX
        | vcg::tri::io::Mask::IOM_FACECOLOR;
    QCOMPARE(source.saveMesh(meshIndex, path, options), 0);
    QVERIFY(QFileInfo(path).size() > 0);

    Document loaded;
    QCOMPARE(loaded.loadMesh(path), 0);
    QCOMPARE(loaded.meshCount(), 1);
    QCOMPARE(loaded.mesh(0).mesh.VN(), 4);
    QCOMPARE(loaded.mesh(0).mesh.FN(), 2);
    QVERIFY(loaded.mesh(0).ioMask & vcg::tri::io::Mask::IOM_FACECOLOR);
    const VCGMesh &result = loaded.mesh(0).mesh;
    QCOMPARE(result.vert[1].cP().X(), 2.0f);
    QCOMPARE(result.vert[2].cP().Y(), 3.0f);
    QCOMPARE(result.face[0].cC(), vcg::Color4b(255, 0, 0, 255));
    QCOMPARE(result.face[1].cC(), vcg::Color4b(0, 0, 255, 128));
}

// TrueForm is a second, independent OBJ/STL reader-writer. It exists precisely so a file
// one parser rejects can be opened by another, so the test forces it to be the reader for
// both extensions rather than accepting whichever plugin happens to be registered first.
//
// It carries geometry only — no UVs, normals or materials — and STL import welds
// coincident vertices while loading, which is what the vertex-count assertions check.
void DocumentTests::trueFormRoundTripsObjAndStl()
{
    Document probe;
    const QString trueFormId = QStringLiteral("qmeshlab.io.trueform");
    if (!probe.openDialogFilter().contains(QStringLiteral("TrueForm")))
        QSKIP("TrueForm I/O plugin is not available in this build.");

    // A quad split into two triangles: 4 shared vertices, 2 faces.
    VCGMesh mesh;
    vcg::tri::Allocator<VCGMesh>::AddVertex(mesh, VCGMesh::CoordType(0.0f, 0.0f, 0.0f));
    vcg::tri::Allocator<VCGMesh>::AddVertex(mesh, VCGMesh::CoordType(2.0f, 0.0f, 0.0f));
    vcg::tri::Allocator<VCGMesh>::AddVertex(mesh, VCGMesh::CoordType(0.0f, 3.0f, 0.0f));
    vcg::tri::Allocator<VCGMesh>::AddVertex(mesh, VCGMesh::CoordType(2.0f, 3.0f, 0.0f));
    vcg::tri::Allocator<VCGMesh>::AddFace(mesh, size_t(0), size_t(1), size_t(2));
    vcg::tri::Allocator<VCGMesh>::AddFace(mesh, size_t(1), size_t(3), size_t(2));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MeshIOSaveOptions options;
    options.mask = vcg::tri::io::Mask::IOM_VERTCOORD | vcg::tri::io::Mask::IOM_FACEINDEX;

    // setPreferredImportPluginForExtension persists to QSettings, so it is process-wide
    // and outlives this test — it would otherwise redirect every later .obj/.stl load and
    // save, here and in the user's own application. Capture and restore.
    const QString previousObj = probe.preferredImportPluginForExtension(QStringLiteral("obj"));
    const QString previousStl = probe.preferredImportPluginForExtension(QStringLiteral("stl"));
    struct PreferenceRestorer {
        Document *doc;
        QString obj;
        QString stl;
        ~PreferenceRestorer()
        {
            doc->setPreferredImportPluginForExtension(QStringLiteral("obj"), obj);
            doc->setPreferredImportPluginForExtension(QStringLiteral("stl"), stl);
        }
    } restorer{ &probe, previousObj, previousStl };

    for (const QString &ext : { QStringLiteral("obj"), QStringLiteral("stl") }) {
        Document source;
        const int meshIndex = source.addMesh(mesh, QStringLiteral("quad"), options.mask);
        QVERIFY(meshIndex >= 0);
        source.setPreferredImportPluginForExtension(ext, trueFormId);

        const QString path = dir.filePath(QStringLiteral("roundtrip.") + ext);
        QCOMPARE(source.saveMesh(meshIndex, path, options), 0);
        QVERIFY2(QFileInfo(path).size() > 0, qPrintable(ext));

        Document loaded;
        loaded.setPreferredImportPluginForExtension(ext, trueFormId);
        QCOMPARE(loaded.loadMesh(path), 0);
        QCOMPARE(loaded.meshCount(), 1);

        const VCGMesh &result = loaded.mesh(0).mesh;
        QCOMPARE(result.FN(), 2);
        // STL stores an unshared triangle soup; TrueForm welds it back to 4 on import.
        QVERIFY2(result.VN() == 4, qPrintable(QStringLiteral("%1: VN=%2").arg(ext).arg(result.VN())));

        vcg::Box3f box;
        for (const VCGVertex &v : result.vert)
            box.Add(v.cP());
        QVERIFY2(std::abs(box.DimX() - 2.0f) < 1e-3f, qPrintable(ext));
        QVERIFY2(std::abs(box.DimY() - 3.0f) < 1e-3f, qPrintable(ext));
    }
}

void DocumentTests::saveAndLoadEmbeddedGLBTexture()
{
    // Embedded images must remain in memory across export and import.
    VCGMesh mesh;
    vcg::tri::Allocator<VCGMesh>::AddVertex(mesh, VCGMesh::CoordType(0, 0, 0));
    vcg::tri::Allocator<VCGMesh>::AddVertex(mesh, VCGMesh::CoordType(1, 0, 0));
    vcg::tri::Allocator<VCGMesh>::AddVertex(mesh, VCGMesh::CoordType(0, 1, 0));
    vcg::tri::Allocator<VCGMesh>::AddFace(mesh, size_t(0), size_t(1), size_t(2));
    mesh.face.EnableWedgeTexCoord();
    mesh.textures.push_back("embedded.png");
    for (int corner = 0; corner < 3; ++corner)
        mesh.face[0].WT(corner).N() = 0;

    Document source;
    const int meshIndex = source.addMesh(
        mesh, QStringLiteral("textured"), vcg::tri::io::Mask::IOM_WEDGTEXCOORD);
    QImage image(2, 1, QImage::Format_RGBA8888);
    image.setPixelColor(0, 0, Qt::red);
    image.setPixelColor(1, 0, Qt::green);
    MeshIOTextureAsset asset;
    asset.name = QStringLiteral("embedded.png");
    asset.image = image;
    source.mesh(meshIndex).textureAssets = { asset };
    source.mesh(meshIndex).materialSet.entries.resize(1);
    source.mesh(meshIndex).materialSet.entries[0].baseColorTexture.fileName = asset.name;
    source.mesh(meshIndex).materialSet.entries[0].baseColorTexture.assetIndex = 0;

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("embedded.glb"));
    MeshIOSaveOptions options;
    options.mask = vcg::tri::io::Mask::IOM_VERTCOORD
        | vcg::tri::io::Mask::IOM_FACEINDEX
        | vcg::tri::io::Mask::IOM_WEDGTEXCOORD;
    options.embedTextures = true;
    QCOMPARE(source.saveMesh(meshIndex, path, options), 0);

    Document loaded;
    QCOMPARE(loaded.loadMesh(path), 0);
    const MeshIOTextureAsset *loadedAsset = Document::meshTextureAsset(loaded.mesh(0), 0);
    QVERIFY(loadedAsset);
    QCOMPARE(loadedAsset->image.size(), QSize(2, 1));
    QCOMPARE(loadedAsset->image.pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(loadedAsset->image.pixelColor(1, 0), QColor(Qt::green));
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

void DocumentTests::savePolygonalFormatsHonorFauxEdgesAndTriangulationOption()
{
    const int objCapability = vcg::tri::io::ExporterOBJ<VCGMesh>::GetExportMaskCapability();
    QVERIFY(objCapability & vcg::tri::io::Mask::IOM_VERTCOORD);
    QVERIFY(objCapability & vcg::tri::io::Mask::IOM_FACEINDEX);

    VCGMesh mesh;
    mesh.face.EnableWedgeTexCoord();
    for (const VCGMesh::CoordType &point : {
             VCGMesh::CoordType(0, 0, 0), VCGMesh::CoordType(1, 0, 0),
             VCGMesh::CoordType(1, 1, 0), VCGMesh::CoordType(0, 1, 0) })
        vcg::tri::Allocator<VCGMesh>::AddVertex(mesh, point);
    vcg::tri::Allocator<VCGMesh>::AddFace(mesh, size_t(0), size_t(1), size_t(2));
    vcg::tri::Allocator<VCGMesh>::AddFace(mesh, size_t(0), size_t(2), size_t(3));
    mesh.face[0].SetF(2);
    mesh.face[1].SetF(0);

    const std::array<vcg::Point2f, 4> uv = {
        vcg::Point2f(0, 0), vcg::Point2f(1, 0), vcg::Point2f(1, 1), vcg::Point2f(0, 1)
    };
    const std::array<int, 3> secondFaceUV = { 0, 2, 3 };
    for (int corner = 0; corner < 3; ++corner) {
        mesh.face[0].WT(corner).P() = uv[size_t(corner)];
        mesh.face[1].WT(corner).P() = uv[size_t(secondFaceUV[size_t(corner)])];
    }

    Document doc;
    const int meshIndex = doc.addMesh(
        mesh,
        QStringLiteral("textured_quad"),
        vcg::tri::io::Mask::IOM_WEDGTEXCOORD
            | vcg::tri::io::Mask::IOM_BITPOLYGONAL);
    QVERIFY(meshIndex >= 0);
    QCOMPARE(doc.mesh(meshIndex).polygonFaceCount, 1);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("quad.obj"));
    MeshIOSaveOptions options;
    options.mask = vcg::tri::io::Mask::IOM_VERTCOORD
        | vcg::tri::io::Mask::IOM_FACEINDEX
        | vcg::tri::io::Mask::IOM_WEDGTEXCOORD
        | vcg::tri::io::Mask::IOM_BITPOLYGONAL;
    QCOMPARE(doc.saveMesh(meshIndex, path, options), 0);
    QFile obj(path);
    QVERIFY(obj.open(QIODevice::ReadOnly));
    QVERIFY(obj.readAll().contains("# Faces: 1"));

    const QString trianglePath = dir.filePath(QStringLiteral("quad_triangles.obj"));
    options.mask = vcg::tri::io::Mask::IOM_VERTCOORD
        | vcg::tri::io::Mask::IOM_FACEINDEX;
    QCOMPARE(doc.saveMesh(meshIndex, trianglePath, options), 0);
    QFile triangleObj(trianglePath);
    QVERIFY(triangleObj.open(QIODevice::ReadOnly));
    const QByteArray triangleData = triangleObj.readAll();
    QVERIFY(triangleData.contains("# Faces: 2"));
    QCOMPARE(triangleData.count("\nf "), 2);

    Document loaded;
    loaded.setPreferredImportPluginForExtension(QStringLiteral("obj"), QStringLiteral("io_vcg"));
    QCOMPARE(loaded.loadMesh(path), 0);
    QCOMPARE(loaded.mesh(0).polygonFaceCount, 1);
    const VCGMesh &roundTrip = loaded.mesh(0).mesh;
    QCOMPARE(roundTrip.FN(), 2);
    int fauxEdges = 0;
    for (const VCGFace &face : roundTrip.face) {
        for (int corner = 0; corner < 3; ++corner) {
            const int vertex = int(face.cV(corner) - roundTrip.vert.data());
            QCOMPARE(face.cWT(corner).U(), uv[size_t(vertex)].X());
            QCOMPARE(face.cWT(corner).V(), uv[size_t(vertex)].Y());
            fauxEdges += face.IsF(corner) ? 1 : 0;
        }
    }
    QCOMPARE(fauxEdges, 2);

    const auto checkPly = [&](bool binary, bool polygonal) {
        options.mask = vcg::tri::io::Mask::IOM_VERTCOORD
            | vcg::tri::io::Mask::IOM_FACEINDEX
            | vcg::tri::io::Mask::IOM_WEDGTEXCOORD
            | (polygonal ? vcg::tri::io::Mask::IOM_BITPOLYGONAL : 0);
        options.binary = binary;
        const QString plyPath = dir.filePath(
            QStringLiteral("quad_%1_%2.ply")
                .arg(binary ? QStringLiteral("binary") : QStringLiteral("ascii"))
                .arg(polygonal ? QStringLiteral("polygon") : QStringLiteral("triangles")));
        QCOMPARE(doc.saveMesh(meshIndex, plyPath, options), 0);
        QFile ply(plyPath);
        QVERIFY(ply.open(QIODevice::ReadOnly));
        const QByteArray data = ply.readAll();
        QVERIFY(data.contains(binary ? "format binary_little_endian 1.0" : "format ascii 1.0"));
        QVERIFY(data.contains(polygonal ? "element face 1\n" : "element face 2\n"));

        Document loadedPly;
        loadedPly.setPreferredImportPluginForExtension(QStringLiteral("ply"), QStringLiteral("io_vcg"));
        QCOMPARE(loadedPly.loadMesh(plyPath), 0);
        QCOMPARE(loadedPly.mesh(0).polygonFaceCount, polygonal ? 1 : -1);
        const VCGMesh &plyMesh = loadedPly.mesh(0).mesh;
        QCOMPARE(plyMesh.FN(), 2);
        int plyFauxEdges = 0;
        for (const VCGFace &face : plyMesh.face)
            for (int corner = 0; corner < 3; ++corner) {
                const int vertex = int(face.cV(corner) - plyMesh.vert.data());
                QCOMPARE(face.cWT(corner).U(), uv[size_t(vertex)].X());
                QCOMPARE(face.cWT(corner).V(), uv[size_t(vertex)].Y());
                plyFauxEdges += face.IsF(corner) ? 1 : 0;
            }
        QCOMPARE(plyFauxEdges, polygonal ? 2 : 0);
    };
    checkPly(false, true);
    checkPly(false, false);
    checkPly(true, true);
    checkPly(true, false);

    options.mask = vcg::tri::io::Mask::IOM_VERTCOORD
        | vcg::tri::io::Mask::IOM_FACEINDEX
        | vcg::tri::io::Mask::IOM_BITPOLYGONAL;
    const QString polygonOffPath = dir.filePath(QStringLiteral("quad.off"));
    QCOMPARE(doc.saveMesh(meshIndex, polygonOffPath, options), 0);
    QFile polygonOff(polygonOffPath);
    QVERIFY(polygonOff.open(QIODevice::ReadOnly));
    QVERIFY(polygonOff.readAll().contains("OFF\n4 1 0\n"));

    options.mask &= ~vcg::tri::io::Mask::IOM_BITPOLYGONAL;
    const QString triangleOffPath = dir.filePath(QStringLiteral("quad_triangles.off"));
    QCOMPARE(doc.saveMesh(meshIndex, triangleOffPath, options), 0);
    QFile triangleOff(triangleOffPath);
    QVERIFY(triangleOff.open(QIODevice::ReadOnly));
    QVERIFY(triangleOff.readAll().contains("OFF\n4 2 0\n"));
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
