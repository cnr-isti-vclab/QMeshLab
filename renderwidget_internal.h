#pragma once

#include "renderingsettings.h"
#include "vcgmesh.h"
#include <QFile>
#include <QSize>
#include <QtGlobal>
#include <QVector3D>
#include <rhi/qshader.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace RenderWidgetInternal {

inline QShader loadShader(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("Failed to open shader: %s", qPrintable(path));
        return {};
    }
    return QShader::fromSerialized(f.readAll());
}

inline constexpr int kUbufSize = 336;
inline constexpr int kUbufFloatCount = kUbufSize / sizeof(float);
inline constexpr int kUbufBBoxColorOffset = 176 / sizeof(float);
inline constexpr int kUbufPointColorOffset = 192 / sizeof(float);
inline constexpr int kUbufPointParamsOffset = 208 / sizeof(float);
inline constexpr int kUbufWireColorOffset = 224 / sizeof(float);
inline constexpr int kUbufWireParamsOffset = 240 / sizeof(float);
inline constexpr int kUbufFillColorOffset = 256 / sizeof(float);
inline constexpr int kUbufLightingParamsOffset = 272 / sizeof(float);
inline constexpr int kUbufEdgeColorOffset = 288 / sizeof(float);
inline constexpr int kUbufPbrMapUsageOffset = 304 / sizeof(float);
inline constexpr int kUbufPbrParamsOffset = 320 / sizeof(float);
inline constexpr int kFillVertexStrideFloats = 13;
inline constexpr int kPointsVertexStrideFloats = 11;
inline constexpr int kMaskMorphUbufSize = 16;
inline constexpr int kMaskDebugUbufSize = 16;
inline constexpr int kOutlineExtractUbufSize = 16;
inline constexpr int kOutlineUbufSize = 48;
inline constexpr int kDecoratorUbufSize = 80; // mat4 mvp + vec4 color
inline constexpr int kDecoratorFatUbufSize = 96; // mat4 mvp + vec4 color + vec4(width, invW, invH, _)
inline constexpr int kDecoratorSlotVertexNormals = 0;
inline constexpr int kDecoratorSlotFaceNormals = 1;
inline constexpr int kDecoratorSlotBoundaryEdges = 2;
inline constexpr int kDecoratorSlotTextureSeams = 3;
inline constexpr int kDecoratorSlotCount = 4;
inline constexpr int kTrackballGizmoUbufSize = 96; // mat4 mvp + vec4(center.xyz, radius) + vec4(camera.xyz, backShade)
inline constexpr int kTrackballGizmoSteps = 96;
inline constexpr int kWireframeDefaultFaceThreshold = 10000;
inline constexpr float kPi = 3.14159265358979323846f;

struct MainStyleUbufKey {
    QColor bboxWireColor;
    QColor pointColor;
    float pointSize = 0.0f;
    QColor wireColor;
    float wireSize = 0.0f;
    QColor fillColor;
    bool pointLighting = false;
    bool wireLighting = false;
    bool fillLighting = false;
    FillMaterial fillMaterial = FillMaterial::Plain;
    FillPbrTextureSource fillPbrAlbedoSource = FillPbrTextureSource::Texture;
    FillPbrTextureSource fillPbrNormalSource = FillPbrTextureSource::Texture;
    FillPbrTextureSource fillPbrOcclusionSource = FillPbrTextureSource::Texture;
    FillPbrTextureSource fillPbrRoughnessSource = FillPbrTextureSource::Texture;
    FillColorSource fillColorSource = FillColorSource::Constant;
    float fillNormalMapScale = 1.0f;
    float fillOcclusionStrength = 1.0f;
    float fillRoughnessFactor = 1.0f;
    QColor edgeColor;
    float edgeSize = 0.0f;

    bool operator==(const MainStyleUbufKey &other) const
    {
        return bboxWireColor == other.bboxWireColor
            && pointColor == other.pointColor
            && pointSize == other.pointSize
            && wireColor == other.wireColor
            && wireSize == other.wireSize
            && fillColor == other.fillColor
            && pointLighting == other.pointLighting
            && wireLighting == other.wireLighting
            && fillLighting == other.fillLighting
            && fillMaterial == other.fillMaterial
            && fillPbrAlbedoSource == other.fillPbrAlbedoSource
            && fillPbrNormalSource == other.fillPbrNormalSource
            && fillPbrOcclusionSource == other.fillPbrOcclusionSource
            && fillPbrRoughnessSource == other.fillPbrRoughnessSource
            && fillColorSource == other.fillColorSource
            && fillNormalMapScale == other.fillNormalMapScale
            && fillOcclusionStrength == other.fillOcclusionStrength
            && fillRoughnessFactor == other.fillRoughnessFactor
            && edgeColor == other.edgeColor
            && edgeSize == other.edgeSize;
    }
};

inline MainStyleUbufKey mainStyleUbufKeyFromSettings(
    const RenderSettings &settings,
    bool includeLighting = true)
{
    MainStyleUbufKey key;
    key.bboxWireColor = settings.bboxWireColor;
    key.pointColor = settings.pointColor;
    key.pointSize = settings.pointSize;
    key.wireColor = settings.wireColor;
    key.wireSize = settings.wireSize;
    key.fillColor = settings.fillColor;
    key.pointLighting = includeLighting ? settings.pointLighting : false;
    key.wireLighting = includeLighting ? settings.wireLighting : false;
    key.fillLighting = includeLighting ? settings.fillLighting : false;
    key.fillMaterial = settings.fillMaterial;
    key.fillPbrAlbedoSource = settings.fillPbrAlbedoSource;
    key.fillPbrNormalSource = settings.fillPbrNormalSource;
    key.fillPbrOcclusionSource = settings.fillPbrOcclusionSource;
    key.fillPbrRoughnessSource = settings.fillPbrRoughnessSource;
    key.fillColorSource = settings.fillColorSource;
    key.fillNormalMapScale = settings.fillNormalMapScale;
    key.fillOcclusionStrength = settings.fillOcclusionStrength;
    key.fillRoughnessFactor = settings.fillRoughnessFactor;
    key.edgeColor = settings.edgeColor;
    key.edgeSize = settings.edgeSize;
    return key;
}

inline void writeMainStyleToUbuf(
    float *ubufData,
    const RenderSettings &settings,
    const QSize &pixelSize,
    bool enableLighting)
{
    ubufData[kUbufBBoxColorOffset + 0] = settings.bboxWireColor.redF();
    ubufData[kUbufBBoxColorOffset + 1] = settings.bboxWireColor.greenF();
    ubufData[kUbufBBoxColorOffset + 2] = settings.bboxWireColor.blueF();
    ubufData[kUbufBBoxColorOffset + 3] = settings.bboxWireColor.alphaF();

    ubufData[kUbufPointColorOffset + 0] = settings.pointColor.redF();
    ubufData[kUbufPointColorOffset + 1] = settings.pointColor.greenF();
    ubufData[kUbufPointColorOffset + 2] = settings.pointColor.blueF();
    ubufData[kUbufPointColorOffset + 3] = settings.pointColor.alphaF();
    ubufData[kUbufPointParamsOffset + 0] = settings.pointSize;

    ubufData[kUbufWireColorOffset + 0] = settings.wireColor.redF();
    ubufData[kUbufWireColorOffset + 1] = settings.wireColor.greenF();
    ubufData[kUbufWireColorOffset + 2] = settings.wireColor.blueF();
    // Wire pass is intentionally translucent to compose independently over fill/effects.
    ubufData[kUbufWireColorOffset + 3] = settings.wireColor.alphaF() * 0.7f;
    ubufData[kUbufWireParamsOffset + 0] = settings.wireSize;
    ubufData[kUbufWireParamsOffset + 1] = qMax(1.0f, settings.edgeSize);
    ubufData[kUbufWireParamsOffset + 2] = 1.0f / float(qMax(1, pixelSize.width()));
    ubufData[kUbufWireParamsOffset + 3] = 1.0f / float(qMax(1, pixelSize.height()));

    ubufData[kUbufFillColorOffset + 0] = settings.fillColor.redF();
    ubufData[kUbufFillColorOffset + 1] = settings.fillColor.greenF();
    ubufData[kUbufFillColorOffset + 2] = settings.fillColor.blueF();
    ubufData[kUbufFillColorOffset + 3] = settings.fillColor.alphaF();

    // bbox lighting removed: slot 0 intentionally unused/reserved.
    ubufData[kUbufLightingParamsOffset + 0] = 0.0f;
    ubufData[kUbufLightingParamsOffset + 1] = (enableLighting && settings.pointLighting) ? 1.0f : 0.0f;
    ubufData[kUbufLightingParamsOffset + 2] = (enableLighting && settings.wireLighting) ? 1.0f : 0.0f;
    ubufData[kUbufLightingParamsOffset + 3] = (enableLighting && settings.fillLighting) ? 1.0f : 0.0f;

    ubufData[kUbufEdgeColorOffset + 0] = settings.edgeColor.redF();
    ubufData[kUbufEdgeColorOffset + 1] = settings.edgeColor.greenF();
    ubufData[kUbufEdgeColorOffset + 2] = settings.edgeColor.blueF();
    ubufData[kUbufEdgeColorOffset + 3] = settings.edgeColor.alphaF();

    const bool enablePbr = settings.fillMaterial == FillMaterial::Pbr;
    auto encodePbrSource = [enablePbr](FillPbrTextureSource source) -> float {
        if (!enablePbr)
            return 0.0f;
        return static_cast<float>(static_cast<int>(source));
    };
    ubufData[kUbufPbrMapUsageOffset + 0] = encodePbrSource(settings.fillPbrNormalSource);
    ubufData[kUbufPbrMapUsageOffset + 1] = encodePbrSource(settings.fillPbrOcclusionSource);
    ubufData[kUbufPbrMapUsageOffset + 2] = encodePbrSource(settings.fillPbrRoughnessSource);
    ubufData[kUbufPbrMapUsageOffset + 3] = encodePbrSource(settings.fillPbrAlbedoSource);

    ubufData[kUbufPbrParamsOffset + 0] = settings.fillNormalMapScale;
    ubufData[kUbufPbrParamsOffset + 1] = std::clamp(settings.fillOcclusionStrength, 0.0f, 1.0f);
    ubufData[kUbufPbrParamsOffset + 2] = std::max(settings.fillRoughnessFactor, 0.0f);
    ubufData[kUbufPbrParamsOffset + 3] = 0.0f;
}

inline QVector3D toVec3(const VCGMesh::CoordType &p)
{
    return QVector3D(p[0], p[1], p[2]);
}

inline float decodePackedDepthRgb8(const uchar *px, bool bgraOrder)
{
    const float c0 = (bgraOrder ? px[2] : px[0]) / 255.0f;
    const float c1 = px[1] / 255.0f;
    const float c2 = (bgraOrder ? px[0] : px[2]) / 255.0f;
    return c0 + c1 / 255.0f + c2 / 65025.0f;
}

inline std::vector<float> buildTrackballGizmoVertices()
{
    std::vector<float> v;
    v.reserve(kTrackballGizmoSteps * 2 * 3 * 6);

    auto append = [&v](const QVector3D &p, const QVector3D &c) {
        v.push_back(p.x());
        v.push_back(p.y());
        v.push_back(p.z());
        v.push_back(c.x());
        v.push_back(c.y());
        v.push_back(c.z());
    };

    auto emitCircle = [&](int axis, const QVector3D &color) {
        // axis: 0=XY(z=0), 1=YZ(x=0), 2=XZ(y=0)
        for (int i = 0; i < kTrackballGizmoSteps; ++i) {
            const float t0 = float(i) * 2.0f * kPi / float(kTrackballGizmoSteps);
            const float t1 = float(i + 1) * 2.0f * kPi / float(kTrackballGizmoSteps);
            QVector3D p0, p1;
            if (axis == 0) {
                p0 = QVector3D(std::cos(t0), std::sin(t0), 0.0f);
                p1 = QVector3D(std::cos(t1), std::sin(t1), 0.0f);
            } else if (axis == 1) {
                p0 = QVector3D(0.0f, std::cos(t0), std::sin(t0));
                p1 = QVector3D(0.0f, std::cos(t1), std::sin(t1));
            } else {
                p0 = QVector3D(std::cos(t0), 0.0f, std::sin(t0));
                p1 = QVector3D(std::cos(t1), 0.0f, std::sin(t1));
            }
            append(p0, color);
            append(p1, color);
        }
    };

    emitCircle(0, QVector3D(0.40f, 0.40f, 0.85f)); // XY - blue-ish
    emitCircle(1, QVector3D(0.40f, 0.85f, 0.40f)); // YZ - green-ish
    emitCircle(2, QVector3D(0.85f, 0.40f, 0.40f)); // XZ - red-ish
    return v;
}

inline const std::vector<float> &trackballGizmoVertices()
{
    static const std::vector<float> kVerts = buildTrackballGizmoVertices();
    return kVerts;
}

} // namespace RenderWidgetInternal
