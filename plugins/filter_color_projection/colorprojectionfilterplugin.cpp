#include "colorprojectionfilterplugin.h"

#include "camerashot.h"
#include "document.h"
#include "filterparam.h"
#include "meshfilterpluginmanager.h"
#include "textureassociationutils.h"
#include "vcgmesh.h"

#include "floatbuffer.h"
#include "pushpull.h"

#include <vcg/complex/algorithms/point_sampling.h>
#include <vcg/complex/algorithms/update/flag.h>
#include <vcg/complex/algorithms/update/normal.h>
#include <vcg/complex/algorithms/update/topology.h>
#include <wrap/io_trimesh/io_mask.h>

#include <QImage>
#include <QMatrix4x4>
#include <QRgb>
#include <QVector2D>
#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {

constexpr QLatin1StringView kFilterSingleProj(
    "compute_color_from_current_raster_projection");
constexpr QLatin1StringView kFilterMultiProj(
    "compute_color_from_active_rasters_projection");
constexpr QLatin1StringView kFilterMultiTexture(
    "compute_color_and_texture_from_active_rasters_projection");

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

MeshFilterRunResult fail(const QString &message)
{
    MeshFilterRunResult result;
    result.success          = false;
    result.documentModified = false;
    result.errorMessage     = message;
    return result;
}

MeshFilterRunResult success(const QStringList &info = {})
{
    MeshFilterRunResult result;
    result.success          = true;
    result.documentModified = true;
    result.infoMessages     = info;
    return result;
}

inline QVector3D transformPoint(const QMatrix4x4 &m, const vcg::Point3f &p)
{
    return m.map(QVector3D(p[0], p[1], p[2]));
}

// ---------------------------------------------------------------------------
// Software triangle rasterizer for depth buffer construction
// ---------------------------------------------------------------------------

// Rasterizes a single triangle into a float z-buffer using VCG bottom-up y.
static void rasterizeTriangleDepth(
    std::vector<float> &zbuf,
    int w, int h,
    const QVector2D &p0, const QVector2D &p1, const QVector2D &p2,
    float d0, float d1, float d2)
{
    // Skip degenerate or fully behind-camera triangles
    if (d0 <= 0.0f && d1 <= 0.0f && d2 <= 0.0f)
        return;

    // Edge function sign determines triangle orientation
    const float area =
        (p1.x() - p0.x()) * (p2.y() - p0.y()) -
        (p1.y() - p0.y()) * (p2.x() - p0.x());
    if (std::abs(area) < 1e-8f)
        return;
    const float invArea = 1.0f / area;

    // Bounding box in pixel space (clamped to viewport)
    const int ixMin = std::max(0,     static_cast<int>(std::floor(std::min({p0.x(), p1.x(), p2.x()}))));
    const int iyMin = std::max(0,     static_cast<int>(std::floor(std::min({p0.y(), p1.y(), p2.y()}))));
    const int ixMax = std::min(w - 1, static_cast<int>(std::ceil (std::max({p0.x(), p1.x(), p2.x()}))));
    const int iyMax = std::min(h - 1, static_cast<int>(std::ceil (std::max({p0.y(), p1.y(), p2.y()}))));

    if (ixMin > ixMax || iyMin > iyMax)
        return;

    for (int iy = iyMin; iy <= iyMax; ++iy) {
        for (int ix = ixMin; ix <= ixMax; ++ix) {
            const float px = float(ix) + 0.5f;
            const float py = float(iy) + 0.5f;

            // Barycentric coordinates
            const float bw0 = ((p1.x() - p2.x()) * (py - p2.y()) -
                                (p1.y() - p2.y()) * (px - p2.x())) * invArea;
            const float bw1 = ((p2.x() - p0.x()) * (py - p0.y()) -
                                (p2.y() - p0.y()) * (px - p0.x())) * invArea;
            const float bw2 = 1.0f - bw0 - bw1;

            if (bw0 < -1e-5f || bw1 < -1e-5f || bw2 < -1e-5f)
                continue;

            const float depth = bw0 * d0 + bw1 * d1 + bw2 * d2;
            if (depth <= 0.0f)
                continue;

            float &zbufVal = zbuf[size_t(iy * w + ix)];
            if (depth < zbufVal)
                zbufVal = depth;
        }
    }
}

// Builds a per-camera software depth buffer for the given mesh.
// zbuf[y * w + x] uses VCG y-convention (y=0 at bottom).
static std::unique_ptr<FloatBuffer> buildDepthBuffer(
    const CameraShot &shot,
    const VCGMesh    &mesh,
    const QMatrix4x4 &transform)
{
    const int w = shot.viewportPx().width();
    const int h = shot.viewportPx().height();

    const float kMaxDepth = std::numeric_limits<float>::max();
    std::vector<float> zbuf(size_t(w * h), kMaxDepth);

    for (const VCGFace &f : mesh.face) {
        if (f.IsD())
            continue;
        const QVector3D v0 = transformPoint(transform, f.cP(0));
        const QVector3D v1 = transformPoint(transform, f.cP(1));
        const QVector3D v2 = transformPoint(transform, f.cP(2));

        const float d0 = shot.depth(v0);
        const float d1 = shot.depth(v1);
        const float d2 = shot.depth(v2);

        if (d0 <= 0.0f && d1 <= 0.0f && d2 <= 0.0f)
            continue;

        const QVector2D pp0 = shot.project(v0);
        const QVector2D pp1 = shot.project(v1);
        const QVector2D pp2 = shot.project(v2);

        rasterizeTriangleDepth(zbuf, w, h, pp0, pp1, pp2, d0, d1, d2);
    }

    auto result = std::make_unique<FloatBuffer>();
    result->init(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float v = zbuf[size_t(y * w + x)];
            result->setval(x, y, (v == kMaxDepth) ? 0.0f : v);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Near / far depth calculation for depth-weight normalization
// ---------------------------------------------------------------------------

static void calculateNearFar(
    const Document   &doc,
    const VCGMesh    &mesh,
    const QMatrix4x4 &transform,
    std::vector<float> &nearVec,
    std::vector<float> &farVec)
{
    const int rasterCount = doc.rasterCount();
    nearVec.assign(size_t(rasterCount),  1000000.0f);
    farVec.assign (size_t(rasterCount), -1000000.0f);

    for (const VCGVertex &v : mesh.vert) {
        if (v.IsD())
            continue;
        const QVector3D wv = transformPoint(transform, v.cP());
        for (int ri = 0; ri < rasterCount; ++ri) {
            const auto &raster = doc.raster(ri);
            if (!raster.shot.isValid())
                continue;
            const QVector2D pp = raster.shot.project(wv);
            const QSize     vp = raster.shot.viewportPx();
            if (pp.x() > 0.0f && pp.y() > 0.0f &&
                pp.x() < float(vp.width()) &&
                pp.y() < float(vp.height()))
            {
                const float d = raster.shot.depth(wv);
                if (d < nearVec[size_t(ri)]) nearVec[size_t(ri)] = d;
                if (d > farVec [size_t(ri)]) farVec [size_t(ri)] = d;
            }
        }
    }

    for (int ri = 0; ri < rasterCount; ++ri) {
        if (nearVec[size_t(ri)] == 1000000.0f || farVec[size_t(ri)] == -1000000.0f) {
            nearVec[size_t(ri)] = 0.0f;
            farVec [size_t(ri)] = 0.0f;
        }
    }
}

// ---------------------------------------------------------------------------
// Texel descriptor and accumulator for texture-fill mode
// ---------------------------------------------------------------------------

struct TexelDesc {
    vcg::Point2i texcoord;
    QVector3D    meshpoint;   // world space
    QVector3D    meshnormal;  // world space, normalized
};

struct TexelAccum {
    float weights = 0.0f;
    float acc_red = 0.0f;
    float acc_grn = 0.0f;
    float acc_blu = 0.0f;
};

// VCG SurfaceSampling-compatible sampler that collects per-texel world-space
// mesh points and normals for later projection.
class TexFillerSampler
{
public:
    QImage           &trgImg;
    QMatrix4x4        transform;
    QMatrix4x4        normalMatrix;

    std::vector<TexelDesc>  *texelsPointer = nullptr;
    std::vector<TexelAccum> *accumPointer  = nullptr;

    // Required fields for VCG SurfaceSampling protocol
    const VCGFace *currFace = nullptr;
    int faceNo = 0, faceCnt = 0, start = 0, offset = 100;

    TexFillerSampler(QImage &img, const QMatrix4x4 &t, const QMatrix4x4 &nm)
        : trgImg(img), transform(t), normalMatrix(nm)
    {}

    void AddTextureSample(
        const VCGFace            &f,
        const VCGMesh::CoordType &p,
        const vcg::Point2i       &tp,
        float                    /*edgeDist*/)
    {
        const vcg::Point3f modelpoint =
            f.cP(0) * p[0] + f.cP(1) * p[1] + f.cP(2) * p[2];
        const vcg::Point3f modelnorm  =
            (f.cV(0)->N() * p[0] + f.cV(1)->N() * p[1] + f.cV(2)->N() * p[2]).Normalize();

        TexelDesc newtexel;
        newtexel.texcoord  = tp;
        newtexel.meshpoint = transform.map(
            QVector3D(modelpoint[0], modelpoint[1], modelpoint[2]));
        newtexel.meshnormal = normalMatrix.mapVector(
            QVector3D(modelnorm[0], modelnorm[1], modelnorm[2])).normalized();

        texelsPointer->push_back(newtexel);
        accumPointer->push_back(TexelAccum{});
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// Plugin interface
// ---------------------------------------------------------------------------

QString ColorProjectionFilterPlugin::pluginId() const
{
    return QStringLiteral("qmeshlab.filter.color_projection");
}

QString ColorProjectionFilterPlugin::name() const
{
    return QStringLiteral("Color Projection Filters");
}

MeshFilterRunResult ColorProjectionFilterPlugin::runFilter(
    const QString      &filterId,
    const FilterParams &params,
    Document           &doc) const
{
    namespace Tex = TextureAssociationUtils;
    using namespace vcg::tri::io;

    // Resolve current mesh
    const int meshIndex = doc.currentMeshIndex();
    if (meshIndex < 0 || meshIndex >= doc.meshCount())
        return fail(QObject::tr("No current mesh selected."));

    Document::MeshEntry &entry = doc.mesh(meshIndex);
    VCGMesh              &mesh = entry.mesh;

    // -----------------------------------------------------------------------
    // Filter: compute_color_from_current_raster_projection
    // -----------------------------------------------------------------------
    if (filterId == QString::fromLatin1(kFilterSingleProj)) {
        const bool   useDepth    = params.getBool(QStringLiteral("usedepth"), true);
        const bool   onSelection = params.getBool(QStringLiteral("onselection"), false);
        const float  eta         = static_cast<float>(params.getDouble(QStringLiteral("deptheta"), 0.5));
        const QColor blank       = params.getColor(QStringLiteral("blankColor"),
                                                   QColor(0, 0, 0, 0));

        const int currentRasterIndex = doc.currentRasterIndex();
        if (currentRasterIndex < 0 || currentRasterIndex >= doc.rasterCount())
            return fail(QObject::tr("No current raster selected."));

        const Document::RasterEntry &rasterEntry = doc.raster(currentRasterIndex);
        const CameraShot             &shot       = rasterEntry.shot;

        if (!shot.isValid())
            return fail(QObject::tr("Current raster has no valid camera."));
        if (rasterEntry.planes.empty())
            return fail(QObject::tr("Current raster has no image planes."));

        const Document::RasterPlane *rasterPlane = rasterEntry.currentPlane();
        if (!rasterPlane || !rasterPlane->hasImage())
            return fail(QObject::tr("Current raster plane has no image."));

        const QImage &rasterImage = rasterPlane->image;
        const int     imgW        = shot.viewportPx().width();
        const int     imgH        = shot.viewportPx().height();

        // Build software depth buffer if depth test is requested
        std::unique_ptr<FloatBuffer> depthBuf;
        if (useDepth) {
            depthBuf = buildDepthBuffer(shot, mesh, entry.transform);
        }

        // Normal transform matrix (inverse transpose of model matrix)
        const QMatrix4x4 normalMatrix = entry.transform.inverted().transposed();
        const QVector3D  cameraZ      = shot.referenceAxis(2);

        // Apply blank color first if requested
        const bool hasBlank =
            (blank.red() != 0) || (blank.green() != 0) ||
            (blank.blue() != 0) || (blank.alpha() != 0);

        for (VCGVertex &v : mesh.vert) {
            if (v.IsD())
                continue;
            if (onSelection && !v.IsS())
                continue;

            if (hasBlank)
                v.C() = vcg::Color4b(blank.red(), blank.green(),
                                     blank.blue(), blank.alpha());

            const QVector3D wv  = transformPoint(entry.transform, v.cP());
            const QVector2D pp  = shot.project(wv);

            // Check inside image (strictly, matching original > 0 convention)
            if (pp.x() <= 0.0f || pp.y() <= 0.0f ||
                pp.x() >= float(imgW) || pp.y() >= float(imgH))
                continue;

            // Check vertex faces camera (pray is direction from vertex to camera)
            const QVector3D pray = (shot.viewPoint() - wv).normalized();
            if (QVector3D::dotProduct(pray, -cameraZ) > 0.0f)
                continue;

            // Depth test
            if (useDepth) {
                const float depth  = shot.depth(wv);
                const float pdepth = depthBuf->getval(int(pp.x()), int(pp.y()));
                if (depth > pdepth + eta)
                    continue;
            }

            // Sample image (VCG y-flip: y=0 at bottom → QImage y=0 at top)
            const QRgb pcolor = rasterImage.pixel(int(pp.x()), imgH - int(pp.y()));
            v.C() = vcg::Color4b(qRed(pcolor), qGreen(pcolor), qBlue(pcolor), 255);
        }

        entry.ioMask |= Mask::IOM_VERTCOLOR;
        doc.markMeshGeometryChanged(
            meshIndex,
            QObject::tr("Projected raster color onto vertex colors for '%1'.")
                .arg(entry.name));

        return success();
    }

    // -----------------------------------------------------------------------
    // Filter: compute_color_from_active_rasters_projection
    // -----------------------------------------------------------------------
    if (filterId == QString::fromLatin1(kFilterMultiProj)) {
        const float  eta           = static_cast<float>(params.getDouble(QStringLiteral("deptheta"),   0.5));
        const bool   onSelection   = params.getBool (QStringLiteral("onselection"), false);
        const bool   useAngle      = params.getBool (QStringLiteral("useangle"),    true);
        const bool   useDistance   = params.getBool (QStringLiteral("usedistance"), true);
        const bool   useBorders    = params.getBool (QStringLiteral("useborders"),  true);
        const bool   useSilhouette = params.getBool (QStringLiteral("usesilhouettes"), true);
        const bool   useAlpha      = params.getBool (QStringLiteral("usealpha"),    false);
        const QColor blank         = params.getColor(QStringLiteral("blankColor"),
                                                     QColor(0, 0, 0, 0));

        if (doc.rasterCount() == 0)
            return fail(QObject::tr("No rasters in the document."));

        // Ensure per-vertex normals are up-to-date (needed for angle weight)
        if (useAngle)
            vcg::tri::UpdateNormal<VCGMesh>::PerVertex(mesh);

        // Per-raster near/far for distance weight normalization
        std::vector<float> myNear, myFar;
        calculateNearFar(doc, mesh, entry.transform, myNear, myFar);

        float allMaxDepth    = -1000000.0f;
        float allMinDepth    =  1000000.0f;
        for (int ri = 0; ri < doc.rasterCount(); ++ri) {
            if (myFar [size_t(ri)] > allMaxDepth) allMaxDepth = myFar [size_t(ri)];
            if (myNear[size_t(ri)] < allMinDepth) allMinDepth = myNear[size_t(ri)];
        }

        // Accumulation buffers (indexed by vertex position in vert array)
        const int vertCount = int(mesh.vert.size());
        std::vector<double> weights(size_t(vertCount), 0.0);
        std::vector<double> accRed (size_t(vertCount), 0.0);
        std::vector<double> accGrn (size_t(vertCount), 0.0);
        std::vector<double> accBlu (size_t(vertCount), 0.0);

        // Normal transform matrix (inverse transpose)
        const QMatrix4x4 normalMatrix = entry.transform.inverted().transposed();

        int camInd = 0;
        for (int ri = 0; ri < doc.rasterCount(); ++ri) {
            const Document::RasterEntry &rasterEntry = doc.raster(ri);
            if (!rasterEntry.visible)           { ++camInd; continue; }
            if (!rasterEntry.shot.isValid())     { ++camInd; continue; }
            if (rasterEntry.planes.empty())      { ++camInd; continue; }

            const CameraShot &shot       = rasterEntry.shot;
            const Document::RasterPlane *rasterPlane = rasterEntry.currentPlane();
            if (!rasterPlane || !rasterPlane->hasImage()) { ++camInd; continue; }
            const QImage     &rasterImage = rasterPlane->image;
            const int         imgW        = shot.viewportPx().width();
            const int         imgH        = shot.viewportPx().height();
            const QVector3D   cameraZ     = shot.referenceAxis(2);

            // Build software depth buffer
            auto depthBuf = buildDepthBuffer(shot, mesh, entry.transform);

            // Build silhouette distance field if requested
            FloatBuffer *silhouetteBuf = nullptr;
            float maxSilDist = float(imgW + imgH);
            if (useSilhouette) {
                silhouetteBuf = new FloatBuffer();
                silhouetteBuf->init(imgW, imgH);
                silhouetteBuf->applysobel(*depthBuf);
                silhouetteBuf->initborder(*depthBuf);
                const float dist = silhouetteBuf->distancefield();
                if (dist > 0.0f)
                    maxSilDist = dist;
            }

            int buffInd = 0;
            for (const VCGVertex &v : mesh.vert) {
                if (!v.IsD() && (!onSelection || v.IsS())) {
                    const QVector3D wv  = transformPoint(entry.transform, v.cP());
                    const QVector2D pp  = shot.project(wv);

                    if (pp.x() >= 0.0f && pp.y() >= 0.0f &&
                        pp.x() < float(imgW) && pp.y() < float(imgH))
                    {
                        const QVector3D pray = (shot.viewPoint() - wv).normalized();
                        if (QVector3D::dotProduct(pray, -cameraZ) <= 0.0f) {
                            const float depth  = shot.depth(wv);
                            const float pdepth = depthBuf->getval(int(pp.x()), int(pp.y()));

                            if (depth <= pdepth + eta) {
                                const QRgb pcolor = rasterImage.pixel(
                                    int(pp.x()), imgH - int(pp.y()));

                                double pweight = 1.0;

                                if (useAngle) {
                                    const vcg::Point3f &modelNorm = v.cN();
                                    const QVector3D wn = normalMatrix.mapVector(
                                        QVector3D(modelNorm[0], modelNorm[1], modelNorm[2])).normalized();
                                    const float ang = std::abs(QVector3D::dotProduct(wn, pray));
                                    pweight *= double(std::min(1.0f, ang));
                                }

                                if (useDistance && allMaxDepth > allMinDepth) {
                                    float distw = 1.0f - (depth - allMinDepth * 0.99f) /
                                                         (allMaxDepth * 1.01f - allMinDepth * 0.99f);
                                    pweight *= double(distw) * double(distw);
                                }

                                if (useBorders) {
                                    const double xdist = 1.0 - std::abs(pp.x() - imgW * 0.5) / (imgW * 0.5);
                                    const double ydist = 1.0 - std::abs(pp.y() - imgH * 0.5) / (imgH * 0.5);
                                    pweight *= std::min(xdist, ydist);
                                }

                                if (useSilhouette && silhouetteBuf != nullptr) {
                                    const float silw = silhouetteBuf->getval(int(pp.x()), int(pp.y()))
                                                       / maxSilDist;
                                    pweight *= double(silw);
                                }

                                if (useAlpha) {
                                    pweight *= qAlpha(pcolor) / 255.0;
                                }

                                weights[size_t(buffInd)] += pweight;
                                accRed [size_t(buffInd)] += qRed(pcolor)   * pweight / 255.0;
                                accGrn [size_t(buffInd)] += qGreen(pcolor) * pweight / 255.0;
                                accBlu [size_t(buffInd)] += qBlue(pcolor)  * pweight / 255.0;
                            }
                        }
                    }
                }
                ++buffInd;
            }

            delete silhouetteBuf;
            ++camInd;
        }

        // Write accumulated colors back to vertices
        const bool hasBlank =
            (blank.red() != 0) || (blank.green() != 0) ||
            (blank.blue() != 0) || (blank.alpha() != 0);

        int buffInd = 0;
        for (VCGVertex &v : mesh.vert) {
            if (!v.IsD() && (!onSelection || v.IsS())) {
                if (weights[size_t(buffInd)] > 0.0) {
                    const double w = weights[size_t(buffInd)];
                    v.C() = vcg::Color4b(
                        static_cast<unsigned char>(accRed[size_t(buffInd)] / w * 255.0),
                        static_cast<unsigned char>(accGrn[size_t(buffInd)] / w * 255.0),
                        static_cast<unsigned char>(accBlu[size_t(buffInd)] / w * 255.0),
                        255);
                } else if (hasBlank) {
                    v.C() = vcg::Color4b(blank.red(), blank.green(),
                                         blank.blue(), blank.alpha());
                }
            }
            ++buffInd;
        }

        entry.ioMask |= Mask::IOM_VERTCOLOR;
        doc.markMeshGeometryChanged(
            meshIndex,
            QObject::tr("Projected active rasters onto vertex colors for '%1'.")
                .arg(entry.name));

        return success();
    }

    // -----------------------------------------------------------------------
    // Filter: compute_color_and_texture_from_active_rasters_projection
    // -----------------------------------------------------------------------
    if (filterId == QString::fromLatin1(kFilterMultiTexture)) {
        if ((entry.ioMask & Mask::IOM_WEDGTEXCOORD) == 0)
            return fail(QObject::tr("Mesh has no wedge texture coordinates."));

        const float   eta           = static_cast<float>(params.getDouble(QStringLiteral("deptheta"), 0.5));
        const int     texSize       = params.getInt      (QStringLiteral("texsize"),         1024);
        const bool    doRefill      = params.getBool     (QStringLiteral("dorefill"),        true);
        const bool    useAngle      = params.getBool     (QStringLiteral("useangle"),        true);
        const bool    useDistance   = params.getBool     (QStringLiteral("usedistance"),     true);
        const bool    useBorders    = params.getBool     (QStringLiteral("useborders"),      true);
        const bool    useSilhouette = params.getBool     (QStringLiteral("usesilhouettes"),  true);
        const bool    useAlpha      = params.getBool     (QStringLiteral("usealpha"),        false);
        const QString textName      = params.getFileSave (QStringLiteral("textName")).trimmed();

        if (texSize <= 0)
            return fail(QObject::tr("Texture size must be positive."));
        if (textName.isEmpty())
            return fail(QObject::tr("Texture file not specified."));
        if (doc.rasterCount() == 0)
            return fail(QObject::tr("No rasters in the document."));

        const int textW = texSize;
        const int textH = texSize;

        // Ensure per-vertex normals up to date
        if (useAngle)
            vcg::tri::UpdateNormal<VCGMesh>::PerVertex(mesh);

        // Create transparent output texture image
        QImage img(QSize(textW, textH), QImage::Format_ARGB32);
        img.fill(qRgba(0, 0, 0, 0));

        // Build face-face topology for border edge detection
        if (doRefill) {
            VCGMeshFFAdjScope _ffAdj(mesh);
            vcg::tri::UpdateTopology<VCGMesh>::FaceFaceFromTexCoord(mesh);
            vcg::tri::UpdateFlags<VCGMesh>::FaceBorderFromFF(mesh);
        }

        // Rasterize UV atlas into a list of texels with world-space geometry
        std::vector<TexelDesc>  texels;
        std::vector<TexelAccum> accums;
        texels.reserve(size_t(textW * textH));
        accums.reserve(size_t(textW * textH));

        const QMatrix4x4 normalMatrix = entry.transform.inverted().transposed();
        TexFillerSampler tfs(img, entry.transform, normalMatrix);
        tfs.texelsPointer = &texels;
        tfs.accumPointer  = &accums;

        {
            // Need FF adjacency enabled for Texture sampling
            VCGMeshFFAdjScope _ffAdj2(mesh);
            vcg::tri::UpdateTopology<VCGMesh>::FaceFaceFromTexCoord(mesh);
            vcg::tri::UpdateFlags<VCGMesh>::FaceBorderFromFF(mesh);
            vcg::tri::SurfaceSampling<VCGMesh, TexFillerSampler>::Texture(
                mesh, tfs, textW, textH, true);
            // Restore original topology
            vcg::tri::UpdateTopology<VCGMesh>::FaceFace(mesh);
            vcg::tri::UpdateFlags<VCGMesh>::FaceBorderFromFF(mesh);
        }

        // Fix partially-filled edge pixels (border anti-aliasing)
        for (int y = 0; y < textH; ++y) {
            for (int x = 0; x < textW; ++x) {
                const QRgb px = img.pixel(x, y);
                if (qAlpha(px) < 255 && qAlpha(px) > 0)
                    img.setPixel(x, y, px | 0xff000000u);
            }
        }

        // Per-raster near/far for distance weight normalization
        std::vector<float> myNear, myFar;
        calculateNearFar(doc, mesh, entry.transform, myNear, myFar);

        float allMaxDepth = -1000000.0f;
        float allMinDepth =  1000000.0f;
        for (int ri = 0; ri < doc.rasterCount(); ++ri) {
            if (myFar [size_t(ri)] > allMaxDepth) allMaxDepth = myFar [size_t(ri)];
            if (myNear[size_t(ri)] < allMinDepth) allMinDepth = myNear[size_t(ri)];
        }

        // Project each active raster onto the texels
        int camInd = 0;
        for (int ri = 0; ri < doc.rasterCount(); ++ri) {
            const Document::RasterEntry &rasterEntry = doc.raster(ri);
            if (!rasterEntry.visible)            { ++camInd; continue; }
            if (!rasterEntry.shot.isValid())      { ++camInd; continue; }
            if (rasterEntry.planes.empty())       { ++camInd; continue; }

            const CameraShot &shot        = rasterEntry.shot;
            const Document::RasterPlane *rasterPlane = rasterEntry.currentPlane();
            if (!rasterPlane || !rasterPlane->hasImage()) { ++camInd; continue; }
            const QImage     &rasterImage = rasterPlane->image;
            const int         imgW        = shot.viewportPx().width();
            const int         imgH        = shot.viewportPx().height();
            const QVector3D   cameraZ     = shot.referenceAxis(2);

            auto depthBuf = buildDepthBuffer(shot, mesh, entry.transform);

            FloatBuffer *silhouetteBuf = nullptr;
            float maxSilDist = float(imgW + imgH);
            if (useSilhouette) {
                silhouetteBuf = new FloatBuffer();
                silhouetteBuf->init(imgW, imgH);
                silhouetteBuf->applysobel(*depthBuf);
                silhouetteBuf->initborder(*depthBuf);
                const float dist = silhouetteBuf->distancefield();
                if (dist > 0.0f)
                    maxSilDist = dist;
            }

            for (size_t tc = 0; tc < texels.size(); ++tc) {
                const QVector3D &meshPt   = texels[tc].meshpoint;
                const QVector3D &meshNorm = texels[tc].meshnormal;
                const QVector2D  pp       = shot.project(meshPt);

                if (pp.x() <= 0.0f || pp.y() <= 0.0f ||
                    pp.x() >= float(imgW) || pp.y() >= float(imgH))
                    continue;

                const QVector3D pray = (shot.viewPoint() - meshPt).normalized();
                if (QVector3D::dotProduct(pray, -cameraZ) > 0.0f)
                    continue;

                const float depth  = shot.depth(meshPt);
                const float pdepth = depthBuf->getval(int(pp.x()), int(pp.y()));
                if (depth > pdepth + eta)
                    continue;

                const QRgb pcolor = rasterImage.pixel(int(pp.x()), imgH - int(pp.y()));
                double pweight = 1.0;

                if (useAngle) {
                    const float ang = std::abs(QVector3D::dotProduct(meshNorm, pray));
                    pweight *= double(std::min(1.0f, ang));
                }

                if (useDistance && allMaxDepth > allMinDepth) {
                    const float distw = 1.0f - (depth - allMinDepth * 0.99f) /
                                               (allMaxDepth * 1.01f - allMinDepth * 0.99f);
                    pweight *= double(distw) * double(distw);
                }

                if (useBorders) {
                    const double xdist = 1.0 - std::abs(pp.x() - imgW * 0.5) / (imgW * 0.5);
                    const double ydist = 1.0 - std::abs(pp.y() - imgH * 0.5) / (imgH * 0.5);
                    pweight *= std::min(xdist, ydist);
                }

                if (useSilhouette && silhouetteBuf != nullptr) {
                    const float silw = silhouetteBuf->getval(int(pp.x()), int(pp.y())) / maxSilDist;
                    pweight *= double(silw);
                }

                if (useAlpha) {
                    pweight *= qAlpha(pcolor) / 255.0;
                }

                accums[tc].weights += float(pweight);
                accums[tc].acc_red += float(qRed(pcolor)   * pweight / 255.0);
                accums[tc].acc_grn += float(qGreen(pcolor) * pweight / 255.0);
                accums[tc].acc_blu += float(qBlue(pcolor)  * pweight / 255.0);
            }

            delete silhouetteBuf;
            ++camInd;
        }

        // Write accumulated texel colors into the output image
        // Note: texcoord.Y() uses VCG convention (y=0 at bottom), QImage uses y=0 at top
        for (size_t tc = 0; tc < texels.size(); ++tc) {
            const int imgX = texels[tc].texcoord.X();
            const int imgY = textH - 1 - texels[tc].texcoord.Y();
            if (accums[tc].weights > 0.0f) {
                const float w = accums[tc].weights;
                img.setPixel(imgX, imgY,
                    qRgba(int(accums[tc].acc_red / w * 255.0f),
                          int(accums[tc].acc_grn / w * 255.0f),
                          int(accums[tc].acc_blu / w * 255.0f),
                          255));
            } else {
                img.setPixel(imgX, imgY, qRgba(0, 0, 0, 0));
            }
        }

        // PullPush hole-filling
        if (doRefill)
            vcg::PullPush(img, qRgba(0, 0, 0, 0));

        // Save texture image and associate with mesh
        QString saveError;
        const QStringList outputPaths = { textName };
        const std::vector<QImage> outputImages = { img };
        if (!Tex::saveImages(outputPaths, outputImages, saveError))
            return fail(saveError);

        const std::vector<MeshIOTextureAsset> outputAssets =
            Tex::makeTextureAssetsFromSavedImages(outputPaths, outputImages);
        Tex::replaceTextureAssociations(entry, outputAssets);

        entry.ioMask |= Mask::IOM_WEDGTEXCOORD;
        doc.markMeshMaterialChanged(
            meshIndex,
            QObject::tr("Projected active rasters onto texture '%1' for '%2'.")
                .arg(textName, entry.name));

        return success({ QObject::tr("Saved texture: %1").arg(textName) });
    }

    return fail(QObject::tr("Unknown filter: %1").arg(filterId));
}

void registerColorProjectionFilterPlugin(MeshFilterPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<ColorProjectionFilterPlugin>());
}