#include "renderwidget.h"
#include "document.h"
#include "renderwidget_internal.h"
#include <QImage>
#include <QSet>
#include <QVector2D>
#include <QVector4D>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

using namespace RenderWidgetInternal;

namespace {

QVector4D fitImageRectNdc(const QSize &imageSize, const QSize &targetSize)
{
    if (imageSize.width() <= 0 || imageSize.height() <= 0
        || targetSize.width() <= 0 || targetSize.height() <= 0) {
        return QVector4D(0.0f, 0.0f, 1.0f, 1.0f);
    }

    const float imageAspect = float(imageSize.width()) / float(imageSize.height());
    const float targetAspect = float(targetSize.width()) / float(targetSize.height());
    float halfW = 1.0f;
    float halfH = 1.0f;
    if (imageAspect > targetAspect)
        halfH = targetAspect / imageAspect;
    else
        halfW = imageAspect / targetAspect;
    return QVector4D(0.0f, 0.0f, halfW, halfH);
}

bool isFinite(const QVector3D &v)
{
    return std::isfinite(v.x()) && std::isfinite(v.y()) && std::isfinite(v.z());
}

void appendProjectedRasterVertex(
    std::vector<float> &vertices,
    const QVector3D &position)
{
    vertices.push_back(position.x());
    vertices.push_back(position.y());
    vertices.push_back(position.z());
}

void appendProjectedRasterLine(
    std::vector<float> &vertices,
    const QVector3D &a,
    const QVector3D &b)
{
    appendProjectedRasterVertex(vertices, a);
    appendProjectedRasterVertex(vertices, b);
}

} // namespace

void RenderWidget::syncRasterCacheWithDocument()
{
    if (!m_doc) {
        m_rastersGpu.clear();
        return;
    }

    QSet<std::uint64_t> aliveIds;
    for (int i = 0; i < m_doc->rasterCount(); ++i)
        aliveIds.insert(m_doc->raster(i).rasterId);

    for (auto it = m_rastersGpu.begin(); it != m_rastersGpu.end();) {
        if (!aliveIds.contains(it->first))
            it = m_rastersGpu.erase(it);
        else
            ++it;
    }
}

void RenderWidget::ensureRasterResources(
    QRhiCommandBuffer *cb,
    const RenderFramePassRequests &requests)
{
    if (!m_doc || !m_rhi || !cb || !requests.hasRasters()) {
        return;
    }

    syncRasterCacheWithDocument();

    auto rasterFrustumDepth = [&]() {
        bool hasBounds = false;
        QVector3D sceneMin;
        QVector3D sceneMax;
        for (int meshIndex = 0; meshIndex < m_doc->meshCount(); ++meshIndex) {
            if (!meshVisible(meshIndex))
                continue;
            const Document::MeshEntry &meshEntry = m_doc->mesh(meshIndex);
            if (meshEntry.mesh.bbox.IsNull())
                continue;

            const vcg::Box3f &box = meshEntry.mesh.bbox;
            const QVector3D corners[8] = {
                QVector3D(box.min[0], box.min[1], box.min[2]),
                QVector3D(box.max[0], box.min[1], box.min[2]),
                QVector3D(box.min[0], box.max[1], box.min[2]),
                QVector3D(box.max[0], box.max[1], box.min[2]),
                QVector3D(box.min[0], box.min[1], box.max[2]),
                QVector3D(box.max[0], box.min[1], box.max[2]),
                QVector3D(box.min[0], box.max[1], box.max[2]),
                QVector3D(box.max[0], box.max[1], box.max[2])
            };
            for (const QVector3D &corner : corners) {
                const QVector4D transformed = meshEntry.transform * QVector4D(corner, 1.0f);
                const QVector3D worldCorner = (std::abs(transformed.w()) > 1e-8f)
                    ? transformed.toVector3DAffine()
                    : transformed.toVector3D();
                if (!hasBounds) {
                    sceneMin = worldCorner;
                    sceneMax = worldCorner;
                    hasBounds = true;
                    continue;
                }
                sceneMin.setX(std::min(sceneMin.x(), worldCorner.x()));
                sceneMin.setY(std::min(sceneMin.y(), worldCorner.y()));
                sceneMin.setZ(std::min(sceneMin.z(), worldCorner.z()));
                sceneMax.setX(std::max(sceneMax.x(), worldCorner.x()));
                sceneMax.setY(std::max(sceneMax.y(), worldCorner.y()));
                sceneMax.setZ(std::max(sceneMax.z(), worldCorner.z()));
            }
        }

        if (hasBounds)
            return std::max(1e-3f, (sceneMax - sceneMin).length() * 0.12f);

        return std::max(1e-3f, m_trackball.radius() * 0.25f);
    };

    auto buildProjectedVertices = [&](const Document::RasterEntry &entry) {
        std::vector<float> vertices;
        if (!entry.shot.isValid())
            return vertices;

        const QSize viewport = entry.shot.viewportPx();
        if (viewport.width() <= 0 || viewport.height() <= 0)
            return vertices;

        const float depth = rasterFrustumDepth();
        if (!std::isfinite(depth) || depth <= 0.0f)
            return vertices;

        const QVector3D apex = entry.shot.viewPoint();
        const QVector3D bl = entry.shot.unproject(QVector2D(0.0f, 0.0f), depth);
        const QVector3D br = entry.shot.unproject(QVector2D(float(viewport.width()), 0.0f), depth);
        const QVector3D tl = entry.shot.unproject(QVector2D(0.0f, float(viewport.height())), depth);
        const QVector3D tr =
            entry.shot.unproject(QVector2D(float(viewport.width()), float(viewport.height())), depth);
        if (!isFinite(apex) || !isFinite(bl) || !isFinite(br) || !isFinite(tl) || !isFinite(tr))
            return vertices;

        vertices.reserve(kRasterProjectedFrustumVertexCount * kRasterProjectedVertexStrideFloats);
        appendProjectedRasterLine(vertices, apex, bl);
        appendProjectedRasterLine(vertices, apex, br);
        appendProjectedRasterLine(vertices, apex, tl);
        appendProjectedRasterLine(vertices, apex, tr);
        appendProjectedRasterLine(vertices, bl, br);
        appendProjectedRasterLine(vertices, br, tr);
        appendProjectedRasterLine(vertices, tr, tl);
        appendProjectedRasterLine(vertices, tl, bl);
        return vertices;
    };

    QRhiResourceUpdateBatch *updates = nullptr;
    auto ensureUpdates = [&]() {
        if (!updates)
            updates = m_rhi->nextResourceUpdateBatch();
        return updates;
    };

    auto ensureRaster = [&](int rasterIndex, bool projected) {
        if (rasterIndex < 0 || rasterIndex >= m_doc->rasterCount())
            return;

        const Document::RasterEntry &entry = m_doc->raster(rasterIndex);
        const Document::RasterPlane *plane = entry.currentPlane();
        if (!plane || plane->image.isNull())
            return;

        RasterGpu &gpu = m_rastersGpu[entry.rasterId];

        if (projected) {
            if (!gpu.projectedSrb) {
                if (!m_rasterProjectedUbuf)
                    return;
                gpu.projectedSrb.reset(m_rhi->newShaderResourceBindings());
                gpu.projectedSrb->setBindings({
                    QRhiShaderResourceBinding::uniformBuffer(
                        0,
                        QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                        m_rasterProjectedUbuf.get())
                });
                if (!gpu.projectedSrb->create())
                    gpu.projectedSrb.reset();
            }

            constexpr int vertexBytes =
                kRasterProjectedFrustumVertexCount
                * kRasterProjectedVertexStrideFloats
                * int(sizeof(float));
            if (!gpu.projectedVbuf) {
                gpu.projectedVbuf.reset(
                    m_rhi->newBuffer(
                        QRhiBuffer::Dynamic,
                        QRhiBuffer::VertexBuffer,
                        vertexBytes));
                if (!gpu.projectedVbuf || !gpu.projectedVbuf->create()) {
                    gpu.projectedVbuf.reset();
                    gpu.projectedVertexCount = 0;
                    return;
                }
            }

            const std::vector<float> vertices = buildProjectedVertices(entry);
            if (int(vertices.size())
                != kRasterProjectedFrustumVertexCount * kRasterProjectedVertexStrideFloats) {
                gpu.projectedVertexCount = 0;
                return;
            }
            ensureUpdates()->updateDynamicBuffer(
                gpu.projectedVbuf.get(),
                0,
                vertexBytes,
                vertices.data());
            gpu.projectedVertexCount = kRasterProjectedFrustumVertexCount;
            return;
        }

        if (!m_rasterSampler)
            return;

        QImage uploadImage = plane->image.convertToFormat(QImage::Format_RGBA8888);
        if (uploadImage.isNull() || uploadImage.width() <= 0 || uploadImage.height() <= 0)
            return;

        const bool needsTexture =
            !gpu.texture
            || gpu.imageRevision != entry.imageRevision
            || gpu.planeIndex != entry.currentPlaneIndex
            || gpu.size != uploadImage.size();

        if (needsTexture) {
            gpu.backplateSrb.reset();
            gpu.projectedSrb.reset();
            gpu.texture.reset(
                m_rhi->newTexture(QRhiTexture::RGBA8, uploadImage.size(), 1));
            if (!gpu.texture || !gpu.texture->create()) {
                gpu.texture.reset();
                gpu.imageRevision = 0;
                gpu.planeIndex = -1;
                gpu.size = QSize();
                return;
            }
            gpu.imageRevision = entry.imageRevision;
            gpu.planeIndex = entry.currentPlaneIndex;
            gpu.size = uploadImage.size();

            QRhiTextureUploadEntry textureEntry(
                0, 0, QRhiTextureSubresourceUploadDescription(uploadImage));
            ensureUpdates()->uploadTexture(
                gpu.texture.get(),
                QRhiTextureUploadDescription({ textureEntry }));
        }

        if (!projected && !gpu.backplateSrb && gpu.texture) {
            if (!m_rasterBackplateUbuf)
                return;
            gpu.backplateSrb.reset(m_rhi->newShaderResourceBindings());
            gpu.backplateSrb->setBindings({
                QRhiShaderResourceBinding::uniformBuffer(
                    0,
                    QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                    m_rasterBackplateUbuf.get()),
                QRhiShaderResourceBinding::sampledTexture(
                    1,
                    QRhiShaderResourceBinding::FragmentStage,
                    gpu.texture.get(),
                    m_rasterSampler.get())
            });
            if (!gpu.backplateSrb->create())
                gpu.backplateSrb.reset();
        }

    };

    for (int rasterIndex : requests.rasterBackplates)
        ensureRaster(rasterIndex, false);
    for (int rasterIndex : requests.rasterProjected)
        ensureRaster(rasterIndex, true);

    if (updates)
        cb->resourceUpdate(updates);
}

void RenderWidget::renderSceneRasterProjected(
    QRhiCommandBuffer *cb,
    const RenderFramePlan &plan)
{
    if (!cb || plan.rasterProjectedItems.empty()
        || !m_rasterProjectedPipeline || !m_rasterProjectedUbuf) {
        return;
    }

    cb->setGraphicsPipeline(m_rasterProjectedPipeline.get());
    cb->setViewport({
        0,
        0,
        float(plan.pixelSize.width()),
        float(plan.pixelSize.height())
    });

    const QMatrix4x4 mvp = plan.proj * plan.view;
    for (const SceneRasterProjectedDrawItem &item : plan.rasterProjectedItems) {
        if (!item.srb || !item.vertexBuffer || item.vertexCount <= 0)
            continue;

        float ubuf[kRasterProjectedUbufSize / sizeof(float)] = {};
        memcpy(ubuf, mvp.constData(), 64);
        ubuf[16] = 0.15f;
        ubuf[17] = 0.65f;
        ubuf[18] = 1.0f;
        ubuf[19] = 0.95f;

        QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
        u->updateDynamicBuffer(
            m_rasterProjectedUbuf.get(),
            0,
            kRasterProjectedUbufSize,
            ubuf);
        cb->resourceUpdate(u);

        cb->setShaderResources(item.srb);
        const QRhiCommandBuffer::VertexInput binding(item.vertexBuffer, 0);
        cb->setVertexInput(0, 1, &binding);
        cb->draw(item.vertexCount);
    }
}

void RenderWidget::renderSceneRasterBackplates(
    QRhiCommandBuffer *cb,
    const RenderFramePlan &plan)
{
    if (!cb || plan.rasterBackplateItems.empty()
        || !m_rasterBackplatePipeline || !m_rasterBackplateUbuf) {
        return;
    }

    cb->setGraphicsPipeline(m_rasterBackplatePipeline.get());
    cb->setViewport({
        0,
        0,
        float(plan.pixelSize.width()),
        float(plan.pixelSize.height())
    });

    for (const SceneRasterBackplateDrawItem &item : plan.rasterBackplateItems) {
        if (!item.srb || item.imageSize.isEmpty())
            continue;

        const QVector4D rect = item.fitToViewport
            ? fitImageRectNdc(item.imageSize, plan.pixelSize)
            : QVector4D(0.0f, 0.0f, 1.0f, 1.0f);
        float ubuf[kRasterBackplateUbufSize / sizeof(float)] = {};
        ubuf[0] = rect.x();
        ubuf[1] = rect.y();
        ubuf[2] = rect.z();
        ubuf[3] = rect.w();
        ubuf[4] = std::clamp(plan.rasterOpacity, 0.0f, 1.0f);

        QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
        u->updateDynamicBuffer(
            m_rasterBackplateUbuf.get(),
            0,
            kRasterBackplateUbufSize,
            ubuf);
        cb->resourceUpdate(u);

        cb->setShaderResources(item.srb);
        cb->draw(6);
    }
}
