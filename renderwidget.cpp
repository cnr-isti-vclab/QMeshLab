#include "renderwidget.h"
#include <QFile>

static QShader loadShader(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("Failed to open shader: %s", qPrintable(path));
        return {};
    }
    return QShader::fromSerialized(f.readAll());
}

RenderWidget::RenderWidget(QWidget *parent)
    : QRhiWidget(parent)
{
}

void RenderWidget::initialize(QRhiCommandBuffer *cb)
{
    if (m_rhi != rhi()) {
        m_rhi = rhi();
        m_pipeline.reset();
        m_srb.reset();
        m_vbuf.reset();
    }

    if (m_pipeline)
        return;

    if (!m_rhi) {
        qWarning("QRhi not available");
        return;
    }

    // Interleaved vertex data: position(x,y,z) + color(r,g,b)
    static const float vertexData[] = {
         0.0f,  0.5f, 0.0f,   1.0f, 0.0f, 0.0f,   // top - red
        -0.5f, -0.5f, 0.0f,   0.0f, 1.0f, 0.0f,   // bottom-left - green
         0.5f, -0.5f, 0.0f,   0.0f, 0.0f, 1.0f    // bottom-right - blue
    };

    m_vbuf.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(vertexData)));
    if (!m_vbuf->create()) {
        qWarning("Failed to create vertex buffer");
        return;
    }

    QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
    u->uploadStaticBuffer(m_vbuf.get(), vertexData);
    cb->resourceUpdate(u);

    m_srb.reset(m_rhi->newShaderResourceBindings());
    m_srb->create();

    m_pipeline.reset(m_rhi->newGraphicsPipeline());

    QShader vs = loadShader(QStringLiteral(":/shaders/color.vert.qsb"));
    QShader fs = loadShader(QStringLiteral(":/shaders/color.frag.qsb"));
    if (!vs.isValid() || !fs.isValid()) {
        qWarning("Failed to load shaders");
        m_pipeline.reset();
        return;
    }

    m_pipeline->setShaderStages({
        { QRhiShaderStage::Vertex, vs },
        { QRhiShaderStage::Fragment, fs }
    });

    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({ { 6 * sizeof(float) } });
    inputLayout.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float3, 0 },                 // position
        { 0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float) }  // color
    });
    m_pipeline->setVertexInputLayout(inputLayout);
    m_pipeline->setShaderResourceBindings(m_srb.get());
    m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

    if (!m_pipeline->create()) {
        qWarning("Failed to create graphics pipeline");
        m_pipeline.reset();
    }
}

void RenderWidget::render(QRhiCommandBuffer *cb)
{
    m_frameTimer.start();

    cb->beginPass(renderTarget(), QColor(40, 40, 40), { 1.0f, 0 });

    if (m_pipeline) {
        cb->setGraphicsPipeline(m_pipeline.get());
        const QSize sz = renderTarget()->pixelSize();
        cb->setViewport({ 0, 0, float(sz.width()), float(sz.height()) });
        const QRhiCommandBuffer::VertexInput vbufBinding(m_vbuf.get(), 0);
        cb->setVertexInput(0, 1, &vbufBinding);
        cb->draw(3);
    }

    cb->endPass();

    const float ms = m_frameTimer.nsecsElapsed() / 1e6f;
    emit frameRendered(ms);
}
