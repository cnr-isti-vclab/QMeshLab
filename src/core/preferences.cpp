#include "preferences.h"

#include "filterdescriptorloader.h"

#include <QDebug>
#include <QSettings>

namespace {

constexpr auto kResourcePath = ":/resources/preferences.json";
constexpr auto kSettingsGroup = "preferences";

} // namespace

Preferences::Preferences() = default;

Preferences &Preferences::instance()
{
    static Preferences preferences;
    return preferences;
}

void Preferences::ensureLoaded() const
{
    if (m_loaded)
        return;
    m_loaded = true;

    QString error;
    m_descriptors = FilterDescriptorLoader::loadParameters(QString::fromLatin1(kResourcePath), error);
    if (!error.isEmpty()) {
        qWarning() << "Preferences:" << error;
        return;
    }

    // Stored values are only adopted for ids that are still declared, so removing a
    // preference from the JSON does not leave a stale entry behind.
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    for (const auto &descriptor : m_descriptors) {
        if (settings.contains(descriptor.id))
            m_values.insert(descriptor.id, settings.value(descriptor.id));
    }
    settings.endGroup();
}

const std::vector<MeshFilterParameterDescriptor> &Preferences::descriptors() const
{
    ensureLoaded();
    return m_descriptors;
}

const MeshFilterParameterDescriptor *Preferences::descriptor(const QString &id) const
{
    ensureLoaded();
    for (const auto &d : m_descriptors) {
        if (d.id == id)
            return &d;
    }
    return nullptr;
}

QVariant Preferences::value(const QString &id) const
{
    ensureLoaded();
    const auto it = m_values.constFind(id);
    if (it != m_values.constEnd() && it.value().isValid())
        return it.value();
    if (const MeshFilterParameterDescriptor *d = descriptor(id))
        return d->defaultValue;
    qWarning() << "Preferences: unknown preference" << id;
    return {};
}

bool Preferences::boolValue(const QString &id) const { return value(id).toBool(); }
int Preferences::intValue(const QString &id) const { return value(id).toInt(); }
double Preferences::doubleValue(const QString &id) const { return value(id).toDouble(); }
QString Preferences::stringValue(const QString &id) const { return value(id).toString(); }

void Preferences::setValue(const QString &id, const QVariant &newValue)
{
    ensureLoaded();
    const MeshFilterParameterDescriptor *d = descriptor(id);
    if (!d) {
        qWarning() << "Preferences: refusing to set undeclared preference" << id;
        return;
    }
    // Compare against the effective value, not the stored one: setting a preference
    // back to its default must still count as a change when it had been overridden.
    if (value(id) == newValue)
        return;

    m_values.insert(id, newValue);

    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    settings.setValue(id, newValue);
    settings.endGroup();

    emit changed(id, newValue);
}

void Preferences::setValues(const MeshFilterParameterValues &newValues)
{
    for (auto it = newValues.constBegin(); it != newValues.constEnd(); ++it)
        setValue(it.key(), it.value());
}

MeshFilterParameterValues Preferences::values() const
{
    ensureLoaded();
    MeshFilterParameterValues out;
    for (const auto &d : m_descriptors)
        out.insert(d.id, value(d.id));
    return out;
}

void Preferences::resetToDefaults()
{
    ensureLoaded();
    for (const auto &d : m_descriptors)
        setValue(d.id, d.defaultValue);
}
