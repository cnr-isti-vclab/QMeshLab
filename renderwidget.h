#pragma once

#include <QRhiWidget>
#include <rhi/qrhi.h>
#include <QElapsedTimer>
#include <memory>

class RenderWidget : public QRhiWidget
{
    Q_OBJECT
public:
    explicit RenderWidget(QWidget *parent = nullptr);

signals:
    void frameRendered(float ms);

protected:
    void initialize(QRhiCommandBuffer *cb) override;
    void render(QRhiCommandBuffer *cb) override;

private:
    QRhi *m_rhi = nullptr;
    std::unique_ptr<QRhiBuffer> m_vbuf;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    QElapsedTimer m_frameTimer;
};
