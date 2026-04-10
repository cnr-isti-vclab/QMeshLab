#pragma once

#include "vcgmesh.h"
#include <QFile>
#include <QVector3D>
#include <rhi/qshader.h>
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

inline constexpr int kUbufSize = 304;
inline constexpr int kUbufFloatCount = kUbufSize / sizeof(float);
inline constexpr int kUbufBBoxColorOffset = 176 / sizeof(float);
inline constexpr int kUbufPointColorOffset = 192 / sizeof(float);
inline constexpr int kUbufPointParamsOffset = 208 / sizeof(float);
inline constexpr int kUbufWireColorOffset = 224 / sizeof(float);
inline constexpr int kUbufWireParamsOffset = 240 / sizeof(float);
inline constexpr int kUbufFillColorOffset = 256 / sizeof(float);
inline constexpr int kUbufLightingParamsOffset = 272 / sizeof(float);
inline constexpr int kUbufEdgeColorOffset = 288 / sizeof(float);
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
inline constexpr int kTrackballGizmoUbufSize = 80; // mat4 mvp + vec4(center.xyz, radius)
inline constexpr int kTrackballGizmoSteps = 96;
inline constexpr int kWireframeDefaultFaceThreshold = 10000;
inline constexpr float kPi = 3.14159265358979323846f;

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
