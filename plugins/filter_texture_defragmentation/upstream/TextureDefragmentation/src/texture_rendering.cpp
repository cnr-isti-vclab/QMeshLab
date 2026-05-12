/*******************************************************************************
    Copyright (c) 2021, Andrea Maggiordomo, Paolo Cignoni and Marco Tarini

    This file is part of TextureDefrag, a reference implementation for
    the paper ``Texture Defragmentation for Photo-Reconstructed 3D Models''.

    QMeshLab adaptation note: the original implementation rasterized the new
    atlas with OpenGL. This file keeps the same public API and provides a small
    QImage CPU renderer. The call boundary is intentionally identical so a QRhi
    backend can replace this renderer without touching the defrag algorithm.
*******************************************************************************/

#include "mesh.h"
#include "texture_rendering.h"
#include "mesh_attribute.h"
#include "pushpull.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

Vec2 outputPixel(const vcg::TexCoord2d &tc, int w, int h)
{
    return { tc.U() * double(w), (1.0 - tc.V()) * double(h) };
}

QRgb sampleNearest(const QImage &image, double uPixel, double vPixel)
{
    if (image.isNull())
        return qRgba(0, 0, 0, 255);
    const int x = std::clamp(int(std::floor(uPixel)), 0, image.width() - 1);
    const int y = std::clamp(int(std::floor(image.height() - 1 - vPixel)), 0, image.height() - 1);
    return image.pixel(x, y);
}

QRgb sampleLinear(const QImage &image, double uPixel, double vPixel)
{
    if (image.isNull())
        return qRgba(0, 0, 0, 255);

    const double x = std::clamp(uPixel - 0.5, 0.0, double(image.width() - 1));
    const double y = std::clamp(double(image.height() - 1) - (vPixel - 0.5), 0.0, double(image.height() - 1));
    const int x0 = int(std::floor(x));
    const int y0 = int(std::floor(y));
    const int x1 = std::min(x0 + 1, image.width() - 1);
    const int y1 = std::min(y0 + 1, image.height() - 1);
    const double tx = x - x0;
    const double ty = y - y0;

    const QRgb c00 = image.pixel(x0, y0);
    const QRgb c10 = image.pixel(x1, y0);
    const QRgb c01 = image.pixel(x0, y1);
    const QRgb c11 = image.pixel(x1, y1);
    auto lerp = [](double a, double b, double t) { return a + (b - a) * t; };
    auto channel = [&](auto getter) -> int {
        const double a = lerp(getter(c00), getter(c10), tx);
        const double b = lerp(getter(c01), getter(c11), tx);
        return std::clamp(int(std::lround(lerp(a, b, ty))), 0, 255);
    };
    return qRgba(
        channel([](QRgb c) { return qRed(c); }),
        channel([](QRgb c) { return qGreen(c); }),
        channel([](QRgb c) { return qBlue(c); }),
        channel([](QRgb c) { return qAlpha(c); }));
}

bool barycentric(
    const Vec2 &p,
    const Vec2 &a,
    const Vec2 &b,
    const Vec2 &c,
    double &wa,
    double &wb,
    double &wc)
{
    const double den = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (std::abs(den) <= std::numeric_limits<double>::epsilon())
        return false;
    wa = ((b.y - c.y) * (p.x - c.x) + (c.x - b.x) * (p.y - c.y)) / den;
    wb = ((c.y - a.y) * (p.x - c.x) + (a.x - c.x) * (p.y - c.y)) / den;
    wc = 1.0 - wa - wb;
    constexpr double eps = -1e-6;
    return wa >= eps && wb >= eps && wc >= eps;
}

std::shared_ptr<QImage> RenderTextureCpu(
    std::vector<Mesh::FacePointer> &faces,
    Mesh &m,
    TextureObjectHandle textureObject,
    bool filter,
    RenderMode mode,
    int textureWidth,
    int textureHeight)
{
    auto originalTc = GetWedgeTexCoordStorageAttribute(m);
    auto result = std::make_shared<QImage>(textureWidth, textureHeight, QImage::Format_ARGB32);
    const QRgb background = qRgba(0, 255, 0, 128);
    result->fill(background);

    for (Mesh::FacePointer fptr : faces) {
        if (!fptr || fptr->IsD())
            continue;
        const int inputTextureIndex = originalTc[fptr].tc[0].N();
        if (inputTextureIndex < 0 || inputTextureIndex >= int(textureObject->texInfoVec.size()))
            continue;
        const QImage &source = textureObject->texInfoVec[size_t(inputTextureIndex)].texture;
        if (source.isNull())
            continue;

        const Vec2 p0 = outputPixel(fptr->cWT(0), textureWidth, textureHeight);
        const Vec2 p1 = outputPixel(fptr->cWT(1), textureWidth, textureHeight);
        const Vec2 p2 = outputPixel(fptr->cWT(2), textureWidth, textureHeight);

        const int minX = std::clamp(int(std::floor(std::min({p0.x, p1.x, p2.x}))) - 1, 0, textureWidth - 1);
        const int maxX = std::clamp(int(std::ceil (std::max({p0.x, p1.x, p2.x}))) + 1, 0, textureWidth - 1);
        const int minY = std::clamp(int(std::floor(std::min({p0.y, p1.y, p2.y}))) - 1, 0, textureHeight - 1);
        const int maxY = std::clamp(int(std::ceil (std::max({p0.y, p1.y, p2.y}))) + 1, 0, textureHeight - 1);

        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                double w0 = 0.0, w1 = 0.0, w2 = 0.0;
                if (!barycentric({x + 0.5, y + 0.5}, p0, p1, p2, w0, w1, w2))
                    continue;

                const vcg::TexCoord2d &tc0 = originalTc[fptr].tc[0];
                const vcg::TexCoord2d &tc1 = originalTc[fptr].tc[1];
                const vcg::TexCoord2d &tc2 = originalTc[fptr].tc[2];
                const double srcU = w0 * tc0.U() + w1 * tc1.U() + w2 * tc2.U();
                const double srcV = w0 * tc0.V() + w1 * tc1.V() + w2 * tc2.V();
                const QRgb color = (mode == Nearest)
                    ? sampleNearest(source, srcU, srcV)
                    : sampleLinear(source, srcU, srcV);
                result->setPixel(x, y, color);
            }
        }
    }

    if (filter && result->width() > 1 && result->height() > 1)
        vcg::PullPush(*result, background);
    return result;
}

} // namespace

int FacesByTextureIndex(Mesh& m, std::vector<std::vector<Mesh::FacePointer>>& fv)
{
    fv.clear();
    int nTex = 1;
    for (auto& f : m.face)
        nTex = std::max(nTex, f.cWT(0).N() + 1);
    fv.resize(nTex);
    for (auto& f : m.face) {
        int ti = f.cWT(0).N();
        ensure(ti < nTex);
        if (ti >= 0)
            fv[ti].push_back(&f);
    }
    return int(fv.size());
}

std::vector<std::shared_ptr<QImage>> RenderTexture(
    Mesh& m,
    TextureObjectHandle textureObject,
    const std::vector<TextureSize> &texSizes,
    bool filter,
    RenderMode imode)
{
    std::vector<std::vector<Mesh::FacePointer>> facesByTexture;
    int nTex = FacesByTextureIndex(m, facesByTexture);
    ensure(nTex <= int(texSizes.size()));

    std::vector<std::shared_ptr<QImage>> newTextures;
    for (int i = 0; i < nTex; ++i) {
        const int w = std::max(1, texSizes[size_t(i)].w);
        const int h = std::max(1, texSizes[size_t(i)].h);
        newTextures.push_back(RenderTextureCpu(facesByTexture[size_t(i)], m, textureObject, filter, imode, w, h));
    }
    return newTextures;
}
