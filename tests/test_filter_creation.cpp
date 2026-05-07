#include <QtTest/QtTest>

#include "document.h"

// Tests every filter that, according to its manifest descriptor, takes no mesh
// as input (inputDomain == None) and produces new meshes (outputDomain ==
// NewMeshes).  Each such filter is run with its default parameter values.
//
// The data-driven pattern (runWithDefaults_data / runWithDefaults) gives every
// filter its own named row in the test output, so it is immediately clear which
// filters are covered and which ones fail.

class FilterCreationTests : public QObject
{
    Q_OBJECT

private slots:
    void runWithDefaults_data();
    void runWithDefaults();
};

// Populate one row per eligible filter.  The row tag is the filter id, which
// Qt Test uses as the sub-test name in output and --testcase selectors.
void FilterCreationTests::runWithDefaults_data()
{
    QTest::addColumn<QString>("key");

    Document doc;
    const std::vector<Document::FilterInfo> infos = doc.filterInfos();
    QVERIFY2(!infos.empty(), "Filter registry must be non-empty");

    for (const auto &info : infos) {
        if (info.descriptor.inputDomain != MeshFilterInputDomain::None)
            continue;
        if (info.descriptor.outputDomain != MeshFilterOutputDomain::NewMeshes)
            continue;
        QTest::newRow(qPrintable(info.descriptor.id)) << info.key;
    }
}

void FilterCreationTests::runWithDefaults()
{
    QFETCH(QString, key);

    Document doc;

    // Locate the FilterInfo for this filter to check applicability.
    const std::vector<Document::FilterInfo> infos = doc.filterInfos();
    const auto it = std::find_if(infos.begin(), infos.end(),
                                 [&](const Document::FilterInfo &fi) { return fi.key == key; });
    QVERIFY2(it != infos.end(), qPrintable("Filter key not found in registry: " + key));

    QVERIFY2(it->applicable,
             qPrintable(QStringLiteral("Filter should be applicable with no meshes loaded: ")
                        + it->applicabilityError));

    const int meshCountBefore = doc.meshCount();
    const MeshFilterRunResult result = doc.runFilter(key, {});

    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QVERIFY2(!result.newMeshIndices.isEmpty(), "Filter succeeded but created no new meshes");
    QVERIFY(doc.meshCount() > meshCountBefore);

    for (int idx : result.newMeshIndices) {
        QVERIFY2(idx >= 0 && idx < doc.meshCount(),
                 qPrintable(QStringLiteral("Returned out-of-range mesh index %1").arg(idx)));
        QVERIFY2(doc.mesh(idx).mesh.VN() > 0, "New mesh has no vertices");
    }
}

QTEST_MAIN(FilterCreationTests)
#include "test_filter_creation.moc"
