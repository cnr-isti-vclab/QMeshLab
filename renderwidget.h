#pragma once

#include "renderingsettings.h"
#include <QRhiWidget>
#include <rhi/qrhi.h>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QQuaternion>
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
    ShadingMode shadingMode() const { return m_shadingMode; }

signals:
    void frameRendered(float cpuMs, float gpuMs, bool gpuTimingSupported, bool gpuSampleValid);

protected:
    void initialize(QRhiCommandBuffer *cb) override;
    void render(QRhiCommandBuffer *cb) override;
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
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
    void prepareDirtyBuffers(QRhiCommandBuffer *cb);
    void updateCameraFrameIfNeeded();
    int fillGpuVariantIndexForCurrentSettings() const;
    int pointGpuVariantIndexForCurrentSettings() const;
    QRhiShaderResourceBindings *shaderResourcesForTexture(QRhiTexture *texture);
    void renderCurrentMeshMask(QRhiCommandBuffer *cb, const QSize &pixelSize);
    void processCurrentMeshMask(QRhiCommandBuffer *cb, const QSize &pixelSize);
    void drawCurrentMeshDebugView(QRhiCommandBuffer *cb, const QSize &pixelSize);
    void drawCurrentMeshOutline(QRhiCommandBuffer *cb, const QSize &pixelSize);
    QVector3D projectOnArcball(const QPointF &pos) const;
    QVector3D cameraRight() const;
    QVector3D cameraUp() const;

    enum class NavigationMode {
        None,
        Rotate,
        Pan
    };

    Document *m_doc;
    QRhi *m_rhi = nullptr;
    bool m_applySceneDefaultRenderMode = true;
    bool m_reframeCameraRequested = true;

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
    ShadingMode m_shadingMode = ShadingMode::Smooth;
    RenderSettings m_renderSettings;
    RenderOverlayPanel *m_overlayPanel = nullptr;
    QElapsedTimer m_frameTimer;
    bool m_currentMaskFromPoints = false;
    NavigationMode m_navigationMode = NavigationMode::None;
    QPointF m_lastMousePos;
    QVector3D m_lastArcballVec = QVector3D(0.0f, 0.0f, 1.0f);
    QQuaternion m_trackballRotation;

    // Simple orbit camera
    float m_distance = 3.0f;
    QVector3D m_center;
    float m_radius = 1.0f;
};
