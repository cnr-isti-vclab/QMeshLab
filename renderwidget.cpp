#include "renderwidget.h"
#include "document.h"
#include <QFile>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>

static QShader loadShader(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("Failed to open shader: %s", qPrintable(path));
        return {};
    }
    return QShader::fromSerialized(f.readAll());
}

RenderWidget::RenderWidget(Document *doc, QWidget *parent)
    : QRhiWidget(parent), m_doc(doc)
{
    connect(m_doc, &Document::meshAdded, this, [this](int) {
        m_buffersDirty = true;
        m_logRebuildRequested = true;
        update();
    });
    connect(m_doc, &Document::meshRemoved, this, [this](int) {
        m_buffersDirty = true;
        m_logRebuildRequested = true;
        update();
    });
}

void RenderWidget::setShadingMode(ShadingMode mode)
{
    if (m_shadingMode == mode)
        return;

    m_shadingMode = mode;
    m_pipeline.reset();
    update();
}

void RenderWidget::ensureRenderResources()
{
    if (m_rhi != rhi()) {
        m_rhi = rhi();
        m_pipeline.reset();
        m_srb.reset();
        m_ubuf.reset();
        m_meshGPU.clear();
        m_buffersDirty = true;
    }

    if (!m_rhi || !renderTarget())
        return;

    if (!m_ubuf) {
        // Uniform buffer: mat4 mvp (64) + mat4 modelView (64) + mat3 std140 (48) = 176 bytes
        m_ubuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 176));
        m_ubuf->create();
    }

    if (!m_srb) {
        m_srb.reset(m_rhi->newShaderResourceBindings());
        m_srb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, m_ubuf.get())
        });
        m_srb->create();
    }

    if (!m_pipeline) {
        m_pipeline.reset(m_rhi->newGraphicsPipeline());

        const QString vsPath = (m_shadingMode == ShadingMode::Flat)
            ? QStringLiteral(":/shaders/flat.vert.qsb")
            : QStringLiteral(":/shaders/color.vert.qsb");
        const QString fsPath = (m_shadingMode == ShadingMode::Flat)
            ? QStringLiteral(":/shaders/flat.frag.qsb")
            : QStringLiteral(":/shaders/color.frag.qsb");

        QShader vs = loadShader(vsPath);
        QShader fs = loadShader(fsPath);
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load shaders");
            m_pipeline.reset();
            return;
        }

        m_pipeline->setShaderStages({
            { QRhiShaderStage::Vertex, vs },
            { QRhiShaderStage::Fragment, fs }
        });

        m_pipeline->setDepthTest(true);
        m_pipeline->setDepthWrite(true);
        m_pipeline->setCullMode(QRhiGraphicsPipeline::Back);

        QRhiVertexInputLayout inputLayout;
        inputLayout.setBindings({ { 6 * sizeof(float) } });
        if (m_shadingMode == ShadingMode::Flat) {
            inputLayout.setAttributes({
                { 0, 0, QRhiVertexInputAttribute::Float3, 0 }              // position
            });
        } else {
            inputLayout.setAttributes({
                { 0, 0, QRhiVertexInputAttribute::Float3, 0 },             // position
                { 0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float) } // normal
            });
        }
        m_pipeline->setVertexInputLayout(inputLayout);
        m_pipeline->setShaderResourceBindings(m_srb.get());
        m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

        if (!m_pipeline->create()) {
            qWarning("Failed to create graphics pipeline");
            m_pipeline.reset();
        }
    }
}

void RenderWidget::rebuildBuffers()
{
    m_meshGPU.clear();
    if (!m_rhi || m_doc->meshCount() == 0)
        return;

    // Compute global bounding box for camera framing
    vcg::Box3f bbox;
    for (int i = 0; i < m_doc->meshCount(); ++i)
        bbox.Add(m_doc->mesh(i).mesh.bbox);
    auto c = bbox.Center();
    m_center = QVector3D(c[0], c[1], c[2]);
    m_radius = bbox.Diag() / 2.0f;
    m_distance = m_radius * 3.0f;

    for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
        const VCGMesh &mesh = m_doc->mesh(mi).mesh;
        if (mesh.FN() == 0) continue;

        // Build interleaved vertex buffer: pos(3f) + normal(3f)
        const int vertBytes = mesh.VN() * 6 * sizeof(float);
        auto vbuf = std::unique_ptr<QRhiBuffer>(
            m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, vertBytes));
        vbuf->create();

        std::vector<float> vdata(mesh.VN() * 6);
        for (int i = 0; i < mesh.VN(); ++i) {
            const auto &v = mesh.vert[i];
            vdata[i * 6 + 0] = v.P()[0];
            vdata[i * 6 + 1] = v.P()[1];
            vdata[i * 6 + 2] = v.P()[2];
            vdata[i * 6 + 3] = v.N()[0];
            vdata[i * 6 + 4] = v.N()[1];
            vdata[i * 6 + 5] = v.N()[2];
        }

        // Build index buffer
        const int idxCount = mesh.FN() * 3;
        const int idxBytes = idxCount * sizeof(quint32);
        auto ibuf = std::unique_ptr<QRhiBuffer>(
            m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, idxBytes));
        ibuf->create();

        std::vector<quint32> idata(idxCount);
        for (int i = 0; i < mesh.FN(); ++i) {
            const auto &f = mesh.face[i];
            idata[i * 3 + 0] = static_cast<quint32>(vcg::tri::Index(mesh, f.cV(0)));
            idata[i * 3 + 1] = static_cast<quint32>(vcg::tri::Index(mesh, f.cV(1)));
            idata[i * 3 + 2] = static_cast<quint32>(vcg::tri::Index(mesh, f.cV(2)));
        }

        MeshGPU mg;
        mg.vbuf = std::move(vbuf);
        mg.ibuf = std::move(ibuf);
        mg.indexCount = idxCount;
        mg.uploadData = std::move(vdata);
        mg.uploadIndices = std::move(idata);
        m_meshGPU.push_back(std::move(mg));
    }
}

void RenderWidget::prepareDirtyBuffers(QRhiCommandBuffer *cb)
{
    if (!m_buffersDirty || !m_rhi)
        return;

    const bool logRebuild = m_logRebuildRequested;
    QElapsedTimer rebuildTimer;
    rebuildTimer.start();
    rebuildBuffers();
    const qint64 rebuildMs = rebuildTimer.elapsed();

    qint64 uploadMs = 0;
    int uploadedMeshes = 0;
    int uploadedVertices = 0;
    int uploadedTriangles = 0;

    if (!m_meshGPU.empty()) {
        QElapsedTimer uploadTimer;
        uploadTimer.start();
        QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
        for (auto &mg : m_meshGPU) {
            u->uploadStaticBuffer(mg.vbuf.get(), mg.uploadData.data());
            u->uploadStaticBuffer(mg.ibuf.get(), mg.uploadIndices.data());
            ++uploadedMeshes;
            uploadedVertices += static_cast<int>(mg.uploadData.size() / 6);
            uploadedTriangles += mg.indexCount / 3;
            mg.uploadData.clear();
            mg.uploadIndices.clear();
        }
        cb->resourceUpdate(u);
        uploadMs = uploadTimer.elapsed();
    }

    if (logRebuild) {
        m_doc->writeLog(tr("[render] Prepared buffers in %1 ms, uploaded in %2 ms (%3 meshes, %4 vertices, %5 triangles)")
            .arg(rebuildMs)
            .arg(uploadMs)
            .arg(uploadedMeshes)
            .arg(uploadedVertices)
            .arg(uploadedTriangles),
            Document::LogSource::Application);
        m_logRebuildRequested = false;
    }

    m_buffersDirty = false;
}

void RenderWidget::initialize(QRhiCommandBuffer *cb)
{
    ensureRenderResources();
    if (!m_rhi) {
        qWarning("QRhi not available");
        return;
    }

    prepareDirtyBuffers(cb);
}

void RenderWidget::render(QRhiCommandBuffer *cb)
{
    ensureRenderResources();
    if (!m_rhi || !m_ubuf)
        return;

    prepareDirtyBuffers(cb);

    m_frameTimer.start();

    const QSize sz = renderTarget()->pixelSize();
    const float aspect = sz.width() / float(sz.height());

    QMatrix4x4 proj;
    proj.perspective(45.0f, aspect, 0.01f * m_radius, 100.0f * m_radius);

    QMatrix4x4 view;
    view.translate(0, 0, -m_distance);
    view.rotate(m_rotX, 1, 0, 0);
    view.rotate(m_rotY, 0, 1, 0);
    view.translate(-m_center);

    QMatrix4x4 modelView = view;

    QMatrix4x4 mvp = proj * view;
    QMatrix3x3 normalMat = modelView.normalMatrix();

    // Pack uniform: mat4 mvp + mat4 modelView + mat3 as 3 vec4 (std140)
    float ubufData[44]; // 176 / 4
    memcpy(ubufData, mvp.constData(), 64);
    memcpy(ubufData + 16, modelView.constData(), 64);
    // std140: mat3 is stored as 3 columns of vec4
    const float *n = normalMat.constData();
    ubufData[32] = n[0]; ubufData[33] = n[1]; ubufData[34] = n[2]; ubufData[35] = 0;
    ubufData[36] = n[3]; ubufData[37] = n[4]; ubufData[38] = n[5]; ubufData[39] = 0;
    ubufData[40] = n[6]; ubufData[41] = n[7]; ubufData[42] = n[8]; ubufData[43] = 0;

    QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
    u->updateDynamicBuffer(m_ubuf.get(), 0, 176, ubufData);

    cb->beginPass(renderTarget(), QColor(40, 40, 40), { 1.0f, 0 }, u);

    if (m_pipeline) {
        cb->setGraphicsPipeline(m_pipeline.get());
        cb->setViewport({ 0, 0, float(sz.width()), float(sz.height()) });
        cb->setShaderResources();

        for (const auto &mg : m_meshGPU) {
            if (mg.indexCount == 0) continue;
            const QRhiCommandBuffer::VertexInput vbufBinding(mg.vbuf.get(), 0);
            cb->setVertexInput(0, 1, &vbufBinding, mg.ibuf.get(), 0, QRhiCommandBuffer::IndexUInt32);
            cb->drawIndexed(mg.indexCount);
        }
    }

    cb->endPass();

    const float ms = m_frameTimer.nsecsElapsed() / 1e6f;
    emit frameRendered(ms);
}

void RenderWidget::mousePressEvent(QMouseEvent *e)
{
    m_lastPos = e->position();
}

void RenderWidget::mouseMoveEvent(QMouseEvent *e)
{
    QPointF delta = e->position() - m_lastPos;
    m_lastPos = e->position();
    if (e->buttons() & Qt::LeftButton) {
        m_rotY += delta.x() * 0.5f;
        m_rotX += delta.y() * 0.5f;
        m_rotX = qBound(-90.0f, m_rotX, 90.0f);
        update();
    }
}

void RenderWidget::wheelEvent(QWheelEvent *e)
{
    m_distance *= (1.0f - e->angleDelta().y() * 0.001f);
    m_distance = qMax(m_distance, 0.01f * m_radius);
    update();
}
