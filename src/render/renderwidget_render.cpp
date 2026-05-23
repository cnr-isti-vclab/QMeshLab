#include "renderwidget.h"
#include "colormap.h"
#include "document.h"
#include "renderwidget_internal.h"
#include <QLabel>
#include <algorithm>
#include <cmath>

using namespace RenderWidgetInternal;

void RenderWidget::initialize(QRhiCommandBuffer *cb)
{
    ensureRenderResources();
    if (!m_rhi) {
        qWarning("QRhi not available");
        return;
    }

    prepareDirtyBuffers(cb);
}

void RenderWidget::uploadMainUbuf(
    QRhiCommandBuffer *cb,
    const QMatrix4x4 &mvp,
    const QMatrix4x4 &modelView,
    const QMatrix3x3 &normalMat,
    const PerMeshRenderSettings &settings,
    const QSize &pixelSize,
    bool enableLighting,
    const QVector3D &lightDir,
    MainUbufMaterialOverrides materialOverrides)
{
    if (!m_rhi || !m_ubuf || !cb)
        return;

    float ubufData[kUbufFloatCount] = {};
    writeMainUbuf(
        ubufData,
        mvp,
        modelView,
        normalMat,
        settings,
        pixelSize,
        enableLighting,
        lightDir,
        materialOverrides);

    QRhiResourceUpdateBatch *uMesh = m_rhi->nextResourceUpdateBatch();
    uMesh->updateDynamicBuffer(m_ubuf.get(), 0, kUbufSize, ubufData);
    cb->resourceUpdate(uMesh);
}

void RenderWidget::uploadMainUbufForMesh(
    QRhiCommandBuffer *cb,
    int meshIndex,
    const QMatrix4x4 &proj,
    const QMatrix4x4 &view,
    const PerMeshRenderSettings &meshSettings,
    const QSize &pixelSize,
    bool enableLighting,
    const QVector3D &lightDir,
    MainUbufMaterialOverrides materialOverrides)
{
    if (!m_doc || meshIndex < 0 || meshIndex >= m_doc->meshCount())
        return;

    const QMatrix4x4 model = m_doc->mesh(meshIndex).transform;
    const QMatrix4x4 modelView = view * model;
    const QMatrix4x4 mvp = proj * modelView;
    const QMatrix3x3 normalMat = modelView.normalMatrix();

    uploadMainUbuf(
        cb,
        mvp,
        modelView,
        normalMat,
        meshSettings,
        pixelSize,
        enableLighting,
        lightDir,
        materialOverrides);
}

void RenderWidget::render(QRhiCommandBuffer *cb)
{
    ensureRenderResources();
    if (!m_rhi || !m_ubuf || !m_srb)
        return;

    if (m_viewMode == ViewMode::ParametrizationUV) {
        renderParametrization(cb);
        return;
    }
    for (QLabel *label : m_uvScaleXTickLabels) {
        if (label)
            label->hide();
    }
    for (QLabel *label : m_uvScaleYTickLabels) {
        if (label)
            label->hide();
    }

    advanceCenterAnimation();
    emitCameraStateChangedIfNeeded();
    syncPerMeshRenderModesWithDocument();
    updateCameraFrameIfNeeded();

    const bool drawTrackballGizmo =
        m_renderSettings.showTrackballGizmo && (m_doc->meshCount() > 0);
    const int currentMeshIndex = m_doc->currentMeshIndex();
    const bool drawCurrentMeshHighlight =
        m_renderSettings.highlightCurrentMesh
        && (currentMeshIndex >= 0)
        && meshVisible(currentMeshIndex);
    const RenderFramePassRequests framePassRequests = collectRenderFramePassRequests();

    prepareDirtyBuffers(cb, framePassRequests, currentMeshIndex, drawCurrentMeshHighlight);

    m_frameTimer.start();

    const QSize sz = renderTarget()->pixelSize();

    QRhiResourceUpdateBatch *u = nullptr;
    auto updateQualityColorMapLut = [&](QRhiResourceUpdateBatch *&batch) {
        if (!m_qualityColorMapTexture || !m_rhi)
            return;
        const ColorMapRegistry &registry = ColorMapRegistry::instance();
        QString mapId = m_renderSettings.qualityHistogramColorMapId.trimmed().toLower();
        const bool isConstant = (mapId == QStringLiteral("constant"));
        if (!isConstant && (mapId.isEmpty() || !registry.hasMap(mapId)))
            mapId = registry.fallbackMapId();
        const bool invert = m_renderSettings.qualityHistogramInvertColorMap;
        const bool isolinesEnabled = m_renderSettings.qualityIsolinesEnabled;
        const int isolineCount = m_renderSettings.qualityIsolineCount;
        if (m_qualityColorMapTextureMapId != mapId || m_qualityColorMapTextureInverted != invert
            || m_qualityColorMapTextureIsolinesEnabled != isolinesEnabled
            || m_qualityColorMapTextureIsolineCount != isolineCount) {
            m_qualityColorMapTextureMapId = mapId;
            m_qualityColorMapTextureInverted = invert;
            m_qualityColorMapTextureIsolinesEnabled = isolinesEnabled;
            m_qualityColorMapTextureIsolineCount = isolineCount;
            m_qualityColorMapTextureUploadPending = true;
        }
        if (!m_qualityColorMapTextureUploadPending)
            return;

        constexpr int kLutSize = 1024;
        QImage lut(kLutSize, 1, QImage::Format_RGBA8888);
        uchar *bits = lut.bits();
        for (int i = 0; i < kLutSize; ++i) {
            float t = float(i) / float(kLutSize - 1);
            if (invert)
                t = 1.0f - t;
            QColor c;
            if (isConstant) {
                c = QColor(255, 255, 255);
            } else {
                c = registry.sampleQColor(mapId, t, 1.0f);
            }
            bits[i * 4 + 0] = uchar(c.red());
            bits[i * 4 + 1] = uchar(c.green());
            bits[i * 4 + 2] = uchar(c.blue());
            bits[i * 4 + 3] = 255u;
        }
        // Apply isolines: pairs of black pixels at regular intervals
        if (isolinesEnabled && isolineCount > 0) {
            for (int k = 1; k <= isolineCount; ++k) {
                const int center = int(std::round(float(k) / float(isolineCount + 1) * float(kLutSize - 1)));
                for (int offset = -1; offset <= 0; ++offset) {
                    const int idx = std::clamp(center + offset, 0, kLutSize - 1);
                    bits[idx * 4 + 0] = 0;
                    bits[idx * 4 + 1] = 0;
                    bits[idx * 4 + 2] = 0;
                    bits[idx * 4 + 3] = 255u;
                }
            }
        }
        if (!batch)
            batch = m_rhi->nextResourceUpdateBatch();
        QRhiTextureUploadEntry entry(0, 0, QRhiTextureSubresourceUploadDescription(lut));
        batch->uploadTexture(m_qualityColorMapTexture.get(), QRhiTextureUploadDescription({ entry }));
        m_qualityColorMapTextureUploadPending = false;
    };
    updateQualityColorMapLut(u);

    QMatrix4x4 proj;
    QMatrix4x4 view;
    QMatrix4x4 vp;
    const float aspect = float(sz.width()) / float(qMax(1, sz.height()));
    proj = m_trackball.projectionMatrix(aspect);

    view = m_trackball.viewMatrix();
    vp = proj * view;

    if (drawTrackballGizmo && m_trackballGizmoUbuf && m_trackballGizmoVbuf) {
        if (!u)
            u = m_rhi->nextResourceUpdateBatch();
        const auto &verts = trackballGizmoVertices();
        u->updateDynamicBuffer(
            m_trackballGizmoVbuf.get(),
            0,
            int(verts.size() * sizeof(float)),
            verts.data());

        float gizmoData[kTrackballGizmoUbufSize / sizeof(float)] = {};
        memcpy(gizmoData, vp.constData(), 64);
        const QVector3D center = m_trackball.center();
        gizmoData[16] = center.x();
        gizmoData[17] = center.y();
        gizmoData[18] = center.z();
        gizmoData[19] = m_trackball.gizmoWorldRadius();
        const QMatrix4x4 invView = view.inverted();
        QVector4D cameraH = invView * QVector4D(0.0f, 0.0f, 0.0f, 1.0f);
        if (std::abs(cameraH.w()) > 1e-8f)
            cameraH /= cameraH.w();
        gizmoData[20] = cameraH.x();
        gizmoData[21] = cameraH.y();
        gizmoData[22] = cameraH.z();
        gizmoData[23] = 0.38f; // back hemisphere shading floor
        u->updateDynamicBuffer(
            m_trackballGizmoUbuf.get(),
            0,
            kTrackballGizmoUbufSize,
            gizmoData);
    }

    // Light gizmo UBO (always update so light dir is current)
    if (m_lightGizmoUbuf && m_lightGizmoVbuf) {
        if (!u)
            u = m_rhi->nextResourceUpdateBatch();
        // Upload vertex data once (dynamic buffer, upload every frame is fine)
        const auto &lgVerts = lightGizmoVertices();
        u->updateDynamicBuffer(
            m_lightGizmoVbuf.get(),
            0,
            int(lgVerts.size() * sizeof(float)),
            lgVerts.data());

        const QVector3D lightDir = m_lightRotation.rotatedVector(QVector3D(0.0f, 0.0f, 1.0f));
        // UBO layout: mat4 (64 bytes unused/padding) + vec4 lightDir + vec4 params
        float lgData[kLightGizmoUbufSize / sizeof(float)] = {};
        // [0..15] = padding (mat4 not used by this shader but keeps struct aligned)
        lgData[16] = lightDir.x();
        lgData[17] = lightDir.y();
        lgData[18] = lightDir.z();
        lgData[19] = 0.0f;
        // params: x=radius(NDC), y=anchor NDC X, z=anchor NDC Y, w=aspect(w/h)
        const float gizmoR = 0.12f;
        const float anchorX = -1.0f + gizmoR * 1.5f;
        const float anchorY = -1.0f + gizmoR * 1.5f * (float(sz.width()) / float(qMax(1, sz.height())));
        lgData[20] = gizmoR;
        lgData[21] = anchorX;
        lgData[22] = anchorY;
        lgData[23] = float(sz.width()) / float(qMax(1, sz.height())); // aspect w/h
        u->updateDynamicBuffer(m_lightGizmoUbuf.get(), 0, kLightGizmoUbufSize, lgData);
    }
    const QVector3D frameLightDir =
        m_lightRotation.rotatedVector(QVector3D(0.0f, 0.0f, 1.0f));
    const bool depthPickPendingAtFrameStart = m_depthPickPending;

    if (m_depthPickPending) {
        if (u) {
            cb->resourceUpdate(u);
            u = nullptr;
        }
        executePendingDepthPick(cb, sz);
    }

    if (drawCurrentMeshHighlight) {
        if (u) {
            cb->resourceUpdate(u);
            u = nullptr;
        }
        renderCurrentMeshMask(cb, sz);
        processCurrentMeshMask(cb, sz);
    }

    if (m_sceneBackgroundUbuf) {
        float bgData[8] = {};
        bgData[0] = m_renderSettings.sceneBackgroundBottomColor.redF();
        bgData[1] = m_renderSettings.sceneBackgroundBottomColor.greenF();
        bgData[2] = m_renderSettings.sceneBackgroundBottomColor.blueF();
        bgData[3] = 1.0f;
        bgData[4] = m_renderSettings.sceneBackgroundTopColor.redF();
        bgData[5] = m_renderSettings.sceneBackgroundTopColor.greenF();
        bgData[6] = m_renderSettings.sceneBackgroundTopColor.blueF();
        bgData[7] = 1.0f;
        if (!u)
            u = m_rhi->nextResourceUpdateBatch();
        u->updateDynamicBuffer(m_sceneBackgroundUbuf.get(), 0, sizeof(bgData), bgData);
    }

    RenderFrameRequest frameRequest;
    frameRequest.viewMode = m_viewMode;
    frameRequest.pixelSize = sz;
    frameRequest.proj = proj;
    frameRequest.view = view;
    frameRequest.lightDir = frameLightDir;
    frameRequest.passes = framePassRequests;

    const RenderFramePlan framePlan = buildRenderFramePlan(frameRequest);
    const bool needMvpForFrame =
        framePlan.hasSceneDrawItems()
        || drawCurrentMeshHighlight
        || drawTrackballGizmo
        || depthPickPendingAtFrameStart
        || m_lightDragActive;

    if (framePlan.hasFillPass())
        renderSceneFillPrepasses(cb, framePlan);

    cb->beginPass(renderTarget(), m_renderSettings.sceneBackgroundBottomColor, { 1.0f, 0 }, u);
    cb->setViewport({ 0, 0, float(sz.width()), float(sz.height()) });

    if (m_sceneBackgroundPipeline && m_sceneBackgroundSrb) {
        cb->setGraphicsPipeline(m_sceneBackgroundPipeline.get());
        cb->setShaderResources(m_sceneBackgroundSrb.get());
        cb->draw(3);
    }

    if (framePlan.hasFillPass())
        renderSceneFillPass(cb, framePlan);

    renderSceneBufferItems(cb, framePlan, framePlan.wireItems);
    renderSceneBufferItems(cb, framePlan, framePlan.edgeItems);
    renderSceneBufferItems(cb, framePlan, framePlan.boundingBoxItems);
    renderSceneBufferItems(cb, framePlan, framePlan.pointItems);
    renderSceneDecoratorItems(cb, framePlan);

    if (drawTrackballGizmo && m_trackballGizmoPipeline && m_trackballGizmoVbuf && m_trackballGizmoSrb) {
        cb->setGraphicsPipeline(m_trackballGizmoPipeline.get());
        cb->setShaderResources(m_trackballGizmoSrb.get());
        cb->setViewport({ 0, 0, float(sz.width()), float(sz.height()) });
        const QRhiCommandBuffer::VertexInput gv(m_trackballGizmoVbuf.get(), 0);
        cb->setVertexInput(0, 1, &gv);
        cb->draw(m_trackballGizmoVertexCount);
    }

    // Draw light gizmo during drag (and always when light is non-default, but at minimum during drag)
    if (m_lightDragActive && m_lightGizmoPipeline && m_lightGizmoVbuf && m_lightGizmoSrb) {
        cb->setGraphicsPipeline(m_lightGizmoPipeline.get());
        cb->setShaderResources(m_lightGizmoSrb.get());
        cb->setViewport({ 0, 0, float(sz.width()), float(sz.height()) });
        const QRhiCommandBuffer::VertexInput lgv(m_lightGizmoVbuf.get(), 0);
        cb->setVertexInput(0, 1, &lgv);
        cb->draw(m_lightGizmoVertexCount);
    }

    if (drawCurrentMeshHighlight)
        drawCurrentMeshOutline(cb, sz);

    renderSceneSelectionItems(cb, framePlan);

    if (needMvpForFrame) {
        updateBoundingBoxCornersOverlayPlacement(vp, view, sz);
    }

    cb->endPass();

    const float cpuMs = m_frameTimer.nsecsElapsed() / 1e6f;

    const bool gpuTimingSupported = m_rhi->isFeatureSupported(QRhi::Timestamps);
    float gpuMs = 0.0f;
    bool gpuSampleValid = false;
    if (gpuTimingSupported) {
        // API returns elapsed seconds for the last completed frame.
        const double gpuSeconds = cb->lastCompletedGpuTime();
        if (gpuSeconds > 0.0) {
            gpuMs = static_cast<float>(gpuSeconds * 1000.0);
            gpuSampleValid = true;
        }
    }

    emit frameRendered(cpuMs, gpuMs, gpuTimingSupported, gpuSampleValid);
}
