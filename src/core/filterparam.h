#pragma once

#include "meshfilterplugin.h"
#include <QColor>
#include <QStringList>
#include <QVector3D>

struct TextureOutputRefValue
{
    bool overwriteExisting = false;
    int textureSlot = -1; // zero-based when overwriteExisting is true
    QString filePath;
};

// Typed wrapper around a pre-normalized MeshFilterParameterValues map.
// The manager guarantees every declared parameter is present with a valid value
// before runFilter is called, so the no-fallback overloads are always safe.
class FilterParams
{
public:
    explicit FilterParams(const MeshFilterParameterValues &values);

    // Typed access — no fallback (parameter must be present, i.e. declared in descriptor)
    bool    getBool  (const QString &id) const;
    int     getInt   (const QString &id) const;
    int     getMesh  (const QString &id) const;
    double  getDouble(const QString &id) const;
    QString getString(const QString &id) const;
    QString getFileOpen(const QString &id) const;
    QString getFileSave(const QString &id) const;
    int     getTextureRef(const QString &id) const;
    TextureOutputRefValue getTextureOutputRef(const QString &id) const;
    QString getEnum  (const QString &id) const;  // returns the selected option id
    QColor  getColor (const QString &id) const;
    QVector3D getPoint3f(const QString &id) const;
    QString getCameraState(const QString &id) const;
    QString getRenderState(const QString &id) const;

    // Typed access with explicit fallback (use when parameter may be absent)
    bool    getBool  (const QString &id, bool fallback) const;
    int     getInt   (const QString &id, int fallback) const;
    int     getMesh  (const QString &id, int fallback) const;
    double  getDouble(const QString &id, double fallback) const;
    QString getString(const QString &id, const QString &fallback) const;
    QString getFileOpen(const QString &id, const QString &fallback) const;
    QString getFileSave(const QString &id, const QString &fallback) const;
    int     getTextureRef(const QString &id, int fallback) const;
    TextureOutputRefValue getTextureOutputRef(
        const QString &id,
        const TextureOutputRefValue &fallback) const;
    QString getEnum  (const QString &id, const QString &fallback) const;
    QColor  getColor (const QString &id, const QColor &fallback) const;
    QVector3D getPoint3f(const QString &id, const QVector3D &fallback) const;
    QString getCameraState(const QString &id, const QString &fallback) const;
    QString getRenderState(const QString &id, const QString &fallback) const;

    // Access to the underlying map (for forwarding to functions that still use the raw map)
    const MeshFilterParameterValues &rawValues() const { return m_values; }

private:
    const MeshFilterParameterValues &m_values;
};

// Convert a QVariant to a Python-compatible literal string
QString filterParamValueToPythonLiteral(const QVariant &v,
                                         MeshFilterParameterType type);

// Return "key=value" entries for parameters whose value differs from
// the descriptor's defaultValue (i.e. non-default values only).
QStringList nonDefaultFilterParamsToPython(
    const MeshFilterDescriptor &descriptor,
    const MeshFilterParameterValues &values);
