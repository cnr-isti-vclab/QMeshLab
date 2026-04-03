#include "renderwidget.h"
#include "document.h"
#include <QFile>
#include <QIcon>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QToolButton>
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
    createOverlayButtons();

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
    if (mode == ShadingMode::Wireframe) {
        m_showWire = true;
        m_showFill = true;
        if (m_wireButton) m_wireButton->setChecked(true);
        if (m_fillButton) m_fillButton->setChecked(true);
        update();
        return;
    }

    if (m_shadingMode == mode)
        return;

    m_shadingMode = mode;
    m_pipeline.reset();
    m_buffersDirty = true;
    m_logRebuildRequested = true;
    update();
}

void RenderWidget::createOverlayButtons()
{
    auto makeButton = [this](const QString &iconPath, const QString &tooltip) {
        auto *btn = new QToolButton(this);
        btn->setIcon(QIcon(iconPath));
        btn->setToolTip(tooltip);
        btn->setCheckable(true);
        btn->setAutoRaise(false);
        btn->setIconSize(QSize(32, 32));
        btn->setFixedSize(32, 32);
        btn->setStyleSheet(QStringLiteral(
            "QToolButton { background: rgba(250,250,250,210); border: 1px solid rgba(40,40,40,160); border-radius: 4px; }"
            "QToolButton:checked { background: rgba(60,130,220,220); }"
            "QToolButton:hover { background: rgba(220,230,245,220); }"));
        return btn;
    };

    m_bboxButton = makeButton(QStringLiteral(":/img/box.png"), tr("Bounding Box"));
    m_pointsButton = makeButton(QStringLiteral(":/img/points.png"), tr("Points (stub)"));
    m_wireButton = makeButton(QStringLiteral(":/img/wire.png"), tr("Wireframe pass"));
    m_fillButton = makeButton(QStringLiteral(":/img/flat.png"), tr("Fill pass"));

    m_bboxButton->setChecked(m_showBoundingBox);
    m_pointsButton->setChecked(m_showPoints);
    m_wireButton->setChecked(m_showWire);
    m_fillButton->setChecked(m_showFill);

    connect(m_bboxButton, &QToolButton::toggled, this, [this](bool checked) {
        m_showBoundingBox = checked;
        update();
    });
    connect(m_pointsButton, &QToolButton::toggled, this, [this](bool checked) {
        m_showPoints = checked;
        m_doc->writeLog(tr("[render] Points pass is not implemented yet"), Document::LogSource::Application);
        update();
    });
    connect(m_wireButton, &QToolButton::toggled, this, [this](bool checked) {
        m_showWire = checked;
        m_pipeline.reset();
        m_buffersDirty = true;
        m_logRebuildRequested = true;
        update();
    });
    connect(m_fillButton, &QToolButton::toggled, this, [this](bool checked) {
        m_showFill = checked;
        m_pipeline.reset();
        m_buffersDirty = true;
        m_logRebuildRequested = true;
        update();
    });

    layoutOverlayButtons();
}

void RenderWidget::layoutOverlayButtons()
{
    const int x0 = 8;
    const int y0 = 8;
    const int s = 32;
    const int gap = 4;

    if (m_bboxButton) m_bboxButton->move(x0 + 0 * (s + gap), y0);
    if (m_pointsButton) m_pointsButton->move(x0 + 1 * (s + gap), y0);
    if (m_wireButton) m_wireButton->move(x0 + 2 * (s + gap), y0);
    if (m_fillButton) m_fillButton->move(x0 + 3 * (s + gap), y0);
}

void RenderWidget::ensureRenderResources()
{
    if (m_rhi != rhi()) {
        m_rhi = rhi();
        m_pipeline.reset();
        m_bboxPipeline.reset();
        m_srb.reset();
        m_ubuf.reset();
        m_meshGPU.clear();
        m_bboxGPU.clear();
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

        const bool useWirePipeline = m_showWire;

        QString vsPath;
        QString fsPath;
        if (useWirePipeline) {
            vsPath = QStringLiteral(":/shaders/wireframe.vert.qsb");
            fsPath = m_showFill
                ? QStringLiteral(":/shaders/wireframe.frag.qsb")
                : QStringLiteral(":/shaders/wireframe_lines.frag.qsb");
        } else {
            switch (m_shadingMode) {
            case ShadingMode::Smooth:
                vsPath = QStringLiteral(":/shaders/color.vert.qsb");
                fsPath = QStringLiteral(":/shaders/color.frag.qsb");
                break;
            case ShadingMode::Flat:
                vsPath = QStringLiteral(":/shaders/flat.vert.qsb");
                fsPath = QStringLiteral(":/shaders/flat.frag.qsb");
                break;
            case ShadingMode::Wireframe:
                vsPath = QStringLiteral(":/shaders/color.vert.qsb");
                fsPath = QStringLiteral(":/shaders/color.frag.qsb");
                break;
            }
        }

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
        if (useWirePipeline) {
            inputLayout.setAttributes({
                { 0, 0, QRhiVertexInputAttribute::Float3, 0 },             // position
                { 0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float) } // barycentric
            });
        } else if (m_shadingMode == ShadingMode::Flat) {
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

    if (!m_bboxPipeline) {
        m_bboxPipeline.reset(m_rhi->newGraphicsPipeline());

        QShader vs = loadShader(QStringLiteral(":/shaders/bbox.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/bbox.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load bbox shaders");
            m_bboxPipeline.reset();
            return;
        }

        m_bboxPipeline->setShaderStages({
            { QRhiShaderStage::Vertex, vs },
            { QRhiShaderStage::Fragment, fs }
        });
        m_bboxPipeline->setTopology(QRhiGraphicsPipeline::Lines);
        m_bboxPipeline->setDepthTest(true);
        m_bboxPipeline->setDepthWrite(false);
        m_bboxPipeline->setCullMode(QRhiGraphicsPipeline::None);

        QRhiVertexInputLayout bboxLayout;
        bboxLayout.setBindings({ { 3 * sizeof(float) } });
        bboxLayout.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, 0 } });
        m_bboxPipeline->setVertexInputLayout(bboxLayout);
        m_bboxPipeline->setShaderResourceBindings(m_srb.get());
        m_bboxPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

        if (!m_bboxPipeline->create()) {
            qWarning("Failed to create bbox pipeline");
            m_bboxPipeline.reset();
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

        MeshGPU mg;

        if (m_showWire) {
            const int vertexCount = mesh.FN() * 3;
            const int vertBytes = vertexCount * 6 * sizeof(float);
            auto vbuf = std::unique_ptr<QRhiBuffer>(
                m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, vertBytes));
            vbuf->create();

            std::vector<float> vdata(vertexCount * 6);
            static constexpr float barycentrics[3][3] = {
                { 1.0f, 0.0f, 0.0f },
                { 0.0f, 1.0f, 0.0f },
                { 0.0f, 0.0f, 1.0f }
            };

            for (int i = 0; i < mesh.FN(); ++i) {
                const auto &f = mesh.face[i];
                for (int corner = 0; corner < 3; ++corner) {
                    const auto *vertex = f.cV(corner);
                    const int base = (i * 3 + corner) * 6;
                    vdata[base + 0] = vertex->cP()[0];
                    vdata[base + 1] = vertex->cP()[1];
                    vdata[base + 2] = vertex->cP()[2];
                    vdata[base + 3] = barycentrics[corner][0];
                    vdata[base + 4] = barycentrics[corner][1];
                    vdata[base + 5] = barycentrics[corner][2];
                }
            }

            mg.vbuf = std::move(vbuf);
            mg.vertexCount = vertexCount;
            mg.uploadData = std::move(vdata);
            m_meshGPU.push_back(std::move(mg));
            continue;
        }

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

        mg.vbuf = std::move(vbuf);
        mg.ibuf = std::move(ibuf);
        mg.vertexCount = mesh.VN();
        mg.indexCount = idxCount;
        mg.uploadData = std::move(vdata);
        mg.uploadIndices = std::move(idata);
        m_meshGPU.push_back(std::move(mg));
    }

    // Build per-mesh bounding box line buffers (12 edges = 24 vertices)
    m_bboxGPU.clear();
    for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
        const VCGMesh &mesh = m_doc->mesh(mi).mesh;
        if (mesh.bbox.IsNull()) continue;

        const auto &mn = mesh.bbox.min;
        const auto &mx = mesh.bbox.max;

        // clang-format off
        std::vector<float> bd = {
            // bottom face
            mn[0],mn[1],mn[2],  mx[0],mn[1],mn[2],
            mx[0],mn[1],mn[2],  mx[0],mx[1],mn[2],
            mx[0],mx[1],mn[2],  mn[0],mx[1],mn[2],
            mn[0],mx[1],mn[2],  mn[0],mn[1],mn[2],
            // top face
            mn[0],mn[1],mx[2],  mx[0],mn[1],mx[2],
            mx[0],mn[1],mx[2],  mx[0],mx[1],mx[2],
            mx[0],mx[1],mx[2],  mn[0],mx[1],mx[2],
            mn[0],mx[1],mx[2],  mn[0],mn[1],mx[2],
            // vertical edges
            mn[0],mn[1],mn[2],  mn[0],mn[1],mx[2],
            mx[0],mn[1],mn[2],  mx[0],mn[1],mx[2],
            mx[0],mx[1],mn[2],  mx[0],mx[1],mx[2],
            mn[0],mx[1],mn[2],  mn[0],mx[1],mx[2],
        };
        // clang-format on

        auto bvbuf = std::unique_ptr<QRhiBuffer>(
            m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                             static_cast<quint32>(bd.size() * sizeof(float))));
        bvbuf->create();

        BBoxGPU bg;
        bg.vbuf = std::move(bvbuf);
        bg.uploadData = std::move(bd);
        m_bboxGPU.push_back(std::move(bg));
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

    if (!m_meshGPU.empty() || !m_bboxGPU.empty()) {
        QElapsedTimer uploadTimer;
        uploadTimer.start();
        QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
        for (auto &mg : m_meshGPU) {
            u->uploadStaticBuffer(mg.vbuf.get(), mg.uploadData.data());
            if (mg.ibuf && !mg.uploadIndices.empty())
                u->uploadStaticBuffer(mg.ibuf.get(), mg.uploadIndices.data());
            ++uploadedMeshes;
            uploadedVertices += static_cast<int>(mg.uploadData.size() / 6);
            uploadedTriangles += (mg.indexCount > 0 ? mg.indexCount : mg.vertexCount) / 3;
            mg.uploadData.clear();
            mg.uploadIndices.clear();
        }
        for (auto &bg : m_bboxGPU) {
            if (!bg.uploadData.empty()) {
                u->uploadStaticBuffer(bg.vbuf.get(), bg.uploadData.data());
                bg.uploadData.clear();
            }
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

    if (!m_showFill && !m_showWire && !m_showBoundingBox)
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
            if (mg.indexCount == 0 && mg.vertexCount == 0)
                continue;
            const QRhiCommandBuffer::VertexInput vbufBinding(mg.vbuf.get(), 0);
            if (mg.indexCount > 0) {
                cb->setVertexInput(0, 1, &vbufBinding, mg.ibuf.get(), 0, QRhiCommandBuffer::IndexUInt32);
                cb->drawIndexed(mg.indexCount);
            } else {
                cb->setVertexInput(0, 1, &vbufBinding);
                cb->draw(mg.vertexCount);
            }
        }
    }

    if (m_showBoundingBox && m_bboxPipeline && !m_bboxGPU.empty()) {
        cb->setGraphicsPipeline(m_bboxPipeline.get());
        cb->setShaderResources();
        for (const auto &bg : m_bboxGPU) {
            if (!bg.vbuf) continue;
            const QRhiCommandBuffer::VertexInput bv(bg.vbuf.get(), 0);
            cb->setVertexInput(0, 1, &bv);
            cb->draw(24);
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

void RenderWidget::resizeEvent(QResizeEvent *e)
{
    QRhiWidget::resizeEvent(e);
    layoutOverlayButtons();
}
