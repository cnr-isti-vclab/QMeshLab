// Round-trip coverage for the render-settings serialisation.
//
// Every field of GlobalRenderSettings / PerMeshRenderSettings has to be written out
// by hand in four places: the struct, operator==, the JSON writer and the JSON
// reader. Nothing ties those lists together, so a field added to the struct and
// forgotten in the writer silently drops out of every saved render state — which is
// exactly what had happened to showViewCameras and fillTextureNearestSampling.
//
// These tests are the tie. They need no per-field code: the writer's own output is
// used to drive the reader, so any field that writes but does not read back (or reads
// back into the wrong member) shows up as a mismatch, and the key count catches
// fields that never write at all.
#include "rendersettingsjson.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

using namespace RenderSettingsJson;

namespace {

// Change a value into a different, still-valid one of the same JSON type. Numbers
// move by -1 rather than +1 because enum-valued keys sit at the top of their range
// more often than the bottom, and an out-of-range enum would not survive the reader.
QJsonValue perturb(const QJsonValue &v)
{
    if (v.isBool())
        return !v.toBool();
    if (v.isDouble()) {
        const double d = v.toDouble();
        return d > 0.0 ? d - 1.0 : d + 1.0;
    }
    if (v.isString())
        return v.toString() + QStringLiteral("_x");
    if (v.isArray()) {
        // Colors: 4 components in 0..255, so move each towards the middle.
        QJsonArray out;
        for (const QJsonValue &e : v.toArray())
            out.append(e.isDouble() ? QJsonValue(std::fmod(e.toDouble() + 37.0, 256.0)) : e);
        return out;
    }
    return v;
}

bool sameJsonValue(const QJsonValue &a, const QJsonValue &b);

template <typename Settings, typename ToJson, typename Parse>
void checkRoundTrip(ToJson toJson, Parse parse, const char *label)
{
    // Passing no defaults makes the writer emit every field it knows about.
    const QJsonObject baseline = toJson(Settings{}, nullptr);
    QVERIFY2(!baseline.isEmpty(), label);

    QJsonObject mutated;
    for (auto it = baseline.begin(); it != baseline.end(); ++it)
        mutated.insert(it.key(), perturb(it.value()));

    Settings parsed;
    QString error;
    QVERIFY2(parse(mutated, parsed, &error), qPrintable(QStringLiteral("%1: %2").arg(label, error)));

    const QJsonObject reserialised = toJson(parsed, nullptr);

    // Compare key by key so a failure names the field instead of dumping two blobs.
    for (auto it = mutated.begin(); it != mutated.end(); ++it) {
        QVERIFY2(
            reserialised.contains(it.key()),
            qPrintable(QStringLiteral("%1: '%2' was written but did not survive the round trip")
                           .arg(label, it.key())));
        QVERIFY2(
            sameJsonValue(reserialised.value(it.key()), it.value()),
            qPrintable(QStringLiteral("%1: '%2' changed across the round trip").arg(label, it.key())));
    }
    QCOMPARE(reserialised.size(), mutated.size());
}

// The settings store float, JSON carries double, so a value that has been through a
// float compares unequal as a double. Compare numbers at float precision.
bool sameJsonValue(const QJsonValue &a, const QJsonValue &b)
{
    if (a.isDouble() && b.isDouble())
        return fuzzyFloatEqual(float(a.toDouble()), float(b.toDouble()));
    if (a.isArray() && b.isArray()) {
        const QJsonArray aa = a.toArray();
        const QJsonArray bb = b.toArray();
        if (aa.size() != bb.size())
            return false;
        for (int i = 0; i < aa.size(); ++i) {
            if (!sameJsonValue(aa[i], bb[i]))
                return false;
        }
        return true;
    }
    return a == b;
}

} // namespace

class RenderSettingsJsonTests : public QObject
{
    Q_OBJECT
private slots:
    void globalSettingsSerialiseEveryField();
    void perMeshSettingsSerialiseEveryField();
    void globalSettingsRoundTrip();
    void perMeshSettingsRoundTrip();
    void defaultsAreOmittedWhenDefaultsGiven();
};

// The count is the tie between the struct and the writer: add a field without adding
// it to globalSettingsToJson and this fails instead of the field quietly vanishing
// from every stored render state.
void RenderSettingsJsonTests::globalSettingsSerialiseEveryField()
{
    const QJsonObject all = globalSettingsToJson(GlobalRenderSettings{}, nullptr);
    QCOMPARE(all.size(), kGlobalSettingsFieldCount);
}

void RenderSettingsJsonTests::perMeshSettingsSerialiseEveryField()
{
    const QJsonObject all = perMeshSettingsToJson(PerMeshRenderSettings{}, nullptr);
    QCOMPARE(all.size(), kPerMeshSettingsFieldCount);
}

void RenderSettingsJsonTests::globalSettingsRoundTrip()
{
    checkRoundTrip<GlobalRenderSettings>(
        [](const GlobalRenderSettings &s, const GlobalRenderSettings *d) {
            return globalSettingsToJson(s, d);
        },
        [](const QJsonObject &o, GlobalRenderSettings &s, QString *e) {
            return parseGlobalSettings(o, s, e);
        },
        "GlobalRenderSettings");
}

void RenderSettingsJsonTests::perMeshSettingsRoundTrip()
{
    checkRoundTrip<PerMeshRenderSettings>(
        [](const PerMeshRenderSettings &s, const PerMeshRenderSettings *d) {
            return perMeshSettingsToJson(s, d);
        },
        [](const QJsonObject &o, PerMeshRenderSettings &s, QString *e) {
            return parsePerMeshSettings(o, s, e);
        },
        "PerMeshRenderSettings");
}

// The sparse form is what keeps stored render states small: identical to the
// defaults means nothing is written.
void RenderSettingsJsonTests::defaultsAreOmittedWhenDefaultsGiven()
{
    const GlobalRenderSettings globalDefaults;
    QVERIFY(globalSettingsToJson(globalDefaults, &globalDefaults).isEmpty());

    const PerMeshRenderSettings meshDefaults;
    QVERIFY(perMeshSettingsToJson(meshDefaults, &meshDefaults).isEmpty());
}

QTEST_MAIN(RenderSettingsJsonTests)
#include "test_rendersettingsjson.moc"
