#pragma once

#include "camerashot.h"
#include "floatbuffer.h"
#include "vcgmesh.h"

#include <QMatrix4x4>
#include <QVector2D>
#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

inline QVector3D transformPoint(const QMatrix4x4 &m, const vcg::Point3f &p)
{
    return m.map(QVector3D(p[0], p[1], p[2]));
}

inline void rasterizeTriangleDepth(
    std::vector<float> &zbuf, int w, int h,
    const QVector2D &p0, const QVector2D &p1, const QVector2D &p2,
    float d0, float d1, float d2)
{
    if (d0 <= 0.0f && d1 <= 0.0f && d2 <= 0.0f) return;
    const float area = (p1.x() - p0.x()) * (p2.y() - p0.y()) -
                       (p1.y() - p0.y()) * (p2.x() - p0.x());
    if (std::abs(area) < 1e-8f) return;
    const float invArea = 1.0f / area;

    const int ixMin = std::max(0,     int(std::floor(std::min({p0.x(), p1.x(), p2.x()}))));
    const int iyMin = std::max(0,     int(std::floor(std::min({p0.y(), p1.y(), p2.y()}))));
    const int ixMax = std::min(w - 1, int(std::ceil (std::max({p0.x(), p1.x(), p2.x()}))));
    const int iyMax = std::min(h - 1, int(std::ceil (std::max({p0.y(), p1.y(), p2.y()}))));
    if (ixMin > ixMax || iyMin > iyMax) return;

    for (int iy = iyMin; iy <= iyMax; ++iy) {
        for (int ix = ixMin; ix <= ixMax; ++ix) {
            const float px = float(ix) + 0.5f;
            const float py = float(iy) + 0.5f;
            const float f12 = (p2.x() - p1.x()) * (py - p1.y()) -
                              (p2.y() - p1.y()) * (px - p1.x());
            const float f20 = (p0.x() - p2.x()) * (py - p2.y()) -
                              (p0.y() - p2.y()) * (px - p2.x());
            const float f01 = area - f12 - f20;

            const float bw0 = f12 * invArea;
            const float bw1 = f20 * invArea;
            const float bw2 = 1.0f - bw0 - bw1;
            if (bw0 < -1e-5f || bw1 < -1e-5f || bw2 < -1e-5f) continue;
            const float d = bw0 * d0 + bw1 * d1 + bw2 * d2;
            if (d <= 0.0f) continue;
            float &z = zbuf[size_t(iy * w + ix)];
            if (d < z) z = d;
        }
    }
}

inline std::unique_ptr<FloatBuffer> buildDepthBuffer(
    const CameraShot &shot, const VCGMesh &mesh, const QMatrix4x4 &transform)
{
    const int w = shot.viewportPx().width();
    const int h = shot.viewportPx().height();
    const float kMax = std::numeric_limits<float>::max();
    std::vector<float> zbuf(size_t(w * h), kMax);

    for (const VCGFace &f : mesh.face) {
        if (f.IsD()) continue;
        const QVector3D v0 = transformPoint(transform, f.cP(0));
        const QVector3D v1 = transformPoint(transform, f.cP(1));
        const QVector3D v2 = transformPoint(transform, f.cP(2));
        const float d0 = shot.depth(v0);
        const float d1 = shot.depth(v1);
        const float d2 = shot.depth(v2);
        if (d0 <= 0.0f && d1 <= 0.0f && d2 <= 0.0f) continue;
        rasterizeTriangleDepth(zbuf, w, h,
            shot.project(v0), shot.project(v1), shot.project(v2), d0, d1, d2);
    }

    auto buf = std::make_unique<FloatBuffer>();
    buf->init(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            buf->setval(x, y, (zbuf[size_t(y * w + x)] == kMax) ? 0.0f : zbuf[size_t(y * w + x)]);
    return buf;
}
