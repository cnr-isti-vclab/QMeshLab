#include <QtTest/QtTest>

#include "document.h"

class FilterTests : public QObject
{
    Q_OBJECT

private slots:
    void filterRegistryExposesBuiltins();
    void filterApplicabilityReflectsDocumentState();
    void basicFiltersRunOnLoadedMesh();
    void filterParameterValidation();
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
    for (const auto &info : infos) {
        hasMeshInfo = hasMeshInfo || (info.descriptor.id == QStringLiteral("mesh_info"));
        hasNormalize = hasNormalize || (info.descriptor.id == QStringLiteral("normalize_unit_box"));
        hasDuplicate = hasDuplicate || (info.descriptor.id == QStringLiteral("duplicate_current_mesh"));
        hasCreateIso = hasCreateIso || (info.descriptor.id == QStringLiteral("create_noisy_isosurface"));
    }

    QVERIFY(hasMeshInfo);
    QVERIFY(hasNormalize);
    QVERIFY(hasDuplicate);
    QVERIFY(hasCreateIso);
}

void FilterTests::filterApplicabilityReflectsDocumentState()
{
    Document doc;
    QString createIsoKey;

    {
        const std::vector<Document::FilterInfo> infos = doc.filterInfos();
        QVERIFY(!infos.empty());
        for (const auto &info : infos) {
            if (info.descriptor.id == QStringLiteral("create_noisy_isosurface")) {
                createIsoKey = info.key;
                QVERIFY(info.applicable);
                QVERIFY(info.applicabilityError.trimmed().isEmpty());
            } else {
                QVERIFY(!info.applicable);
                QVERIFY(!info.applicabilityError.trimmed().isEmpty());
            }
        }
    }
    QVERIFY(!createIsoKey.isEmpty());
    {
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

    {
        const std::vector<Document::FilterInfo> infos = doc.filterInfos();
        QVERIFY(!infos.empty());
        for (const auto &info : infos)
            QVERIFY(info.applicable);
    }
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
        else if (info.descriptor.id == QStringLiteral("duplicate_current_mesh"))
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
        MeshFilterParameterValues params;
        params.insert(QStringLiteral("name_suffix"), QStringLiteral("_dup"));
        const MeshFilterRunResult result = doc.runFilter(duplicateKey, params);
        QVERIFY(result.success);
        QVERIFY(result.documentModified);
        QCOMPARE(result.newMeshIndices.size(), 1);
    }
    QCOMPARE(doc.meshCount(), meshCountBeforeDuplicate + 1);
    QVERIFY(doc.mesh(doc.currentMeshIndex()).name.endsWith(QStringLiteral("_dup")));

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

QTEST_MAIN(FilterTests)
#include "test_filters.moc"
