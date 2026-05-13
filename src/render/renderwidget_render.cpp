#include "renderwidget.h"
#include "colormap.h"
#include "document.h"
#include "renderwidget_internal.h"
#include <QLabel>
#include <algorithm>
#include <cmath>

using namespace RenderWidgetInternal;

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
    if (!m_rhi || !m_ubuf || !m_srb)
        return;

    if (m_viewMode == ViewMode::ParametrizationUV) {
        renderParametrization(cb);
        return;
    }
    for (QLabel *label : m_uvScaleXTickLabels) {
        if (label)
            label->hide();
    }
    for (QLabel *label : m_uvScaleYTickLabels) {
        if (label)
            label->hide();
    }

    advanceCenterAnimation();
    emitCameraStateChangedIfNeeded();
    syncPerMeshRenderModesWithDocument();

    const bool drawTrackballGizmo =
        m_renderSettings.showTrackballGizmo && (m_doc->meshCount() > 0);
    const int currentMeshIndex = m_doc->currentMeshIndex();
    const bool drawCurrentMeshHighlight =
        m_renderSettings.highlightCurrentMesh
        && (currentMeshIndex >= 0)
        && meshVisible(currentMeshIndex);
    bool drawFillPass = false;
    bool drawWirePass = false;
    bool drawEdgesPass = false;
    bool drawBBoxPass = false;
    bool drawPointsPass = false;
    bool drawSelectionPass = false;
    bool drawDecoratorPass = false;
    for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
        if (!meshVisible(mi))
            continue;
        const MeshRenderMode mode = renderModeForMesh(mi);
        drawFillPass = drawFillPass || mode.showFill;
        drawWirePass = drawWirePass || mode.showWire;
        drawEdgesPass = drawEdgesPass || mode.showEdges;
        drawBBoxPass = drawBBoxPass || mode.showBoundingBox;
        drawPointsPass = drawPointsPass || mode.showPoints;
        drawSelectionPass =
            drawSelectionPass
            || (mode.showSelection && (mode.showSelectionVertices || mode.showSelectionFaces));
        drawDecoratorPass =
            drawDecoratorPass
            || mode.decoratorVertexNormals
            || mode.decoratorFaceNormals
            || mode.decoratorCurvatureDir
            || mode.decoratorBoundaryEdges
            || mode.decoratorTextureSeams
            || mode.decoratorNonManifoldEdges
            || mode.decoratorNonManifoldVertices;
    }
    const bool anyDrawPass =
        drawFillPass || drawWirePass || drawEdgesPass || drawBBoxPass || drawPointsPass
        || drawSelectionPass || drawDecoratorPass
        || drawCurrentMeshHighlight || drawTrackballGizmo;
    const bool needMvpForFrame = anyDrawPass || m_depthPickPending;

    if (anyDrawPass)
        prepareDirtyBuffers(cb);

    m_frameTimer.start();

    const QSize sz = renderTarget()->pixelSize();

    QRhiResourceUpdateBatch *u = nullptr;
    auto updateQualityColorMapLut = [&](QRhiResourceUpdateBatch *&batch) {
        if (!m_qualityColorMapTexture || !m_rhi)
            return;
        const ColorMapRegistry &registry = ColorMapRegistry::instance();
        QString mapId = m_renderSettings.qualityHistogramColorMapId.trimmed().toLower();
        const bool isConstant = (mapId == QStringLiteral("constant"));
        if (!isConstant && (mapId.isEmpty() || !registry.hasMap(mapId)))
            mapId = registry.fallbackMapId();
        const bool invert = m_renderSettings.qualityHistogramInvertColorMap;
        const bool isolinesEnabled = m_renderSettings.qualityIsolinesEnabled;
        const int isolineCount = m_renderSettings.qualityIsolineCount;
        if (m_qualityColorMapTextureMapId != mapId || m_qualityColorMapTextureInverted != invert
            || m_qualityColorMapTextureIsolinesEnabled != isolinesEnabled
            || m_qualityColorMapTextureIsolineCount != isolineCount) {
            m_qualityColorMapTextureMapId = mapId;
            m_qualityColorMapTextureInverted = invert;
            m_qualityColorMapTextureIsolinesEnabled = isolinesEnabled;
            m_qualityColorMapTextureIsolineCount = isolineCount;
            m_qualityColorMapTextureUploadPending = true;
        }
        if (!m_qualityColorMapTextureUploadPending)
            return;

        constexpr int kLutSize = 1024;
        QImage lut(kLutSize, 1, QImage::Format_RGBA8888);
        uchar *bits = lut.bits();
        for (int i = 0; i < kLutSize; ++i) {
            float t = float(i) / float(kLutSize - 1);
            if (invert)
                t = 1.0f - t;
            QColor c;
            if (isConstant) {
                c = QColor(255, 255, 255);
            } else {
                c = registry.sampleQColor(mapId, t, 1.0f);
            }
            bits[i * 4 + 0] = uchar(c.red());
            bits[i * 4 + 1] = uchar(c.green());
            bits[i * 4 + 2] = uchar(c.blue());
            bits[i * 4 + 3] = 255u;
        }
        // Apply isolines: pairs of black pixels at regular intervals
        if (isolinesEnabled && isolineCount > 0) {
            for (int k = 1; k <= isolineCount; ++k) {
                const int center = int(std::round(float(k) / float(isolineCount + 1) * float(kLutSize - 1)));
                for (int offset = -1; offset <= 0; ++offset) {
                    const int idx = std::clamp(center + offset, 0, kLutSize - 1);
                    bits[idx * 4 + 0] = 0;
                    bits[idx * 4 + 1] = 0;
                    bits[idx * 4 + 2] = 0;
                    bits[idx * 4 + 3] = 255u;
                }
            }
        }
        if (!batch)
            batch = m_rhi->nextResourceUpdateBatch();
        QRhiTextureUploadEntry entry(0, 0, QRhiTextureSubresourceUploadDescription(lut));
        batch->uploadTexture(m_qualityColorMapTexture.get(), QRhiTextureUploadDescription({ entry }));
        m_qualityColorMapTextureUploadPending = false;
    };
    updateQualityColorMapLut(u);

    QMatrix4x4 proj;
    QMatrix4x4 view;
    QMatrix4x4 vp;
    bool haveCameraMatrices = false;
    if (needMvpForFrame) {
        const float aspect = sz.width() / float(sz.height());
        proj = m_trackball.projectionMatrix(aspect);

        view = m_trackball.viewMatrix();
        vp = proj * view;
        haveCameraMatrices = true;

        if (drawTrackballGizmo && m_trackballGizmoUbuf && m_trackballGizmoVbuf) {
            if (!u)
                u = m_rhi->nextResourceUpdateBatch();
            const auto &verts = trackballGizmoVertices();
            u->updateDynamicBuffer(
                m_trackballGizmoVbuf.get(),
                0,
                int(verts.size() * sizeof(float)),
                verts.data());

            float gizmoData[kTrackballGizmoUbufSize / sizeof(float)] = {};
            memcpy(gizmoData, vp.constData(), 64);
            const QVector3D center = m_trackball.center();
            gizmoData[16] = center.x();
            gizmoData[17] = center.y();
            gizmoData[18] = center.z();
            gizmoData[19] = m_trackball.gizmoWorldRadius();
            const QMatrix4x4 invView = view.inverted();
            QVector4D cameraH = invView * QVector4D(0.0f, 0.0f, 0.0f, 1.0f);
            if (std::abs(cameraH.w()) > 1e-8f)
                cameraH /= cameraH.w();
            gizmoData[20] = cameraH.x();
            gizmoData[21] = cameraH.y();
            gizmoData[22] = cameraH.z();
            gizmoData[23] = 0.38f; // back hemisphere shading floor
            u->updateDynamicBuffer(
                m_trackballGizmoUbuf.get(),
                0,
                kTrackballGizmoUbufSize,
                gizmoData);
        }
    }

    auto updateMainUbufForMesh =
        [&](int meshIndex,
            const PerMeshRenderSettings &meshSettings,
            float normalScale = 1.0f,
            float occlusionStrength = 1.0f,
            float roughnessFactor = 1.0f) {
        if (!haveCameraMatrices || !m_ubuf)
            return;
        if (meshIndex < 0 || meshIndex >= m_doc->meshCount())
            return;

        const QMatrix4x4 model = m_doc->mesh(meshIndex).transform;
        const QMatrix4x4 modelView = view * model;
        const QMatrix4x4 mvp = proj * modelView;
        const QMatrix3x3 normalMat = modelView.normalMatrix();

        float ubufData[kUbufFloatCount] = {};
        memcpy(ubufData, mvp.constData(), 64);
        memcpy(ubufData + 16, modelView.constData(), 64);
        const float *n = normalMat.constData();
        ubufData[32] = n[0]; ubufData[33] = n[1]; ubufData[34] = n[2]; ubufData[35] = 0;
        ubufData[36] = n[3]; ubufData[37] = n[4]; ubufData[38] = n[5]; ubufData[39] = 0;
        ubufData[40] = n[6]; ubufData[41] = n[7]; ubufData[42] = n[8]; ubufData[43] = 0;
        writeMainStyleToUbuf(ubufData, meshSettings, sz, true);
        ubufData[kUbufMaterialParamsOffset + 0] = normalScale;
        ubufData[kUbufMaterialParamsOffset + 1] = occlusionStrength;
        ubufData[kUbufMaterialParamsOffset + 2] = roughnessFactor;

        QRhiResourceUpdateBatch *uMesh = m_rhi->nextResourceUpdateBatch();
        uMesh->updateDynamicBuffer(m_ubuf.get(), 0, kUbufSize, ubufData);
        cb->resourceUpdate(uMesh);
    };

    if (m_depthPickPending) {
        if (u) {
            cb->resourceUpdate(u);
            u = nullptr;
        }
        executePendingDepthPick(cb, sz);
    }

    if (drawCurrentMeshHighlight) {
        if (u) {
            cb->resourceUpdate(u);
            u = nullptr;
        }
        renderCurrentMeshMask(cb, sz);
        processCurrentMeshMask(cb, sz);
    }

    if (m_sceneBackgroundUbuf) {
        float bgData[8] = {};
        bgData[0] = m_renderSettings.sceneBackgroundBottomColor.redF();
        bgData[1] = m_renderSettings.sceneBackgroundBottomColor.greenF();
        bgData[2] = m_renderSettings.sceneBackgroundBottomColor.blueF();
        bgData[3] = 1.0f;
        bgData[4] = m_renderSettings.sceneBackgroundTopColor.redF();
        bgData[5] = m_renderSettings.sceneBackgroundTopColor.greenF();
        bgData[6] = m_renderSettings.sceneBackgroundTopColor.blueF();
        bgData[7] = 1.0f;
        if (!u)
            u = m_rhi->nextResourceUpdateBatch();
        u->updateDynamicBuffer(m_sceneBackgroundUbuf.get(), 0, sizeof(bgData), bgData);
    }

    // ── RS gradient pre-pass (pass 1) ─────────────────────────────────────────
    // For any mesh rendered with Radiance Scaling, we first render the whole
    // scene into a floating-point gradient texture (gx, gy, logZ, 1.0).
    // Pass 2 (the normal fill pass) then samples the 3×3 neighbourhood from
    // that texture — exactly as in the original two-pass MeshLab RS plugin.
    bool anyRsMesh = false;
    if (drawFillPass) {
        for (int mi = 0; mi < m_doc->meshCount() && !anyRsMesh; ++mi) {
            if (!meshVisible(mi))
                continue;
            const PerMeshRenderSettings ms = renderModeForMesh(mi);
            if (ms.showFill && ms.fillMaterial == FillMaterial::RadianceScaling)
                anyRsMesh = true;
        }
    }
    if (anyRsMesh) {
        ensureRsGradResources(sz);
        if (m_rsGradRt && m_rsGradPipeline && m_rsGradSrb) {
            cb->beginPass(m_rsGradRt.get(), QColor(0, 0, 0, 0), { 1.0f, 0 }, nullptr);
            cb->setViewport({ 0, 0, float(sz.width()), float(sz.height()) });
            cb->setGraphicsPipeline(m_rsGradPipeline.get());
            for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
                if (!meshVisible(mi))
                    continue;
                const PerMeshRenderSettings meshSettings = renderModeForMesh(mi);
                if (!meshSettings.showFill
                    || meshSettings.fillMaterial != FillMaterial::RadianceScaling)
                    continue;
                const auto fillVariant = static_cast<Document::FillGpuVariant>(
                    fillGpuVariantIndexForSettings(meshSettings));
                const Document::FillPassGpuView fillView =
                    m_doc->fillPassGpuView(m_rhi, mi, fillVariant);
                if (!fillView.valid)
                    continue;
                for (int bi = 0; bi < fillView.batchCount; ++bi) {
                    const auto &batch = fillView.batches[bi];
                    if (!batch.vertexBuffer
                        || (batch.indexCount == 0 && batch.vertexCount == 0))
                        continue;
                    updateMainUbufForMesh(mi, meshSettings,
                                         meshSettings.fillRs.enhancement, 1.0f, 1.0f);
                    cb->setShaderResources(m_rsGradSrb.get());
                    const QRhiCommandBuffer::VertexInput vb(batch.vertexBuffer, 0);
                    if (batch.indexCount > 0 && batch.indexBuffer) {
                        cb->setVertexInput(0, 1, &vb,
                                           batch.indexBuffer, 0,
                                           QRhiCommandBuffer::IndexUInt32);
                        cb->drawIndexed(batch.indexCount);
                    } else {
                        cb->setVertexInput(0, 1, &vb);
                        cb->draw(batch.vertexCount);
                    }
                }
            }
            cb->endPass();
        }
    }

    cb->beginPass(renderTarget(), m_renderSettings.sceneBackgroundBottomColor, { 1.0f, 0 }, u);    cb->setViewport({ 0, 0, float(sz.width()), float(sz.height()) });

    if (m_sceneBackgroundPipeline && m_sceneBackgroundSrb) {
        cb->setGraphicsPipeline(m_sceneBackgroundPipeline.get());
        cb->setShaderResources(m_sceneBackgroundSrb.get());
        cb->draw(3);
    }

    if (drawFillPass) {
        for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
            if (!meshVisible(mi))
                continue;
            const PerMeshRenderSettings meshSettings = renderModeForMesh(mi);
            if (!meshSettings.showFill)
                continue;
            QRhiGraphicsPipeline *fillPipeline = fillPipelineForSettings(meshSettings);
            if (!fillPipeline)
                continue;
            cb->setGraphicsPipeline(fillPipeline);
            const auto fillVariant = static_cast<Document::FillGpuVariant>(
                fillGpuVariantIndexForSettings(meshSettings));
            const Document::FillPassGpuView fillView =
                m_doc->fillPassGpuView(m_rhi, mi, fillVariant);
            if (!fillView.valid)
                continue;

            // ── Per-material draw helpers (Suggestion B) ─────────────────────
            // Each lambda encapsulates the UBO update, SRB selection, and draw
            // call for one material type.  The batch loop below dispatches to
            // the right helper via a switch, keeping material-specific logic
            // isolated and easy to extend.

            auto submitBatch = [&](const auto &batch) {
                const QRhiCommandBuffer::VertexInput vb(batch.vertexBuffer, 0);
                if (batch.indexCount > 0 && batch.indexBuffer) {
                    cb->setVertexInput(
                        0, 1, &vb, batch.indexBuffer, 0, QRhiCommandBuffer::IndexUInt32);
                    cb->drawIndexed(batch.indexCount);
                } else {
                    cb->setVertexInput(0, 1, &vb);
                    cb->draw(batch.vertexCount);
                }
            };

            // Plain material: constant/vertex/face/quality colour or a single
            // albedo texture.  No normal-map, occlusion or roughness inputs.
            auto drawFillBatchPlain = [&](const auto &batch) {
                updateMainUbufForMesh(mi, meshSettings,
                                      meshSettings.fillPbr.normalScale * batch.normalScale,
                                      1.0f, 1.0f);
                // When Texture source is selected, resolve the user-chosen texture
                // by index; fall back to the batch's baked base-colour texture if
                // the index is unspecified or the texture can't be found.
                QRhiTexture *albedo = batch.baseColorTexture;
                if (meshSettings.fillPlain.colorSource == FillColorSource::Texture) {
                    if (QRhiTexture *t = resolveSelectedPbrTexture(mi, meshSettings.fillPlain.textureIndex, fillView))
                        albedo = t;
                }
                cb->setShaderResources(shaderResourcesForFillTextures(albedo, nullptr, nullptr, nullptr));
                submitBatch(batch);
            };

            // PBR material: resolves all four texture channels from the per-
            // mesh PbrFillParams and falls back to the batch's baked textures.
            auto drawFillBatchPbr = [&](const auto &batch) {
                const auto &pbr = meshSettings.fillPbr;
                QRhiTexture *albedo =
                    (pbr.albedoSource == FillPbrTextureSource::Texture)
                    ? resolveSelectedPbrTexture(mi, pbr.albedoIndex, fillView) : nullptr;
                QRhiTexture *normal =
                    (pbr.normalSource == FillPbrTextureSource::Texture)
                    ? resolveSelectedPbrTexture(mi, pbr.normalIndex, fillView) : nullptr;
                QRhiTexture *occlusion =
                    (pbr.occlusionSource == FillPbrTextureSource::Texture)
                    ? resolveSelectedPbrTexture(mi, pbr.occlusionIndex, fillView) : nullptr;
                QRhiTexture *roughness =
                    (pbr.roughnessSource == FillPbrTextureSource::Texture)
                    ? resolveSelectedPbrTexture(mi, pbr.roughnessIndex, fillView) : nullptr;
                updateMainUbufForMesh(mi, meshSettings,
                                      pbr.normalScale     * batch.normalScale,
                                      pbr.occlusionStrength * batch.occlusionStrength,
                                      pbr.roughnessFactor * batch.roughnessFactor);
                cb->setShaderResources(shaderResourcesForFillTextures(
                    albedo    ? albedo    : batch.baseColorTexture,
                    normal,
                    occlusion ? occlusion : batch.occlusionTexture,
                    roughness ? roughness : batch.roughnessTexture));
                submitBatch(batch);
            };

            // Radiance Scaling: pass 1 gradient is already in m_rsGradTexture;
            // bind it at the normal-map slot (slot 3) for the RS fragment shader.
            auto drawFillBatchRs = [&](const auto &batch) {
                updateMainUbufForMesh(mi, meshSettings,
                                      meshSettings.fillRs.enhancement, 1.0f, 1.0f);
                QRhiTexture *gradTex = m_rsGradTexture ? m_rsGradTexture.get() : nullptr;
                cb->setShaderResources(shaderResourcesForFillTextures(
                    batch.baseColorTexture, gradTex, nullptr, nullptr));
                submitBatch(batch);
            };

            for (int bi = 0; bi < fillView.batchCount; ++bi) {
                const auto &batch = fillView.batches[bi];
                if (!batch.vertexBuffer || (batch.indexCount == 0 && batch.vertexCount == 0))
                    continue;
                switch (meshSettings.fillMaterial) {
                case FillMaterial::Plain:            drawFillBatchPlain(batch); break;
                case FillMaterial::Pbr:              drawFillBatchPbr(batch);   break;
                case FillMaterial::RadianceScaling:  drawFillBatchRs(batch);    break;
                }
            }
        }
    }

    if (drawWirePass) {
        cb->setViewport({ 0, 0, float(sz.width()), float(sz.height()) });

        for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
            if (!meshVisible(mi))
                continue;
            const PerMeshRenderSettings meshSettings = renderModeForMesh(mi);
            if (!meshSettings.showWire)
                continue;
            QRhiGraphicsPipeline *wirePipeline = wirePipelineForSettings(meshSettings);
            if (!wirePipeline)
                continue;
            cb->setGraphicsPipeline(wirePipeline);
            cb->setShaderResources(m_srb.get());
            updateMainUbufForMesh(mi, meshSettings);
            const Document::WirePassGpuView wireView = m_doc->wirePassGpuView(m_rhi, mi);
            if (!wireView.valid || !wireView.vertexBuffer || wireView.vertexCount <= 0)
                continue;
            const QRhiCommandBuffer::VertexInput vbufBinding(wireView.vertexBuffer, 0);
            cb->setVertexInput(0, 1, &vbufBinding);
            cb->draw(wireView.vertexCount);
        }
    }

    if (drawEdgesPass) {
        cb->setViewport({ 0, 0, float(sz.width()), float(sz.height()) });
        for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
            if (!meshVisible(mi))
                continue;
            const PerMeshRenderSettings meshSettings = renderModeForMesh(mi);
            if (!meshSettings.showEdges)
                continue;

            bool drawn = false;
            {
                QRhiGraphicsPipeline *fatPipeline = fatEdgesPipelineForSettings(meshSettings);
                const Document::EdgeFatPassGpuView fatView = m_doc->edgeFatPassGpuView(m_rhi, mi);
                if (fatPipeline && fatView.valid && fatView.vertexBuffer && fatView.vertexCount > 0) {
                    cb->setGraphicsPipeline(fatPipeline);
                    cb->setShaderResources(m_srb.get());
                    updateMainUbufForMesh(mi, meshSettings);
                    const QRhiCommandBuffer::VertexInput ev(fatView.vertexBuffer, 0);
                    cb->setVertexInput(0, 1, &ev);
                    cb->draw(fatView.vertexCount);
                    drawn = true;
                }
            }

            if (!drawn) {
                QRhiGraphicsPipeline *linePipeline = edgesPipelineForSettings(meshSettings);
                if (!linePipeline)
                    continue;
                cb->setGraphicsPipeline(linePipeline);
                cb->setShaderResources(m_srb.get());
                updateMainUbufForMesh(mi, meshSettings);
                const Document::EdgePassGpuView lineView = m_doc->edgePassGpuView(m_rhi, mi);
                if (!lineView.valid || !lineView.vertexBuffer || lineView.vertexCount <= 0)
                    continue;
                const QRhiCommandBuffer::VertexInput ev(lineView.vertexBuffer, 0);
                cb->setVertexInput(0, 1, &ev);
                cb->draw(lineView.vertexCount);
            }
        }
    }

    if (drawBBoxPass && m_bboxPipeline) {
        cb->setGraphicsPipeline(m_bboxPipeline.get());
        cb->setShaderResources(m_srb.get());
        for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
            if (!meshVisible(mi))
                continue;
            const PerMeshRenderSettings meshSettings = renderModeForMesh(mi);
            if (!meshSettings.showBoundingBox)
                continue;
            updateMainUbufForMesh(mi, meshSettings);
            const Document::BBoxPassGpuView bboxView = m_doc->bboxPassGpuView(m_rhi, mi);
            if (!bboxView.valid || !bboxView.vertexBuffer || bboxView.vertexCount <= 0)
                continue;
            const QRhiCommandBuffer::VertexInput bv(bboxView.vertexBuffer, 0);
            cb->setVertexInput(0, 1, &bv);
            cb->draw(bboxView.vertexCount);
        }
    }

    if (drawPointsPass && m_pointsPipeline) {
        cb->setGraphicsPipeline(m_pointsPipeline.get());
        cb->setShaderResources(m_srb.get());
        for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
            if (!meshVisible(mi))
                continue;
            const PerMeshRenderSettings meshSettings = renderModeForMesh(mi);
            if (!meshSettings.showPoints)
                continue;
            updateMainUbufForMesh(mi, meshSettings);
            const auto pointVariant = static_cast<Document::PointGpuVariant>(
                pointGpuVariantIndexForSettings(meshSettings));
            const Document::PointsPassGpuView pointsView =
                m_doc->pointsPassGpuView(m_rhi, mi, pointVariant);
            if (!pointsView.valid || !pointsView.vertexBuffer || pointsView.vertexCount <= 0)
                continue;
            const QRhiCommandBuffer::VertexInput pv(pointsView.vertexBuffer, 0);
            cb->setVertexInput(0, 1, &pv);
            cb->draw(pointsView.vertexCount);
        }
    }

    if (drawDecoratorPass) {
        auto setDecoratorColor = [&](int slot, int meshIndex, const QColor &color) -> bool {
            if (slot < 0 || slot >= kDecoratorSlotCount)
                return false;
            if (!m_decoratorPipeline)
                return false;
            if (meshIndex < 0 || meshIndex >= m_doc->meshCount())
                return false;
            QRhiBuffer *decoratorUbuf = m_decoratorUbufs[slot].get();
            QRhiShaderResourceBindings *decoratorSrb = m_decoratorSrbs[slot].get();
            if (!decoratorUbuf || !decoratorSrb)
                return false;
            float decoratorData[kDecoratorUbufSize / sizeof(float)] = {};
            const QMatrix4x4 meshMvp = vp * m_doc->mesh(meshIndex).transform;
            memcpy(decoratorData, meshMvp.constData(), 64);
            decoratorData[16] = color.redF();
            decoratorData[17] = color.greenF();
            decoratorData[18] = color.blueF();
            decoratorData[19] = color.alphaF();
            QRhiResourceUpdateBatch *uDecor = m_rhi->nextResourceUpdateBatch();
            uDecor->updateDynamicBuffer(
                decoratorUbuf, 0, kDecoratorUbufSize, decoratorData);
            cb->resourceUpdate(uDecor);
            cb->setGraphicsPipeline(m_decoratorPipeline.get());
            cb->setShaderResources(decoratorSrb);
            cb->setViewport({ 0, 0, float(sz.width()), float(sz.height()) });
            return true;
        };

        auto drawDecoratorKind =
            [&](int slot, auto shouldDraw, auto colorGetter, auto bufferGetter, auto countGetter) {
            for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
                if (!meshVisible(mi))
                    continue;
                const MeshRenderMode mode = renderModeForMesh(mi);
                if (!shouldDraw(mode))
                    continue;
                if (!setDecoratorColor(slot, mi, colorGetter(mode)))
                    continue;
                const Document::DecoratorPassGpuView decorView =
                    m_doc->decoratorPassGpuView(m_rhi, mi);
                if (!decorView.valid)
                    continue;
                QRhiBuffer *vbuf = bufferGetter(decorView);
                const int vertexCount = countGetter(decorView);
                if (!vbuf || vertexCount <= 0)
                    continue;
                const QRhiCommandBuffer::VertexInput binding(vbuf, 0);
                cb->setVertexInput(0, 1, &binding);
                cb->draw(vertexCount);
            }
        };

        auto drawDecoratorBoundaryFatKind = [&](int slot,
                                                auto shouldDraw,
                                                auto colorGetter,
                                                auto fatBufferGetter,
                                                auto fatCountGetter,
                                                auto lineBufferGetter,
                                                auto lineCountGetter) {
            for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
                if (!meshVisible(mi))
                    continue;
                const MeshRenderMode mode = renderModeForMesh(mi);
                if (!shouldDraw(mode))
                    continue;

                const Document::DecoratorPassGpuView decorView =
                    m_doc->decoratorPassGpuView(m_rhi, mi);
                if (!decorView.valid)
                    continue;

                const QColor decoColor = colorGetter(mode);
                QRhiGraphicsPipeline *fatPipeline = m_decoratorFatPipeline.get();
                QRhiBuffer *fatVbuf = fatBufferGetter(decorView);
                const int fatVertexCount = fatCountGetter(decorView);
                if (fatPipeline && m_decoratorFatUbuf && m_decoratorFatSrb
                    && fatVbuf && fatVertexCount > 0) {
                    float fatData[kDecoratorFatUbufSize / sizeof(float)] = {};
                    const QMatrix4x4 meshMvp = vp * m_doc->mesh(mi).transform;
                    memcpy(fatData, meshMvp.constData(), 64);
                    fatData[16] = decoColor.redF();
                    fatData[17] = decoColor.greenF();
                    fatData[18] = decoColor.blueF();
                    fatData[19] = decoColor.alphaF();
                    fatData[20] = qMax(0.5f, mode.decoratorBoundaryWidth);
                    fatData[21] = 1.0f / float(qMax(1, sz.width()));
                    fatData[22] = 1.0f / float(qMax(1, sz.height()));
                    QRhiResourceUpdateBatch *uFat = m_rhi->nextResourceUpdateBatch();
                    uFat->updateDynamicBuffer(
                        m_decoratorFatUbuf.get(), 0, kDecoratorFatUbufSize, fatData);
                    cb->resourceUpdate(uFat);
                    cb->setGraphicsPipeline(fatPipeline);
                    cb->setShaderResources(m_decoratorFatSrb.get());
                    cb->setViewport({ 0, 0, float(sz.width()), float(sz.height()) });
                    const QRhiCommandBuffer::VertexInput binding(fatVbuf, 0);
                    cb->setVertexInput(0, 1, &binding);
                    cb->draw(fatVertexCount);
                    continue;
                }

                if (!setDecoratorColor(slot, mi, colorGetter(mode)))
                    continue;
                QRhiBuffer *lineVbuf = lineBufferGetter(decorView);
                const int lineVertexCount = lineCountGetter(decorView);
                if (!lineVbuf || lineVertexCount <= 0)
                    continue;
                const QRhiCommandBuffer::VertexInput binding(lineVbuf, 0);
                cb->setVertexInput(0, 1, &binding);
                cb->draw(lineVertexCount);
            }
        };

        bool drawVertexNormals = false;
        bool drawFaceNormals = false;
        bool drawCurvatureDir = false;
        bool drawBoundaryEdges = false;
        bool drawTextureSeams = false;
        bool drawNonManifoldEdges = false;
        bool drawNonManifoldVertices = false;
        for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
            if (!meshVisible(mi))
                continue;
            const MeshRenderMode mode = renderModeForMesh(mi);
            drawVertexNormals = drawVertexNormals || mode.decoratorVertexNormals;
            drawFaceNormals = drawFaceNormals || mode.decoratorFaceNormals;
            drawCurvatureDir = drawCurvatureDir || mode.decoratorCurvatureDir;
            drawBoundaryEdges = drawBoundaryEdges || mode.decoratorBoundaryEdges;
            drawTextureSeams = drawTextureSeams || mode.decoratorTextureSeams;
            drawNonManifoldEdges = drawNonManifoldEdges || mode.decoratorNonManifoldEdges;
            drawNonManifoldVertices = drawNonManifoldVertices || mode.decoratorNonManifoldVertices;
        }

        if (drawVertexNormals) {
            drawDecoratorKind(
                kDecoratorSlotVertexNormals,
                [](const MeshRenderMode &mode) {
                    return mode.decoratorVertexNormals;
                },
                [](const MeshRenderMode &mode) {
                    return mode.decoratorVertexNormalColor;
                },
                [](const Document::DecoratorPassGpuView &view) {
                    return view.vertexNormalsBuffer;
                },
                [](const Document::DecoratorPassGpuView &view) {
                    return view.vertexNormalsVertexCount;
                });
        }
        if (drawFaceNormals) {
            drawDecoratorKind(
                kDecoratorSlotFaceNormals,
                [](const MeshRenderMode &mode) {
                    return mode.decoratorFaceNormals;
                },
                [](const MeshRenderMode &mode) {
                    return mode.decoratorFaceNormalColor;
                },
                [](const Document::DecoratorPassGpuView &view) {
                    return view.faceNormalsBuffer;
                },
                [](const Document::DecoratorPassGpuView &view) {
                    return view.faceNormalsVertexCount;
                });
        }
        if (drawCurvatureDir) {
            drawDecoratorKind(
                kDecoratorSlotCurvaturePD1,
                [](const MeshRenderMode &mode) { return mode.decoratorCurvatureDir; },
                [](const MeshRenderMode &mode) { return mode.decoratorCurvatureDirPD1Color; },
                [](const Document::DecoratorPassGpuView &view) {
                    return view.curvatureDirPD1Buffer;
                },
                [](const Document::DecoratorPassGpuView &view) {
                    return view.curvatureDirPD1VertexCount;
                });
            drawDecoratorKind(
                kDecoratorSlotCurvaturePD2,
                [](const MeshRenderMode &mode) { return mode.decoratorCurvatureDir; },
                [](const MeshRenderMode &mode) { return mode.decoratorCurvatureDirPD2Color; },
                [](const Document::DecoratorPassGpuView &view) {
                    return view.curvatureDirPD2Buffer;
                },
                [](const Document::DecoratorPassGpuView &view) {
                    return view.curvatureDirPD2VertexCount;
                });
        }
        if (drawBoundaryEdges) {
            drawDecoratorBoundaryFatKind(
                kDecoratorSlotBoundaryEdges,
                [](const MeshRenderMode &mode) {
                    return mode.decoratorBoundaryEdges;
                },
                [](const MeshRenderMode &mode) {
                    return mode.decoratorBoundaryEdgeColor;
                },
                [](const Document::DecoratorPassGpuView &view) {
                    return view.boundaryEdgesFatBuffer;
                },
                [](const Document::DecoratorPassGpuView &view) {
                    return view.boundaryEdgesFatVertexCount;
                },
                [](const Document::DecoratorPassGpuView &view) {
                    return view.boundaryEdgesBuffer;
                },
                [](const Document::DecoratorPassGpuView &view) {
                    return view.boundaryEdgesVertexCount;
                });
        }
        if (drawTextureSeams) {
            drawDecoratorBoundaryFatKind(
                kDecoratorSlotTextureSeams,
                [](const MeshRenderMode &mode) {
                    return mode.decoratorTextureSeams;
                },
                [](const MeshRenderMode &mode) {
                    return mode.decoratorTextureSeamColor;
                },
                [](const Document::DecoratorPassGpuView &view) {
                    return view.textureSeamsFatBuffer;
                },
                [](const Document::DecoratorPassGpuView &view) {
                    return view.textureSeamsFatVertexCount;
                },
                [](const Document::DecoratorPassGpuView &view) {
                    return view.textureSeamsBuffer;
                },
                [](const Document::DecoratorPassGpuView &view) {
                    return view.textureSeamsVertexCount;
                });
        }
        if (drawNonManifoldEdges) {
            drawDecoratorBoundaryFatKind(
                kDecoratorSlotNonManifoldEdges,
                [](const MeshRenderMode &mode) {
                    return mode.decoratorNonManifoldEdges;
                },
                [](const MeshRenderMode &mode) {
                    return mode.decoratorNonManifoldEdgeColor;
                },
                [](const Document::DecoratorPassGpuView &view) {
                    return view.nonManifoldEdgesFatBuffer;
                },
                [](const Document::DecoratorPassGpuView &view) {
                    return view.nonManifoldEdgesFatVertexCount;
                },
                [](const Document::DecoratorPassGpuView &view) {
                    return view.nonManifoldEdgesBuffer;
                },
                [](const Document::DecoratorPassGpuView &view) {
                    return view.nonManifoldEdgesVertexCount;
                });
        }
        if (drawNonManifoldVertices) {
            // Draw non-manifold vertices as points using the dedicated point pipeline.
            const int slot = kDecoratorSlotNonManifoldVertices;
            for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
                if (!meshVisible(mi))
                    continue;
                const MeshRenderMode mode = renderModeForMesh(mi);
                if (!mode.decoratorNonManifoldVertices)
                    continue;
                const Document::DecoratorPassGpuView decorView =
                    m_doc->decoratorPassGpuView(m_rhi, mi);
                if (!decorView.valid || !decorView.nonManifoldVerticesBuffer
                    || decorView.nonManifoldVerticesVertexCount <= 0)
                    continue;
                if (!setDecoratorColor(slot, mi, mode.decoratorNonManifoldVertexColor))
                    continue;
                if (!m_decoratorPointPipeline)
                    continue;
                cb->setGraphicsPipeline(m_decoratorPointPipeline.get());
                cb->setShaderResources(m_decoratorSrbs[slot].get());
                cb->setViewport({ 0, 0, float(sz.width()), float(sz.height()) });
                const QRhiCommandBuffer::VertexInput binding(
                    decorView.nonManifoldVerticesBuffer, 0);
                cb->setVertexInput(0, 1, &binding);
                cb->draw(decorView.nonManifoldVerticesVertexCount);
            }
        }
    } // end drawDecoratorPass

    if (drawTrackballGizmo && m_trackballGizmoPipeline && m_trackballGizmoVbuf && m_trackballGizmoSrb) {
        cb->setGraphicsPipeline(m_trackballGizmoPipeline.get());
        cb->setShaderResources(m_trackballGizmoSrb.get());
        cb->setViewport({ 0, 0, float(sz.width()), float(sz.height()) });
        const QRhiCommandBuffer::VertexInput gv(m_trackballGizmoVbuf.get(), 0);
        cb->setVertexInput(0, 1, &gv);
        cb->draw(m_trackballGizmoVertexCount);
    }

    if (drawCurrentMeshHighlight)
        drawCurrentMeshOutline(cb, sz);

    if (drawSelectionPass
        && m_selectionUbuf
        && m_selectionSrb
        && (m_selectionFacesPipeline || m_selectionVerticesPipeline)) {
        cb->setViewport({ 0, 0, float(sz.width()), float(sz.height()) });

        for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
            if (!meshVisible(mi))
                continue;
            const MeshRenderMode mode = renderModeForMesh(mi);
            if (!mode.showSelection || (!mode.showSelectionVertices && !mode.showSelectionFaces))
                continue;

            const Document::SelectionPassGpuView selectionView =
                m_doc->selectionPassGpuView(m_rhi, mi);
            if (!selectionView.valid)
                continue;

            float selectionData[kDecoratorUbufSize / sizeof(float)] = {};
            const QMatrix4x4 meshMvp = vp * m_doc->mesh(mi).transform;
            memcpy(selectionData, meshMvp.constData(), 64);
            selectionData[16] = 1.0f;
            selectionData[17] = 0.0f;
            selectionData[18] = 0.0f;
            selectionData[19] = 0.5f;
            QRhiResourceUpdateBatch *uSel = m_rhi->nextResourceUpdateBatch();
            uSel->updateDynamicBuffer(
                m_selectionUbuf.get(), 0, kDecoratorUbufSize, selectionData);
            cb->resourceUpdate(uSel);

            if (mode.showSelectionFaces
                && m_selectionFacesPipeline
                && selectionView.selectedFacesBuffer
                && selectionView.selectedFacesVertexCount > 0) {
                cb->setGraphicsPipeline(m_selectionFacesPipeline.get());
                cb->setShaderResources(m_selectionSrb.get());
                const QRhiCommandBuffer::VertexInput fv(
                    selectionView.selectedFacesBuffer, 0);
                cb->setVertexInput(0, 1, &fv);
                cb->draw(selectionView.selectedFacesVertexCount);
            }

            if (mode.showSelectionVertices
                && m_selectionVerticesPipeline
                && selectionView.selectedVerticesBuffer
                && selectionView.selectedVerticesVertexCount > 0) {
                cb->setGraphicsPipeline(m_selectionVerticesPipeline.get());
                cb->setShaderResources(m_selectionSrb.get());
                const QRhiCommandBuffer::VertexInput vv(
                    selectionView.selectedVerticesBuffer, 0);
                cb->setVertexInput(0, 1, &vv);
                cb->draw(selectionView.selectedVerticesVertexCount);
            }
        }
    }

    if (needMvpForFrame) {
        updateBoundingBoxCornersOverlayPlacement(vp, view, sz);
    }

    cb->endPass();

    const float cpuMs = m_frameTimer.nsecsElapsed() / 1e6f;

    const bool gpuTimingSupported = m_rhi->isFeatureSupported(QRhi::Timestamps);
    float gpuMs = 0.0f;
    bool gpuSampleValid = false;
    if (gpuTimingSupported) {
        // API returns elapsed seconds for the last completed frame.
        const double gpuSeconds = cb->lastCompletedGpuTime();
        if (gpuSeconds > 0.0) {
            gpuMs = static_cast<float>(gpuSeconds * 1000.0);
            gpuSampleValid = true;
        }
    }

    emit frameRendered(cpuMs, gpuMs, gpuTimingSupported, gpuSampleValid);
}
