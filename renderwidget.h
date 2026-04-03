#pragma once

#include <QRhiWidget>
#include <rhi/qrhi.h>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <memory>
#include <vector>

class Document;

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
    void frameRendered(float ms);

protected:
    void initialize(QRhiCommandBuffer *cb) override;
    void render(QRhiCommandBuffer *cb) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    void ensureRenderResources();
    void rebuildBuffers();
    void prepareDirtyBuffers(QRhiCommandBuffer *cb);

    Document *m_doc;
    QRhi *m_rhi = nullptr;

    // Per-mesh GPU data
    struct MeshGPU {
        std::unique_ptr<QRhiBuffer> vbuf;
        std::unique_ptr<QRhiBuffer> ibuf;
        int vertexCount = 0;
        int indexCount = 0;
        std::vector<float> uploadData;
        std::vector<quint32> uploadIndices;
    };
    std::vector<MeshGPU> m_meshGPU;
    bool m_buffersDirty = true;
    bool m_logRebuildRequested = false;

    std::unique_ptr<QRhiBuffer> m_ubuf;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    ShadingMode m_shadingMode = ShadingMode::Smooth;
    QElapsedTimer m_frameTimer;

    // Simple orbit camera
    float m_rotX = -20.0f;
    float m_rotY = 0.0f;
    float m_distance = 3.0f;
    QPointF m_lastPos;
    QVector3D m_center;
    float m_radius = 1.0f;
};
