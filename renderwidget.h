#pragma once

#include "renderingsettings.h"
#include <QRhiWidget>
#include <rhi/qrhi.h>
#include <QElapsedTimer>
#include <QMatrix4x4>
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
    void drawCurrentMeshOutline(QRhiCommandBuffer *cb, const QSize &pixelSize);

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
    QSize m_currentMaskSize;
    std::unique_ptr<QRhiGraphicsPipeline> m_currentMaskFillPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_currentMaskPointsPipeline;
    std::unique_ptr<QRhiBuffer> m_outlineUbuf;
    std::unique_ptr<QRhiSampler> m_outlineSampler;
    std::unique_ptr<QRhiShaderResourceBindings> m_outlineSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_outlinePipeline;
    ShadingMode m_shadingMode = ShadingMode::Smooth;
    RenderSettings m_renderSettings;
    RenderOverlayPanel *m_overlayPanel = nullptr;
    QElapsedTimer m_frameTimer;

    // Simple orbit camera
    float m_rotX = -20.0f;
    float m_rotY = 0.0f;
    float m_distance = 3.0f;
    QPointF m_lastPos;
    QVector3D m_center;
    float m_radius = 1.0f;
};
