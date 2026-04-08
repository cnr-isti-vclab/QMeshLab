#pragma once

#include "renderingsettings.h"
#include "viewtrackball.h"
#include <QRhiWidget>
#include <rhi/qrhi.h>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QPoint>
#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

class Document;
class RenderOverlayPanel;

class RenderWidget : public QRhiWidget
{
    Q_OBJECT
public:
    enum class ShadingMode {
        Smooth,
        Flat,
        Wireframe
    };

    explicit RenderWidget(Document *doc, QWidget *parent = nullptr);
    void setShadingMode(ShadingMode mode);
    void resetCameraToScene();

signals:
    void frameRendered(float cpuMs, float gpuMs, bool gpuTimingSupported, bool gpuSampleValid);
    void trackballCenterPicked(const QVector3D &worldPos);

protected:
    void initialize(QRhiCommandBuffer *cb) override;
    void render(QRhiCommandBuffer *cb) override;
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    void createOverlayButtons();
    void layoutOverlayButtons();
    void applySceneDefaultRenderModeIfNeeded();
    void refreshColorSourceAvailability();
    void ensureRenderResources();
    void ensureCurrentMeshMaskResources(const QSize &pixelSize);
    void ensureDepthPickResources(const QSize &pixelSize);
    void prepareDirtyBuffers(QRhiCommandBuffer *cb);
    void startCenterAnimation(const QVector3D &targetCenter);
    void cancelCenterAnimation();
    void advanceCenterAnimation();
    void updateCameraFrameIfNeeded();
    int fillGpuVariantIndexForCurrentSettings() const;
    int pointGpuVariantIndexForCurrentSettings() const;
    QRhiShaderResourceBindings *shaderResourcesForTexture(QRhiTexture *texture);
    void executePendingDepthPick(
        QRhiCommandBuffer *cb,
        const QMatrix4x4 &mvp,
        const QSize &pixelSize,
        int pointVariantIndex);
    void renderCurrentMeshMask(QRhiCommandBuffer *cb, const QSize &pixelSize);
    void processCurrentMeshMask(QRhiCommandBuffer *cb, const QSize &pixelSize);
    void drawCurrentMeshDebugView(QRhiCommandBuffer *cb, const QSize &pixelSize);
    void drawCurrentMeshOutline(QRhiCommandBuffer *cb, const QSize &pixelSize);

    Document *m_doc;
    QRhi *m_rhi = nullptr;
    bool m_applySceneDefaultRenderMode = true;
    bool m_reframeCameraRequested = true;
    bool m_resetTrackballRequested = false;
    bool m_centerAnimActive = false;
    QVector3D m_centerAnimStart;
    QVector3D m_centerAnimTarget;
    QElapsedTimer m_centerAnimTimer;
    int m_centerAnimDurationMs = 200;

    std::unique_ptr<QRhiBuffer> m_ubuf;
    std::unique_ptr<QRhiSampler> m_textureSampler;
    std::unique_ptr<QRhiTexture> m_fallbackTexture;
    bool m_fallbackTextureUploadPending = false;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unordered_map<QRhiTexture *, std::unique_ptr<QRhiShaderResourceBindings>> m_textureSrbs;
    std::unique_ptr<QRhiGraphicsPipeline> m_fillPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_wirePipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_bboxPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_pointsPipeline;
    std::array<std::unique_ptr<QRhiBuffer>, 4> m_decoratorUbufs;
    std::array<std::unique_ptr<QRhiShaderResourceBindings>, 4> m_decoratorSrbs;
    std::unique_ptr<QRhiGraphicsPipeline> m_decoratorPipeline;
    std::unique_ptr<QRhiTexture> m_depthPickTexture;
    std::unique_ptr<QRhiRenderBuffer> m_depthPickDepth;
    std::unique_ptr<QRhiTextureRenderTarget> m_depthPickRt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_depthPickRp;
    QSize m_depthPickSize;
    std::unique_ptr<QRhiShaderResourceBindings> m_depthPickSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_depthPickFillPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_depthPickPointsPipeline;
    bool m_depthPickPending = false;
    bool m_depthPickInFlight = false;
    QPoint m_depthPickPos;
    std::unique_ptr<QRhiReadbackResult> m_depthPickReadbackResult;
    std::unique_ptr<QRhiTexture> m_currentMaskTexture;
    std::unique_ptr<QRhiRenderBuffer> m_currentMaskDepth;
    std::unique_ptr<QRhiTextureRenderTarget> m_currentMaskRt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_currentMaskRp;
    std::unique_ptr<QRhiTexture> m_currentMaskBaseTexture;
    std::unique_ptr<QRhiTextureRenderTarget> m_currentMaskBaseRt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_currentMaskBaseRp;
    std::unique_ptr<QRhiTexture> m_currentMaskWorkTexture;
    std::unique_ptr<QRhiTextureRenderTarget> m_currentMaskWorkRt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_currentMaskWorkRp;
    QSize m_currentMaskSize;
    std::unique_ptr<QRhiGraphicsPipeline> m_currentMaskFillPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_currentMaskPointsPipeline;
    std::unique_ptr<QRhiBuffer> m_maskMorphCopyUbuf;
    std::unique_ptr<QRhiBuffer> m_maskMorphDilateUbuf;
    std::unique_ptr<QRhiBuffer> m_maskMorphErodeUbuf;
    std::unique_ptr<QRhiSampler> m_maskMorphSampler;
    std::unique_ptr<QRhiShaderResourceBindings> m_maskMorphMaskToBaseSrb;
    std::unique_ptr<QRhiShaderResourceBindings> m_maskMorphMaskToWorkSrb;
    std::unique_ptr<QRhiShaderResourceBindings> m_maskMorphWorkToMaskSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_maskMorphToBasePipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_maskMorphToWorkPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_maskMorphWorkToMaskPipeline;
    std::unique_ptr<QRhiBuffer> m_maskDebugUbuf;
    std::unique_ptr<QRhiShaderResourceBindings> m_maskDebugBaseSrb;
    std::unique_ptr<QRhiShaderResourceBindings> m_maskDebugWorkSrb;
    std::unique_ptr<QRhiShaderResourceBindings> m_maskDebugMaskSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_maskDebugPipeline;
    std::unique_ptr<QRhiBuffer> m_outlineUbuf;
    std::unique_ptr<QRhiSampler> m_outlineSampler;
    std::unique_ptr<QRhiShaderResourceBindings> m_outlineSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_outlinePipeline;
    std::unique_ptr<QRhiBuffer> m_trackballGizmoUbuf;
    std::unique_ptr<QRhiBuffer> m_trackballGizmoVbuf;
    std::unique_ptr<QRhiShaderResourceBindings> m_trackballGizmoSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_trackballGizmoPipeline;
    int m_trackballGizmoVertexCount = 0;
    ShadingMode m_shadingMode = ShadingMode::Smooth;
    RenderSettings m_renderSettings;
    RenderOverlayPanel *m_overlayPanel = nullptr;
    QElapsedTimer m_frameTimer;
    bool m_currentMaskFromPoints = false;
    ViewTrackball m_trackball;
};
