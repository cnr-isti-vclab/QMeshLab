#include "renderwidget.h"
#include "document.h"
#include "renderwidget_internal.h"
#include <wrap/io_trimesh/io_mask.h>
#include <QLabel>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace RenderWidgetInternal;

namespace {
constexpr int kUvBackgroundUbufSize = 64;
constexpr float kUvMinZoom = 0.05f;
constexpr float kUvMaxZoom = 5000.0f;
}

void RenderWidget::syncUvCacheWithDocument()
{
    if (!m_doc) {
        m_uvMeshGpu.clear();
        return;
    }

    std::unordered_map<std::uint64_t, bool> aliveMeshIds;
    aliveMeshIds.reserve(size_t(m_doc->meshCount()));
    for (int i = 0; i < m_doc->meshCount(); ++i)
        aliveMeshIds.emplace(m_doc->mesh(i).meshId, true);

    for (auto it = m_uvMeshGpu.begin(); it != m_uvMeshGpu.end();) {
        if (aliveMeshIds.find(it->first) == aliveMeshIds.end())
            it = m_uvMeshGpu.erase(it);
        else
            ++it;
    }
}

bool RenderWidget::meshHasParametrization(int meshIndex) const
{
    if (!m_doc || meshIndex < 0 || meshIndex >= m_doc->meshCount())
        return false;

    const auto &entry = m_doc->mesh(meshIndex);
    if (entry.mesh.FN() <= 0)
        return false;
    const int mask = entry.ioMask;
    const bool hasWedgeTex = (mask & vcg::tri::io::Mask::IOM_WEDGTEXCOORD) != 0;
    const bool hasVertexTex = (mask & vcg::tri::io::Mask::IOM_VERTTEXCOORD) != 0;
    return hasWedgeTex || hasVertexTex;
}

bool RenderWidget::ensureUvMeshResources(int meshIndex, QRhiCommandBuffer *cb)
{
    if (!m_doc || !m_rhi || !cb || meshIndex < 0 || meshIndex >= m_doc->meshCount())
        return false;

    const auto &entry = m_doc->mesh(meshIndex);
    auto &gpu = m_uvMeshGpu[entry.meshId];
    if (gpu.valid
        && gpu.geometryRevision == entry.geometryRevision
        && gpu.materialRevision == entry.materialRevision) {
        return true;
    }

    gpu = UvMeshGpu {};
    gpu.geometryRevision = entry.geometryRevision;
    gpu.materialRevision = entry.materialRevision;
    if (!meshHasParametrization(meshIndex))
        return false;

    const VCGMesh &mesh = entry.mesh;
    const int mask = entry.ioMask;
    const bool hasWedgeTex = (mask & vcg::tri::io::Mask::IOM_WEDGTEXCOORD) != 0;
    const bool hasVertexTex = !hasWedgeTex && ((mask & vcg::tri::io::Mask::IOM_VERTTEXCOORD) != 0);
    const bool hasVertexColors = (mask & vcg::tri::io::Mask::IOM_VERTCOLOR) != 0;
    const bool hasFaceColors = (mask & vcg::tri::io::Mask::IOM_FACECOLOR) != 0;

    auto uvForCorner = [&](const VCGMesh::FaceType &f, int corner, QVector2D &outUv) -> bool {
        float u = 0.0f;
        float v = 0.0f;
        if (hasWedgeTex) {
            const auto &wt = f.cWT(corner);
            u = wt.U();
            v = wt.V();
        } else if (hasVertexTex) {
            const auto *vertex = f.cV(corner);
            if (!vertex)
                return false;
            const auto &vt = vertex->cT();
            u = vt.U();
            v = vt.V();
        } else {
            return false;
        }
        if (!std::isfinite(u) || !std::isfinite(v))
            return false;
        outUv = QVector2D(u, v);
        return true;
    };

    std::vector<float> wireData;
    wireData.reserve(size_t(mesh.FN()) * 18);
    std::vector<float> boundaryEdgeData;
    boundaryEdgeData.reserve(size_t(mesh.FN()) * 6);
    std::vector<float> textureSeamData;
    textureSeamData.reserve(size_t(mesh.FN()) * 6);
    std::array<std::vector<float>, 3> fillData;
    for (auto &v : fillData)
        v.reserve(size_t(mesh.FN()) * 3 * kFillVertexStrideFloats);
    std::array<std::vector<float>, 2> pointsData;
    for (auto &v : pointsData)
        v.reserve(size_t(std::max(mesh.VN(), mesh.FN() * 3)) * kPointsVertexStrideFloats);

    QVector2D minUv(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    QVector2D maxUv(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
    bool hasUv = false;

    auto includeUvBounds = [&](const QVector2D &uv) {
        minUv.setX(std::min(minUv.x(), uv.x()));
        minUv.setY(std::min(minUv.y(), uv.y()));
        maxUv.setX(std::max(maxUv.x(), uv.x()));
        maxUv.setY(std::max(maxUv.y(), uv.y()));
        hasUv = true;
    };

    auto appendFillVertex = [&](std::vector<float> &dst,
                                const QVector2D &uv,
                                const QVector3D &meshColor,
                                float useMeshColorFlag,
                                const QVector3D &texInfo) {
        dst.push_back(uv.x());
        dst.push_back(uv.y());
        dst.push_back(0.0f);
        dst.push_back(0.0f);
        dst.push_back(0.0f);
        dst.push_back(1.0f);
        dst.push_back(meshColor.x());
        dst.push_back(meshColor.y());
        dst.push_back(meshColor.z());
        dst.push_back(useMeshColorFlag);
        dst.push_back(texInfo.x());
        dst.push_back(texInfo.y());
        dst.push_back(texInfo.z());
    };

    auto appendPointVertex = [&](std::vector<float> &dst,
                                 const QVector2D &uv,
                                 const QVector3D &meshColor,
                                 float useMeshColorFlag) {
        dst.push_back(uv.x());
        dst.push_back(uv.y());
        dst.push_back(0.0f);
        dst.push_back(meshColor.x());
        dst.push_back(meshColor.y());
        dst.push_back(meshColor.z());
        dst.push_back(useMeshColorFlag);
        dst.push_back(0.0f);
        dst.push_back(0.0f);
        dst.push_back(1.0f);
        dst.push_back(0.0f);
    };

    for (int fi = 0; fi < mesh.FN(); ++fi) {
        const auto &f = mesh.face[fi];
        if (f.IsD())
            continue;

        QVector2D uv[3];
        bool validFaceUv = true;
        for (int c = 0; c < 3; ++c) {
            if (!uvForCorner(f, c, uv[c])) {
                validFaceUv = false;
                break;
            }
        }
        if (!validFaceUv)
            continue;

        const auto fc = f.cC();
        const QVector3D faceColor(
            float(fc[0]) / 255.0f,
            float(fc[1]) / 255.0f,
            float(fc[2]) / 255.0f);
        const float useFaceColorFlag = hasFaceColors ? 1.0f : 0.0f;

        for (int c = 0; c < 3; ++c) {
            includeUvBounds(uv[c]);
            const int n = (c + 1) % 3;
            wireData.push_back(uv[c].x());
            wireData.push_back(uv[c].y());
            wireData.push_back(0.0f);
            wireData.push_back(uv[n].x());
            wireData.push_back(uv[n].y());
            wireData.push_back(0.0f);

            const auto *vertex = f.cV(c);
            QVector3D vertexColor(1.0f, 1.0f, 1.0f);
            float useVertexColorFlag = 0.0f;
            if (vertex && hasVertexColors) {
                const auto vc = vertex->cC();
                vertexColor = QVector3D(
                    float(vc[0]) / 255.0f,
                    float(vc[1]) / 255.0f,
                    float(vc[2]) / 255.0f);
                useVertexColorFlag = 1.0f;
            }

            appendFillVertex(
                fillData[0], uv[c], QVector3D(1.0f, 1.0f, 1.0f), 0.0f, QVector3D(0.0f, 0.0f, 0.0f));
            appendFillVertex(
                fillData[1], uv[c], vertexColor, useVertexColorFlag, QVector3D(0.0f, 0.0f, 0.0f));
            appendFillVertex(
                fillData[2], uv[c], faceColor, useFaceColorFlag, QVector3D(0.0f, 0.0f, 0.0f));

            appendPointVertex(pointsData[0], uv[c], QVector3D(1.0f, 1.0f, 1.0f), 0.0f);
            appendPointVertex(pointsData[1], uv[c], vertexColor, useVertexColorFlag);
        }
    }

    if (!hasUv)
        return false;

    struct UvEdgeSample {
        QVector2D uvA;
        QVector2D uvB;
    };
    std::unordered_map<std::uint64_t, std::vector<UvEdgeSample>> edgeSamples;
    edgeSamples.reserve(size_t(mesh.FN()) * 3);

    for (int fi = 0; fi < mesh.FN(); ++fi) {
        const auto &f = mesh.face[fi];
        if (f.IsD())
            continue;
        for (int i = 0; i < 3; ++i) {
            const int next = (i + 1) % 3;
            QVector2D uv0;
            QVector2D uv1;
            if (!uvForCorner(f, i, uv0) || !uvForCorner(f, next, uv1))
                continue;

            const auto *v0 = f.cV(i);
            const auto *v1 = f.cV(next);
            if (!v0 || !v1)
                continue;
            int a = vcg::tri::Index(mesh, v0);
            int b = vcg::tri::Index(mesh, v1);
            if (a < 0 || b < 0)
                continue;

            if (a > b) {
                std::swap(a, b);
                std::swap(uv0, uv1);
            }

            const std::uint64_t key =
                (std::uint64_t(std::uint32_t(a)) << 32) | std::uint64_t(std::uint32_t(b));
            edgeSamples[key].push_back(UvEdgeSample { uv0, uv1 });
        }
    }

    const float uvEps = 1e-6f;
    for (const auto &kv : edgeSamples) {
        const auto &samples = kv.second;
        if (samples.empty())
            continue;

        if (samples.size() == 1) {
            const UvEdgeSample &s = samples.front();
            boundaryEdgeData.push_back(s.uvA.x());
            boundaryEdgeData.push_back(s.uvA.y());
            boundaryEdgeData.push_back(0.0f);
            boundaryEdgeData.push_back(s.uvB.x());
            boundaryEdgeData.push_back(s.uvB.y());
            boundaryEdgeData.push_back(0.0f);
            continue;
        }

        const UvEdgeSample &ref = samples.front();
        bool isSeam = false;
        for (size_t si = 1; si < samples.size(); ++si) {
            const UvEdgeSample &s = samples[si];
            if ((s.uvA - ref.uvA).lengthSquared() > uvEps * uvEps
                || (s.uvB - ref.uvB).lengthSquared() > uvEps * uvEps) {
                isSeam = true;
                break;
            }
        }
        if (!isSeam)
            continue;

        textureSeamData.push_back(ref.uvA.x());
        textureSeamData.push_back(ref.uvA.y());
        textureSeamData.push_back(0.0f);
        textureSeamData.push_back(ref.uvB.x());
        textureSeamData.push_back(ref.uvB.y());
        textureSeamData.push_back(0.0f);
    }

    QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
    bool anyUpload = false;
    auto uploadFloats = [&](const std::vector<float> &src,
                            std::unique_ptr<QRhiBuffer> &dst,
                            int &dstCount,
                            int strideFloats) {
        dst.reset();
        dstCount = 0;
        if (src.empty())
            return;
        dst.reset(
            m_rhi->newBuffer(
                QRhiBuffer::Immutable,
                QRhiBuffer::VertexBuffer,
                quint32(src.size() * sizeof(float))));
        if (!dst || !dst->create()) {
            dst.reset();
            return;
        }
        updates->uploadStaticBuffer(dst.get(), src.data());
        dstCount = int(src.size() / strideFloats);
        anyUpload = true;
    };

    uploadFloats(wireData, gpu.wireVbuf, gpu.wireVertexCount, 3);
    uploadFloats(
        boundaryEdgeData,
        gpu.boundaryEdgesVbuf,
        gpu.boundaryEdgesVertexCount,
        3);
    uploadFloats(
        textureSeamData,
        gpu.textureSeamsVbuf,
        gpu.textureSeamsVertexCount,
        3);
    for (int i = 0; i < 3; ++i) {
        uploadFloats(
            fillData[size_t(i)],
            gpu.fillVariants[size_t(i)].vbuf,
            gpu.fillVariants[size_t(i)].vertexCount,
            kFillVertexStrideFloats);
    }
    for (int i = 0; i < 2; ++i) {
        uploadFloats(
            pointsData[size_t(i)],
            gpu.pointsVariants[size_t(i)].vbuf,
            gpu.pointsVariants[size_t(i)].vertexCount,
            kPointsVertexStrideFloats);
    }

    if (anyUpload)
        cb->resourceUpdate(updates);

    gpu.minUv = minUv;
    gpu.maxUv = maxUv;
    gpu.valid = (gpu.wireVertexCount > 0)
        || (gpu.fillVariants[0].vertexCount > 0)
        || (gpu.pointsVariants[0].vertexCount > 0);
    return gpu.valid;
}

void RenderWidget::fitUvViewToCurrentMesh(const QSize &pixelSize)
{
    const int meshIndex = m_doc ? m_doc->currentMeshIndex() : -1;
    if (!m_doc || meshIndex < 0 || meshIndex >= m_doc->meshCount()) {
        m_uvPan = QVector2D(0.5f, 0.5f);
        m_uvZoom = 1.0f;
        m_uvFitRequested = false;
        return;
    }

    const auto &entry = m_doc->mesh(meshIndex);
    const auto it = m_uvMeshGpu.find(entry.meshId);
    if (it == m_uvMeshGpu.end() || !it->second.valid) {
        m_uvPan = QVector2D(0.5f, 0.5f);
        m_uvZoom = 1.0f;
        m_uvFitRequested = false;
        return;
    }

    const QVector2D minUv = it->second.minUv;
    const QVector2D maxUv = it->second.maxUv;
    const QVector2D center = (minUv + maxUv) * 0.5f;
    const float halfW = qMax(1e-6f, (maxUv.x() - minUv.x()) * 0.5f);
    const float halfH = qMax(1e-6f, (maxUv.y() - minUv.y()) * 0.5f);

    const float aspect =
        (pixelSize.height() > 0) ? (float(pixelSize.width()) / float(pixelSize.height())) : 1.0f;
    const float xLim = (aspect >= 1.0f) ? aspect : 1.0f;
    const float yLim = (aspect >= 1.0f) ? 1.0f : (1.0f / qMax(1e-6f, aspect));
    const float padding = 0.92f;
    const float zoomX = (xLim * padding) / halfW;
    const float zoomY = (yLim * padding) / halfH;

    m_uvPan = center;
    m_uvZoom = std::clamp(std::min(zoomX, zoomY), kUvMinZoom, kUvMaxZoom);
    m_uvFitRequested = false;
}

void RenderWidget::renderParametrization(QRhiCommandBuffer *cb)
{
    if (!m_rhi || !m_ubuf || !cb || !renderTarget())
        return;

    syncPerMeshRenderModesWithDocument();
    syncUvCacheWithDocument();
    m_frameTimer.start();

    if (m_bboxMinCornerOverlayLabel)
        m_bboxMinCornerOverlayLabel->hide();
    if (m_bboxMaxCornerOverlayLabel)
        m_bboxMaxCornerOverlayLabel->hide();
    if (m_bboxDimXOverlayLabel)
        m_bboxDimXOverlayLabel->hide();
    if (m_bboxDimYOverlayLabel)
        m_bboxDimYOverlayLabel->hide();
    if (m_bboxDimZOverlayLabel)
        m_bboxDimZOverlayLabel->hide();

    const QSize sz = renderTarget()->pixelSize();
    const int meshIndex = m_doc ? m_doc->currentMeshIndex() : -1;
    const RenderSettings meshSettings =
        (meshIndex >= 0 && meshIndex < m_doc->meshCount())
        ? renderSettingsForMesh(meshIndex)
        : m_renderSettings;
    const bool canDraw =
        (meshIndex >= 0)
        && meshVisible(meshIndex)
        && ensureUvMeshResources(meshIndex, cb);
    if (m_uvFitRequested)
        fitUvViewToCurrentMesh(sz);

    if (!m_uvBackgroundUbuf) {
        m_uvBackgroundUbuf.reset(
            m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUvBackgroundUbufSize));
        if (!m_uvBackgroundUbuf->create())
            m_uvBackgroundUbuf.reset();
    }
    if (!m_uvBackgroundSrb && m_uvBackgroundUbuf) {
        m_uvBackgroundSrb.reset(m_rhi->newShaderResourceBindings());
        m_uvBackgroundSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::FragmentStage, m_uvBackgroundUbuf.get())
        });
        if (!m_uvBackgroundSrb->create())
            m_uvBackgroundSrb.reset();
    }
    if (!m_uvBackgroundPipeline && m_uvBackgroundSrb) {
        m_uvBackgroundPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/uv_background.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/uv_background.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            m_uvBackgroundPipeline.reset();
        } else {
            m_uvBackgroundPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs },
            });
            m_uvBackgroundPipeline->setDepthTest(false);
            m_uvBackgroundPipeline->setDepthWrite(false);
            m_uvBackgroundPipeline->setCullMode(QRhiGraphicsPipeline::None);
            m_uvBackgroundPipeline->setShaderResourceBindings(m_uvBackgroundSrb.get());
            m_uvBackgroundPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
            if (!m_uvBackgroundPipeline->create()) {
                qWarning("Failed to create UV background pipeline");
                m_uvBackgroundPipeline.reset();
            }
        }
    }
    if (!m_uvTextureFillPipeline && m_srb) {
        m_uvTextureFillPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/uv_fill_texture.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/fill_smooth.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            m_uvTextureFillPipeline.reset();
        } else {
            m_uvTextureFillPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs },
            });
            m_uvTextureFillPipeline->setDepthTest(false);
            m_uvTextureFillPipeline->setDepthWrite(false);
            m_uvTextureFillPipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiVertexInputLayout layout;
            layout.setBindings({ { kFillVertexStrideFloats * sizeof(float) } });
            layout.setAttributes({
                { 0, 0, QRhiVertexInputAttribute::Float3, 0 },
                { 0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float) },
                { 0, 2, QRhiVertexInputAttribute::Float4, 6 * sizeof(float) },
                { 0, 3, QRhiVertexInputAttribute::Float3, 10 * sizeof(float) },
            });
            m_uvTextureFillPipeline->setVertexInputLayout(layout);
            m_uvTextureFillPipeline->setShaderResourceBindings(m_srb.get());
            m_uvTextureFillPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
            if (!m_uvTextureFillPipeline->create()) {
                qWarning("Failed to create UV textured fill pipeline");
                m_uvTextureFillPipeline.reset();
            }
        }
    }
    if (!m_uvUnitBoxVbuf) {
        const std::array<float, 24> unitBox = {
            0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f
        };
        m_uvUnitBoxVbuf.reset(
            m_rhi->newBuffer(
                QRhiBuffer::Immutable,
                QRhiBuffer::VertexBuffer,
                quint32(unitBox.size() * sizeof(float))));
        if (m_uvUnitBoxVbuf && m_uvUnitBoxVbuf->create()) {
            QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
            u->uploadStaticBuffer(m_uvUnitBoxVbuf.get(), unitBox.data());
            cb->resourceUpdate(u);
            m_uvUnitBoxVertexCount = 8;
        } else {
            m_uvUnitBoxVbuf.reset();
            m_uvUnitBoxVertexCount = 0;
        }
    }

    const float aspect = (sz.height() > 0) ? (float(sz.width()) / float(sz.height())) : 1.0f;
    const float xLim = (aspect >= 1.0f) ? aspect : 1.0f;
    const float yLim = (aspect >= 1.0f) ? 1.0f : (1.0f / qMax(1e-6f, aspect));

    QMatrix4x4 proj;
    proj.ortho(-xLim, xLim, -yLim, yLim, -1.0f, 1.0f);
    QMatrix4x4 model;
    model.scale(m_uvZoom, m_uvZoom, 1.0f);
    model.translate(-m_uvPan.x(), -m_uvPan.y(), 0.0f);
    const QMatrix4x4 mvp = proj * model;

    QMatrix4x4 modelView;
    modelView.setToIdentity();
    QMatrix3x3 normalMat;
    normalMat.fill(0.0f);
    normalMat(0, 0) = 1.0f;
    normalMat(1, 1) = 1.0f;
    normalMat(2, 2) = 1.0f;

    if (canDraw && meshSettings.showFill && meshSettings.fillColorSource == FillColorSource::Texture) {
        m_doc->ensureMeshGpuResources(
            m_rhi,
            cb,
            meshIndex,
            Document::FillGpuVariant::Texture,
            Document::PointGpuVariant::Constant,
            true,   // fill
            false,  // wire
            false,  // edges
            false,  // points
            false,  // bbox
            false,  // decorator normals
            false); // decorator boundaries
    }

    float baseUbufData[kUbufFloatCount] = {};
    memcpy(baseUbufData, mvp.constData(), 64);
    memcpy(baseUbufData + 16, modelView.constData(), 64);
    const float *n = normalMat.constData();
    baseUbufData[32] = n[0]; baseUbufData[33] = n[1]; baseUbufData[34] = n[2]; baseUbufData[35] = 0.0f;
    baseUbufData[36] = n[3]; baseUbufData[37] = n[4]; baseUbufData[38] = n[5]; baseUbufData[39] = 0.0f;
    baseUbufData[40] = n[6]; baseUbufData[41] = n[7]; baseUbufData[42] = n[8]; baseUbufData[43] = 0.0f;

    auto updateStyleUbuf = [&](const RenderSettings &styleSettings) {
        float ubufData[kUbufFloatCount];
        memcpy(ubufData, baseUbufData, sizeof(ubufData));
        ubufData[kUbufBBoxColorOffset + 0] = styleSettings.bboxWireColor.redF();
        ubufData[kUbufBBoxColorOffset + 1] = styleSettings.bboxWireColor.greenF();
        ubufData[kUbufBBoxColorOffset + 2] = styleSettings.bboxWireColor.blueF();
        ubufData[kUbufBBoxColorOffset + 3] = styleSettings.bboxWireColor.alphaF();
        ubufData[kUbufPointColorOffset + 0] = styleSettings.pointColor.redF();
        ubufData[kUbufPointColorOffset + 1] = styleSettings.pointColor.greenF();
        ubufData[kUbufPointColorOffset + 2] = styleSettings.pointColor.blueF();
        ubufData[kUbufPointColorOffset + 3] = styleSettings.pointColor.alphaF();
        ubufData[kUbufPointParamsOffset + 0] = styleSettings.pointSize;
        ubufData[kUbufWireColorOffset + 0] = styleSettings.wireColor.redF();
        ubufData[kUbufWireColorOffset + 1] = styleSettings.wireColor.greenF();
        ubufData[kUbufWireColorOffset + 2] = styleSettings.wireColor.blueF();
        ubufData[kUbufWireColorOffset + 3] = styleSettings.wireColor.alphaF() * 0.7f;
        ubufData[kUbufWireParamsOffset + 0] = styleSettings.wireSize;
        ubufData[kUbufFillColorOffset + 0] = styleSettings.fillColor.redF();
        ubufData[kUbufFillColorOffset + 1] = styleSettings.fillColor.greenF();
        ubufData[kUbufFillColorOffset + 2] = styleSettings.fillColor.blueF();
        ubufData[kUbufFillColorOffset + 3] = styleSettings.fillColor.alphaF();
        ubufData[kUbufLightingParamsOffset + 0] = 0.0f;
        ubufData[kUbufLightingParamsOffset + 1] = 0.0f;
        ubufData[kUbufLightingParamsOffset + 2] = 0.0f;
        ubufData[kUbufLightingParamsOffset + 3] = 0.0f;
        ubufData[kUbufEdgeColorOffset + 0] = styleSettings.edgeColor.redF();
        ubufData[kUbufEdgeColorOffset + 1] = styleSettings.edgeColor.greenF();
        ubufData[kUbufEdgeColorOffset + 2] = styleSettings.edgeColor.blueF();
        ubufData[kUbufEdgeColorOffset + 3] = styleSettings.edgeColor.alphaF();

        QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
        u->updateDynamicBuffer(m_ubuf.get(), 0, kUbufSize, ubufData);
        cb->resourceUpdate(u);
    };

    if (m_uvBackgroundUbuf) {
        float bgData[kUvBackgroundUbufSize / sizeof(float)] = {};
        bgData[0] = m_uvPan.x();
        bgData[1] = m_uvPan.y();
        bgData[2] = qMax(1e-6f, m_uvZoom);
        bgData[3] = qMax(1e-6f, aspect);
        bgData[4] = 0.30f; bgData[5] = 0.30f; bgData[6] = 0.32f; bgData[7] = 1.0f;
        bgData[8] = 0.36f; bgData[9] = 0.36f; bgData[10] = 0.38f; bgData[11] = 1.0f;
        bgData[12] = 12.0f; bgData[13] = 6.0f; bgData[14] = 0.0f; bgData[15] = 0.0f;
        QRhiResourceUpdateBatch *uBg = m_rhi->nextResourceUpdateBatch();
        uBg->updateDynamicBuffer(m_uvBackgroundUbuf.get(), 0, kUvBackgroundUbufSize, bgData);
        cb->resourceUpdate(uBg);
    }

    cb->beginPass(renderTarget(), QColor(40, 40, 40), { 1.0f, 0 }, nullptr);
    cb->setViewport({ 0, 0, float(sz.width()), float(sz.height()) });

    if (m_uvBackgroundPipeline && m_uvBackgroundSrb) {
        cb->setGraphicsPipeline(m_uvBackgroundPipeline.get());
        cb->setShaderResources(m_uvBackgroundSrb.get());
        cb->draw(3);
    }

    if (canDraw) {
        const auto &entry = m_doc->mesh(meshIndex);
        auto cacheIt = m_uvMeshGpu.find(entry.meshId);
        if (cacheIt != m_uvMeshGpu.end() && cacheIt->second.valid) {
            UvMeshGpu &uvGpu = cacheIt->second;
            const MeshRenderMode meshMode = renderModeForMesh(meshIndex);

            if (meshSettings.showFill) {
                if (meshSettings.fillColorSource == FillColorSource::Texture
                    && m_uvTextureFillPipeline) {
                    updateStyleUbuf(meshSettings);
                    const Document::FillPassGpuView fillView =
                        m_doc->fillPassGpuView(m_rhi, meshIndex, Document::FillGpuVariant::Texture);
                    if (fillView.valid) {
                        cb->setGraphicsPipeline(m_uvTextureFillPipeline.get());
                        for (int bi = 0; bi < fillView.batchCount; ++bi) {
                            const auto &batch = fillView.batches[bi];
                            if (!batch.vertexBuffer || (batch.indexCount == 0 && batch.vertexCount == 0))
                                continue;
                            cb->setShaderResources(shaderResourcesForTexture(batch.texture));
                            const QRhiCommandBuffer::VertexInput binding(batch.vertexBuffer, 0);
                            if (batch.indexCount > 0 && batch.indexBuffer) {
                                cb->setVertexInput(
                                    0, 1, &binding, batch.indexBuffer, 0, QRhiCommandBuffer::IndexUInt32);
                                cb->drawIndexed(batch.indexCount);
                            } else {
                                cb->setVertexInput(0, 1, &binding);
                                cb->draw(batch.vertexCount);
                            }
                        }
                    }
                } else {
                    RenderSettings fillSettings = meshSettings;
                    fillSettings.fillLighting = false;
                    fillSettings.fillBackfaceCulling = false;
                    QRhiGraphicsPipeline *fillPipeline = fillPipelineForSettings(fillSettings);
                    int fillVariantIdx = 0;
                    if (meshSettings.fillColorSource == FillColorSource::PerVertex)
                        fillVariantIdx = 1;
                    else if (meshSettings.fillColorSource == FillColorSource::PerFace)
                        fillVariantIdx = 2;
                    const auto &fillVariant = uvGpu.fillVariants[size_t(fillVariantIdx)];
                    if (fillPipeline && fillVariant.vbuf && fillVariant.vertexCount > 0) {
                        updateStyleUbuf(fillSettings);
                        cb->setGraphicsPipeline(fillPipeline);
                        cb->setShaderResources(shaderResourcesForTexture(nullptr));
                        const QRhiCommandBuffer::VertexInput binding(fillVariant.vbuf.get(), 0);
                        cb->setVertexInput(0, 1, &binding);
                        cb->draw(fillVariant.vertexCount);
                    }
                }
            }

            auto drawLineSet = [&](const QColor &color, float width, QRhiBuffer *vbuf, int vertexCount) {
                if (!vbuf || vertexCount <= 0)
                    return;
                RenderSettings edgeSettings = meshSettings;
                edgeSettings.edgeColor = color;
                edgeSettings.edgeSize = width;
                QRhiGraphicsPipeline *pipeline = edgesPipelineForSettings(edgeSettings);
                if (!pipeline)
                    return;
                updateStyleUbuf(edgeSettings);
                cb->setGraphicsPipeline(pipeline);
                cb->setShaderResources(m_srb.get());
                const QRhiCommandBuffer::VertexInput binding(vbuf, 0);
                cb->setVertexInput(0, 1, &binding);
                cb->draw(vertexCount);
            };

            if (meshSettings.showWire)
                drawLineSet(
                    meshSettings.wireColor, meshSettings.wireSize, uvGpu.wireVbuf.get(), uvGpu.wireVertexCount);
            if (meshSettings.showEdges)
                drawLineSet(
                    meshSettings.edgeColor, meshSettings.edgeSize, uvGpu.wireVbuf.get(), uvGpu.wireVertexCount);
            if (meshMode.decoratorBoundaryEdges)
                drawLineSet(
                    meshMode.decoratorBoundaryEdgeColor,
                    qMax(0.5f, meshSettings.decoratorBoundaryWidth),
                    uvGpu.boundaryEdgesVbuf.get(),
                    uvGpu.boundaryEdgesVertexCount);
            if (meshMode.decoratorTextureSeams)
                drawLineSet(
                    meshMode.decoratorTextureSeamColor,
                    qMax(0.5f, meshSettings.decoratorBoundaryWidth),
                    uvGpu.textureSeamsVbuf.get(),
                    uvGpu.textureSeamsVertexCount);

            if (meshSettings.showPoints && m_pointsPipeline) {
                int pointVariantIdx = (meshSettings.pointColorSource == PointColorSource::PerVertex) ? 1 : 0;
                const auto &pointVariant = uvGpu.pointsVariants[size_t(pointVariantIdx)];
                if (pointVariant.vbuf && pointVariant.vertexCount > 0) {
                    RenderSettings pointSettings = meshSettings;
                    pointSettings.pointLighting = false;
                    updateStyleUbuf(pointSettings);
                    cb->setGraphicsPipeline(m_pointsPipeline.get());
                    cb->setShaderResources(m_srb.get());
                    const QRhiCommandBuffer::VertexInput binding(pointVariant.vbuf.get(), 0);
                    cb->setVertexInput(0, 1, &binding);
                    cb->draw(pointVariant.vertexCount);
                }
            }
        }
    }

    if (meshSettings.showBoundingBox && m_uvUnitBoxVbuf && m_uvUnitBoxVertexCount > 0) {
        RenderSettings boxSettings = meshSettings;
        boxSettings.edgeColor = meshSettings.bboxWireColor;
        boxSettings.edgeSize = qMax(1.0f, meshSettings.edgeSize);
        QRhiGraphicsPipeline *pipeline = edgesPipelineForSettings(boxSettings);
        if (pipeline) {
            updateStyleUbuf(boxSettings);
            cb->setGraphicsPipeline(pipeline);
            cb->setShaderResources(m_srb.get());
            const QRhiCommandBuffer::VertexInput binding(m_uvUnitBoxVbuf.get(), 0);
            cb->setVertexInput(0, 1, &binding);
            cb->draw(m_uvUnitBoxVertexCount);
        }
    }

    cb->endPass();

    const float cpuMs = m_frameTimer.nsecsElapsed() / 1e6f;
    const bool gpuTimingSupported = m_rhi->isFeatureSupported(QRhi::Timestamps);
    float gpuMs = 0.0f;
    bool gpuSampleValid = false;
    if (gpuTimingSupported) {
        const double gpuSeconds = cb->lastCompletedGpuTime();
        if (gpuSeconds > 0.0) {
            gpuMs = static_cast<float>(gpuSeconds * 1000.0);
            gpuSampleValid = true;
        }
    }
    emit frameRendered(cpuMs, gpuMs, gpuTimingSupported, gpuSampleValid);
}
