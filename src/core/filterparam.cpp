#include "filterparam.h"

#include <QVector3D>

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

int FilterParams::getMesh(const QString &id) const
{
    return getMesh(id, -1);
}

double FilterParams::getDouble(const QString &id) const
{
    return getDouble(id, 0.0);
}

QString FilterParams::getString(const QString &id) const
{
    return getString(id, QString());
}

QString FilterParams::getFileOpen(const QString &id) const
{
    return getFileOpen(id, QString());
}

QString FilterParams::getFileSave(const QString &id) const
{
    return getFileSave(id, QString());
}

int FilterParams::getTextureRef(const QString &id) const
{
    return getTextureRef(id, 0);
}

TextureOutputRefValue FilterParams::getTextureOutputRef(const QString &id) const
{
    return getTextureOutputRef(id, {});
}

QString FilterParams::getEnum(const QString &id) const
{
    return getEnum(id, QString());
}

QColor FilterParams::getColor(const QString &id) const
{
    return getColor(id, QColor(Qt::white));
}

QVector3D FilterParams::getPoint3f(const QString &id) const
{
    return getPoint3f(id, QVector3D(0.0f, 0.0f, 0.0f));
}

QString FilterParams::getCameraState(const QString &id) const
{
    return getCameraState(id, QString());
}

QString FilterParams::getRenderState(const QString &id) const
{
    return getRenderState(id, QString());
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

int FilterParams::getMesh(const QString &id, int fallback) const
{
    return getInt(id, fallback);
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

QString FilterParams::getFileOpen(const QString &id, const QString &fallback) const
{
    return getString(id, fallback);
}

QString FilterParams::getFileSave(const QString &id, const QString &fallback) const
{
    return getString(id, fallback);
}

int FilterParams::getTextureRef(const QString &id, int fallback) const
{
    return getInt(id, fallback);
}

TextureOutputRefValue FilterParams::getTextureOutputRef(
    const QString &id,
    const TextureOutputRefValue &fallback) const
{
    const auto it = m_values.constFind(id);
    if (it == m_values.constEnd())
        return fallback;

    TextureOutputRefValue out = fallback;
    const QVariantMap map = it.value().toMap();
    if (map.isEmpty()) {
        const QString path = it.value().toString().trimmed();
        if (!path.isEmpty()) {
            out.overwriteExisting = false;
            out.textureSlot = -1;
            out.filePath = path;
        }
        return out;
    }

    const QString mode = map.value(QStringLiteral("mode")).toString().trimmed().toLower();
    if (mode == QStringLiteral("existing")) {
        bool ok = false;
        const int oneBasedSlot = map.value(QStringLiteral("slot")).toInt(&ok);
        if (ok && oneBasedSlot > 0) {
            out.overwriteExisting = true;
            out.textureSlot = oneBasedSlot - 1;
            out.filePath.clear();
            return out;
        }
    }

    const QString path = map.value(QStringLiteral("path")).toString().trimmed();
    if (!path.isEmpty()) {
        out.overwriteExisting = false;
        out.textureSlot = -1;
        out.filePath = path;
    }
    return out;
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

QVector3D FilterParams::getPoint3f(const QString &id, const QVector3D &fallback) const
{
    const auto it = m_values.constFind(id);
    if (it == m_values.constEnd())
        return fallback;
    if (it.value().userType() == QMetaType::QVector3D)
        return it.value().value<QVector3D>();
    // Accept "x,y,z" string encoding (used in tests / scripting)
    const QStringList parts = it.value().toString().trimmed().split(QLatin1Char(','));
    if (parts.size() == 3) {
        bool okX = false, okY = false, okZ = false;
        const float x = parts[0].trimmed().toFloat(&okX);
        const float y = parts[1].trimmed().toFloat(&okY);
        const float z = parts[2].trimmed().toFloat(&okZ);
        if (okX && okY && okZ)
            return QVector3D(x, y, z);
    }
    return fallback;
}

QString FilterParams::getCameraState(const QString &id, const QString &fallback) const
{
    return getString(id, fallback);
}

QString FilterParams::getRenderState(const QString &id, const QString &fallback) const
{
    return getString(id, fallback);
}
