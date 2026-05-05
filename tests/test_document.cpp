#include <QtTest/QtTest>
#include <QSignalSpy>

#include "document.h"

class DocumentTests : public QObject
{
    Q_OBJECT

private slots:
    void logReplaceLastEntryOnCarriageReturn();
    void loadMeshAddsLayerAndEmitsSignal();
    void undoRedoRestoresMeshList();
    void undoTreeBranchingPreservesAlternateFuture();
    void openDialogFilterContainsKnownFormats();
    void benchmarkLoadMesh();
};

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
