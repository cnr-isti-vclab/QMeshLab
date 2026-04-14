#pragma once

#include <QColor>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVector3D>

struct ColorMapStop {
    float x = 0.0f;
    QVector3D rgb = QVector3D(1.0f, 1.0f, 1.0f);
};

struct ColorMapDefinition {
    QString id;
    QString name;
    QVector<ColorMapStop> stops;
};

class ColorMapRegistry
{
public:
    static ColorMapRegistry &instance();

    void reload();

    QString fallbackMapId() const;
    QStringList mapIds() const;
    QString displayName(const QString &mapId) const;
    bool hasMap(const QString &mapId) const;

    const ColorMapDefinition *definition(const QString &mapId) const;
    QVector3D sampleRgb(const ColorMapDefinition *definition, float t) const;
    QVector3D sampleRgb(const QString &mapId, float t) const;
    QColor sampleQColor(const ColorMapDefinition *definition, float t, float alpha = 1.0f) const;
    QColor sampleQColor(const QString &mapId, float t, float alpha = 1.0f) const;

private:
    ColorMapRegistry() = default;

    void ensureLoaded() const;
    void loadInternal();
    void loadBundledResourceMaps();
    void loadExternalFolderMaps();
    void ensureFallbackMap();
    bool parseMapFile(
        const QString &path,
        const QByteArray &bytes,
        ColorMapDefinition &outMap,
        QString &error) const;
    bool registerMap(ColorMapDefinition map, bool allowReplace, QString &error);

    static QVector3D interpolateStops(const QVector<ColorMapStop> &stops, float t);
    static QString normalizedMapId(const QString &id);

    mutable bool m_loaded = false;
    mutable QString m_fallbackMapId;
    mutable QStringList m_order;
    mutable QHash<QString, ColorMapDefinition> m_maps;
};
