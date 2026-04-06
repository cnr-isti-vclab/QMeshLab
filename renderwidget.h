#pragma once

#include "renderingsettings.h"
#include <QRhiWidget>
#include <rhi/qrhi.h>
#include <QElapsedTimer>
#include <QImage>
#include <QMatrix4x4>
#include <memory>
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
    void rebuildBuffers();
    void prepareDirtyBuffers(QRhiCommandBuffer *cb);
    void renderCurrentMeshMask(QRhiCommandBuffer *cb, const QSize &pixelSize);
    void drawCurrentMeshOutline(QRhiCommandBuffer *cb, const QSize &pixelSize);

    Document *m_doc;
    QRhi *m_rhi = nullptr;

    // Per-mesh GPU data
    struct MeshGPU {
        int meshIndex = -1;
        std::unique_ptr<QRhiBuffer> vbuf;
        std::unique_ptr<QRhiBuffer> ibuf;
        std::unique_ptr<QRhiTexture> texture;
        std::unique_ptr<QRhiShaderResourceBindings> srb;
        int vertexCount = 0;
        int indexCount = 0;
        bool useTexture = false;
        std::vector<float> uploadData;
        std::vector<quint32> uploadIndices;
        QImage uploadTextureImage;
    };
    // Per-mesh wireframe overlay GPU data (expanded triangles with barycentrics)
    struct WireGPU {
        std::unique_ptr<QRhiBuffer> vbuf;
        int vertexCount = 0;
        std::vector<float> uploadData;
    };
    // Per-mesh bounding-box GPU data (24 vertices, LineList)
    struct BBoxGPU {
        std::unique_ptr<QRhiBuffer> vbuf;
        std::vector<float> uploadData;
    };
    // Per-mesh point cloud GPU data (position + optional mesh color, Points topology)
    struct PointsGPU {
        int meshIndex = -1;
        std::unique_ptr<QRhiBuffer> vbuf;
        int vertexCount = 0;
        std::vector<float> uploadData;
    };
    std::vector<MeshGPU> m_meshGPU;
    std::vector<WireGPU> m_wireGPU;
    std::vector<BBoxGPU> m_bboxGPU;
    std::vector<PointsGPU> m_pointsGPU;
    bool m_buffersDirty = true;
    bool m_logRebuildRequested = false;
    bool m_applySceneDefaultRenderMode = true;
    bool m_reframeCameraRequested = true;

    std::unique_ptr<QRhiBuffer> m_ubuf;
    std::unique_ptr<QRhiSampler> m_textureSampler;
    std::unique_ptr<QRhiTexture> m_fallbackTexture;
    bool m_fallbackTextureUploadPending = false;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
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
