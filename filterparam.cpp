#include "filterparam.h"

FilterParams::FilterParams(const MeshFilterParameterValues &values)
    : m_values(values)
{
}

bool FilterParams::getBool(const QString &id) const
{
    return getBool(id, false);
}

int FilterParams::getInt(const QString &id) const
{
    return getInt(id, 0);
}

double FilterParams::getDouble(const QString &id) const
{
    return getDouble(id, 0.0);
}

QString FilterParams::getString(const QString &id) const
{
    return getString(id, QString());
}

QString FilterParams::getEnum(const QString &id) const
{
    return getEnum(id, QString());
}

QColor FilterParams::getColor(const QString &id) const
{
    return getColor(id, QColor(Qt::white));
}

bool FilterParams::getBool(const QString &id, bool fallback) const
{
    const auto it = m_values.constFind(id);
    if (it == m_values.constEnd())
        return fallback;
    if (it.value().userType() == QMetaType::Bool)
        return it.value().toBool();
    const QString text = it.value().toString().trimmed().toLower();
    if (text == QStringLiteral("true") || text == QStringLiteral("1"))
        return true;
    if (text == QStringLiteral("false") || text == QStringLiteral("0"))
        return false;
    return fallback;
}

int FilterParams::getInt(const QString &id, int fallback) const
{
    const auto it = m_values.constFind(id);
    if (it == m_values.constEnd())
        return fallback;
    bool ok = false;
    const int v = it.value().toInt(&ok);
    return ok ? v : fallback;
}

double FilterParams::getDouble(const QString &id, double fallback) const
{
    const auto it = m_values.constFind(id);
    if (it == m_values.constEnd())
        return fallback;
    bool ok = false;
    const double v = it.value().toDouble(&ok);
    return ok ? v : fallback;
}

QString FilterParams::getString(const QString &id, const QString &fallback) const
{
    const auto it = m_values.constFind(id);
    if (it == m_values.constEnd())
        return fallback;
    return it.value().toString();
}

QString FilterParams::getEnum(const QString &id, const QString &fallback) const
{
    return getString(id, fallback);
}

QColor FilterParams::getColor(const QString &id, const QColor &fallback) const
{
    const auto it = m_values.constFind(id);
    if (it == m_values.constEnd())
        return fallback;
    if (it.value().userType() == QMetaType::QColor)
        return it.value().value<QColor>();
    const QColor c(it.value().toString().trimmed());
    return c.isValid() ? c : fallback;
}
