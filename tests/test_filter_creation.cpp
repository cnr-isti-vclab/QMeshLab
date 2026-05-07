#include <QtTest/QtTest>

#include "document.h"

// Tests every filter that, according to its manifest descriptor, takes no mesh
// as input (inputDomain == None) and produces new meshes (outputDomain ==
// NewMeshes).  Each such filter is run with its default parameter values and
// the test verifies:
//   - the run succeeds
//   - at least one new mesh was added to the document
//   - every new mesh contains at least one vertex

class FilterCreationTests : public QObject
{
    Q_OBJECT

private slots:
    void allNoneToNewMeshFiltersRunWithDefaults();
};

void FilterCreationTests::allNoneToNewMeshFiltersRunWithDefaults()
{
    Document doc;
    const std::vector<Document::FilterInfo> infos = doc.filterInfos();
    QVERIFY2(!infos.empty(), "Filter registry must be non-empty");

    int testedCount = 0;
    QStringList failures;

    for (const auto &info : infos) {
        const MeshFilterDescriptor &desc = info.descriptor;
        if (desc.inputDomain != MeshFilterInputDomain::None)
            continue;
        if (desc.outputDomain != MeshFilterOutputDomain::NewMeshes)
            continue;

        QVERIFY2(info.applicable,
                 qPrintable(QStringLiteral("Filter '%1' (%2) should be applicable with no meshes loaded but reports: %3")
                                .arg(desc.id, desc.name, info.applicabilityError)));

        const int meshCountBefore = doc.meshCount();
        const MeshFilterRunResult result = doc.runFilter(info.key, {});

        if (!result.success) {
            failures << QStringLiteral("[%1] run failed: %2").arg(desc.id, result.errorMessage);
            continue;
        }

        if (result.newMeshIndices.isEmpty()) {
            failures << QStringLiteral("[%1] succeeded but created no new meshes").arg(desc.id);
            continue;
        }

        QVERIFY(doc.meshCount() > meshCountBefore);

        for (int idx : result.newMeshIndices) {
            if (idx < 0 || idx >= doc.meshCount()) {
                failures << QStringLiteral("[%1] returned out-of-range mesh index %2").arg(desc.id).arg(idx);
                continue;
            }
            if (doc.mesh(idx).mesh.VN() <= 0) {
                failures << QStringLiteral("[%1] new mesh at index %2 has no vertices").arg(desc.id).arg(idx);
            }
        }

        ++testedCount;
    }

    if (!failures.isEmpty())
        QFAIL(qPrintable(QStringLiteral("The following creation filters failed:\n  ") + failures.join(QStringLiteral("\n  "))));

    QVERIFY2(testedCount > 0, "No filters with inputDomain=None and outputDomain=NewMeshes were found");
}

QTEST_MAIN(FilterCreationTests)
#include "test_filter_creation.moc"
