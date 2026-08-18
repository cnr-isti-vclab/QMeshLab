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

// A color map that failed to load. The registry is a Core singleton that loads lazily,
// possibly before any Document exists, so it cannot write to the log itself; it queues
// the issues instead and whoever can put them in front of the user drains them.
struct ColorMapLoadIssue {
    QString message;
    // A bundled map failing means a broken installation, not a bad file of the user's.
    bool bundled = false;
};

class ColorMapRegistry
{
public:
    static ColorMapRegistry &instance();

    void reload();

    // Drains the diagnostics collected while loading, forcing the lazy load first so a
    // caller running at startup sees everything. Also written to the terminal as it
    // happens, which is the only channel in headless runs.
    QVector<ColorMapLoadIssue> takeLoadIssues();

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
    void recordIssue(bool bundled, const QString &message);

    static QVector3D interpolateStops(const QVector<ColorMapStop> &stops, float t);
    static QString normalizedMapId(const QString &id);

    mutable bool m_loaded = false;
    mutable QString m_fallbackMapId;
    mutable QStringList m_order;
    mutable QHash<QString, ColorMapDefinition> m_maps;
    QVector<ColorMapLoadIssue> m_loadIssues;
};
