#include "renderwidget.h"
#include "document.h"
#include "renderwidget_internal.h"

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
    if (!m_rhi || !m_ubuf)
        return;

    advanceCenterAnimation();
    syncPerMeshRenderModesWithDocument();

    const bool drawTrackballGizmo = (m_doc->meshCount() > 0);
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
        drawDecoratorPass =
            drawDecoratorPass
            || mode.decoratorVertexNormals
            || mode.decoratorFaceNormals
            || mode.decoratorBoundaryEdges
            || mode.decoratorTextureSeams;
    }
    const bool anyDrawPass =
        drawFillPass || drawWirePass || drawEdgesPass || drawBBoxPass || drawPointsPass || drawDecoratorPass
        || drawCurrentMeshHighlight || drawTrackballGizmo;
    const bool needMvpForFrame = anyDrawPass || m_depthPickPending;

    if (anyDrawPass)
        prepareDirtyBuffers(cb);

    m_frameTimer.start();

    const QSize sz = renderTarget()->pixelSize();

    QRhiResourceUpdateBatch *u = nullptr;
    QMatrix4x4 mvp;
    float baseUbufData[kUbufFloatCount] = {};
    bool haveBaseUbufData = false;
    if (needMvpForFrame) {
        const float aspect = sz.width() / float(sz.height());
        const float sceneRadius = m_trackball.radius();

        QMatrix4x4 proj;
        proj.perspective(
            m_trackball.fovYDegrees(),
            aspect,
            0.01f * sceneRadius,
            100.0f * sceneRadius);

        const QMatrix4x4 view = m_trackball.viewMatrix();

        QMatrix4x4 modelView = view;

        mvp = proj * view;
        QMatrix3x3 normalMat = modelView.normalMatrix();

        // Pack uniform: mat4 mvp + mat4 modelView + mat3 as 3 vec4 (std140) + render colors/params.
        memset(baseUbufData, 0, sizeof(baseUbufData));
        memcpy(baseUbufData, mvp.constData(), 64);
        memcpy(baseUbufData + 16, modelView.constData(), 64);
        // std140: mat3 is stored as 3 columns of vec4
        const float *n = normalMat.constData();
        baseUbufData[32] = n[0]; baseUbufData[33] = n[1]; baseUbufData[34] = n[2]; baseUbufData[35] = 0;
        baseUbufData[36] = n[3]; baseUbufData[37] = n[4]; baseUbufData[38] = n[5]; baseUbufData[39] = 0;
        baseUbufData[40] = n[6]; baseUbufData[41] = n[7]; baseUbufData[42] = n[8]; baseUbufData[43] = 0;
        baseUbufData[kUbufBBoxColorOffset + 0] = m_renderSettings.bboxWireColor.redF();
        baseUbufData[kUbufBBoxColorOffset + 1] = m_renderSettings.bboxWireColor.greenF();
        baseUbufData[kUbufBBoxColorOffset + 2] = m_renderSettings.bboxWireColor.blueF();
        baseUbufData[kUbufBBoxColorOffset + 3] = m_renderSettings.bboxWireColor.alphaF();
        baseUbufData[kUbufPointColorOffset + 0] = m_renderSettings.pointColor.redF();
        baseUbufData[kUbufPointColorOffset + 1] = m_renderSettings.pointColor.greenF();
        baseUbufData[kUbufPointColorOffset + 2] = m_renderSettings.pointColor.blueF();
        baseUbufData[kUbufPointColorOffset + 3] = m_renderSettings.pointColor.alphaF();
        baseUbufData[kUbufPointParamsOffset + 0] = m_renderSettings.pointSize;
        baseUbufData[kUbufWireColorOffset + 0] = m_renderSettings.wireColor.redF();
        baseUbufData[kUbufWireColorOffset + 1] = m_renderSettings.wireColor.greenF();
        baseUbufData[kUbufWireColorOffset + 2] = m_renderSettings.wireColor.blueF();
        // Wire pass is intentionally translucent to compose independently over fill/effects.
        baseUbufData[kUbufWireColorOffset + 3] = m_renderSettings.wireColor.alphaF() * 0.7f;
        baseUbufData[kUbufWireParamsOffset + 0] = m_renderSettings.wireSize;
        baseUbufData[kUbufFillColorOffset + 0] = m_renderSettings.fillColor.redF();
        baseUbufData[kUbufFillColorOffset + 1] = m_renderSettings.fillColor.greenF();
        baseUbufData[kUbufFillColorOffset + 2] = m_renderSettings.fillColor.blueF();
        baseUbufData[kUbufFillColorOffset + 3] = m_renderSettings.fillColor.alphaF();
        // bbox lighting removed: slot 0 intentionally unused/reserved.
        baseUbufData[kUbufLightingParamsOffset + 0] = 0.0f;
        baseUbufData[kUbufLightingParamsOffset + 1] = m_renderSettings.pointLighting ? 1.0f : 0.0f;
        baseUbufData[kUbufLightingParamsOffset + 2] = m_renderSettings.wireLighting ? 1.0f : 0.0f;
        baseUbufData[kUbufLightingParamsOffset + 3] = m_renderSettings.fillLighting ? 1.0f : 0.0f;
        baseUbufData[kUbufEdgeColorOffset + 0] = m_renderSettings.edgeColor.redF();
        baseUbufData[kUbufEdgeColorOffset + 1] = m_renderSettings.edgeColor.greenF();
        baseUbufData[kUbufEdgeColorOffset + 2] = m_renderSettings.edgeColor.blueF();
        baseUbufData[kUbufEdgeColorOffset + 3] = m_renderSettings.edgeColor.alphaF();

        u = m_rhi->nextResourceUpdateBatch();
        u->updateDynamicBuffer(m_ubuf.get(), 0, kUbufSize, baseUbufData);
        haveBaseUbufData = true;

        if (drawTrackballGizmo && m_trackballGizmoUbuf && m_trackballGizmoVbuf) {
            const auto &verts = trackballGizmoVertices();
            u->updateDynamicBuffer(
                m_trackballGizmoVbuf.get(),
                0,
                int(verts.size() * sizeof(float)),
                verts.data());

            float gizmoData[kTrackballGizmoUbufSize / sizeof(float)] = {};
            memcpy(gizmoData, mvp.constData(), 64);
            const QVector3D center = m_trackball.center();
            gizmoData[16] = center.x();
            gizmoData[17] = center.y();
            gizmoData[18] = center.z();
            gizmoData[19] = m_trackball.gizmoWorldRadius();
            u->updateDynamicBuffer(
                m_trackballGizmoUbuf.get(),
                0,
                kTrackballGizmoUbufSize,
                gizmoData);
        }
    }

    auto updateStyleUbuf = [&](const RenderSettings &meshSettings) {
        if (!haveBaseUbufData)
            return;
        float ubufData[kUbufFloatCount];
        memcpy(ubufData, baseUbufData, sizeof(ubufData));
        ubufData[kUbufBBoxColorOffset + 0] = meshSettings.bboxWireColor.redF();
        ubufData[kUbufBBoxColorOffset + 1] = meshSettings.bboxWireColor.greenF();
        ubufData[kUbufBBoxColorOffset + 2] = meshSettings.bboxWireColor.blueF();
        ubufData[kUbufBBoxColorOffset + 3] = meshSettings.bboxWireColor.alphaF();
        ubufData[kUbufPointColorOffset + 0] = meshSettings.pointColor.redF();
        ubufData[kUbufPointColorOffset + 1] = meshSettings.pointColor.greenF();
        ubufData[kUbufPointColorOffset + 2] = meshSettings.pointColor.blueF();
        ubufData[kUbufPointColorOffset + 3] = meshSettings.pointColor.alphaF();
        ubufData[kUbufPointParamsOffset + 0] = meshSettings.pointSize;
        ubufData[kUbufWireColorOffset + 0] = meshSettings.wireColor.redF();
        ubufData[kUbufWireColorOffset + 1] = meshSettings.wireColor.greenF();
        ubufData[kUbufWireColorOffset + 2] = meshSettings.wireColor.blueF();
        ubufData[kUbufWireColorOffset + 3] = meshSettings.wireColor.alphaF() * 0.7f;
        ubufData[kUbufWireParamsOffset + 0] = meshSettings.wireSize;
        ubufData[kUbufFillColorOffset + 0] = meshSettings.fillColor.redF();
        ubufData[kUbufFillColorOffset + 1] = meshSettings.fillColor.greenF();
        ubufData[kUbufFillColorOffset + 2] = meshSettings.fillColor.blueF();
        ubufData[kUbufFillColorOffset + 3] = meshSettings.fillColor.alphaF();
        ubufData[kUbufLightingParamsOffset + 1] = meshSettings.pointLighting ? 1.0f : 0.0f;
        ubufData[kUbufLightingParamsOffset + 2] = meshSettings.wireLighting ? 1.0f : 0.0f;
        ubufData[kUbufLightingParamsOffset + 3] = meshSettings.fillLighting ? 1.0f : 0.0f;
        ubufData[kUbufEdgeColorOffset + 0] = meshSettings.edgeColor.redF();
        ubufData[kUbufEdgeColorOffset + 1] = meshSettings.edgeColor.greenF();
        ubufData[kUbufEdgeColorOffset + 2] = meshSettings.edgeColor.blueF();
        ubufData[kUbufEdgeColorOffset + 3] = meshSettings.edgeColor.alphaF();

        QRhiResourceUpdateBatch *uMesh = m_rhi->nextResourceUpdateBatch();
        uMesh->updateDynamicBuffer(m_ubuf.get(), 0, kUbufSize, ubufData);
        cb->resourceUpdate(uMesh);
    };

    const RenderSettings currentMeshSettingsForPick =
        (currentMeshIndex >= 0) ? renderSettingsForMesh(currentMeshIndex) : m_renderSettings;
    const int pointVariantIndex = pointGpuVariantIndexForSettings(currentMeshSettingsForPick);
    if (m_depthPickPending) {
        if (u) {
            cb->resourceUpdate(u);
            u = nullptr;
        }
        executePendingDepthPick(cb, mvp, sz, pointVariantIndex);
    }

    if (drawCurrentMeshHighlight) {
        if (u) {
            cb->resourceUpdate(u);
            u = nullptr;
        }
        renderCurrentMeshMask(cb, sz);
        processCurrentMeshMask(cb, sz);
    }

    cb->beginPass(renderTarget(), QColor(40, 40, 40), { 1.0f, 0 }, u);

    if (drawFillPass) {
        cb->setViewport({ 0, 0, float(sz.width()), float(sz.height()) });

        for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
            if (!meshVisible(mi))
                continue;
            const RenderSettings meshSettings = renderSettingsForMesh(mi);
            if (!meshSettings.showFill)
                continue;
            QRhiGraphicsPipeline *fillPipeline = fillPipelineForSettings(meshSettings);
            if (!fillPipeline)
                continue;
            cb->setGraphicsPipeline(fillPipeline);
            updateStyleUbuf(meshSettings);
            const auto fillVariant = static_cast<Document::FillGpuVariant>(
                fillGpuVariantIndexForSettings(meshSettings));
            const Document::FillPassGpuView fillView =
                m_doc->fillPassGpuView(m_rhi, mi, fillVariant);
            if (!fillView.valid)
                continue;

            for (int bi = 0; bi < fillView.batchCount; ++bi) {
                const auto &batch = fillView.batches[bi];
                if (!batch.vertexBuffer || (batch.indexCount == 0 && batch.vertexCount == 0))
                    continue;

                cb->setShaderResources(shaderResourcesForTexture(batch.texture));
                const QRhiCommandBuffer::VertexInput vbufBinding(batch.vertexBuffer, 0);
                if (batch.indexCount > 0 && batch.indexBuffer) {
                    cb->setVertexInput(
                        0, 1, &vbufBinding, batch.indexBuffer, 0, QRhiCommandBuffer::IndexUInt32);
                    cb->drawIndexed(batch.indexCount);
                } else {
                    cb->setVertexInput(0, 1, &vbufBinding);
                    cb->draw(batch.vertexCount);
                }
            }
        }
    }

    if (drawWirePass) {
        cb->setViewport({ 0, 0, float(sz.width()), float(sz.height()) });
        cb->setShaderResources();

        for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
            if (!meshVisible(mi))
                continue;
            const RenderSettings meshSettings = renderSettingsForMesh(mi);
            if (!meshSettings.showWire)
                continue;
            QRhiGraphicsPipeline *wirePipeline = wirePipelineForSettings(meshSettings);
            if (!wirePipeline)
                continue;
            cb->setGraphicsPipeline(wirePipeline);
            updateStyleUbuf(meshSettings);
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
        cb->setShaderResources();
        for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
            if (!meshVisible(mi))
                continue;
            const RenderSettings meshSettings = renderSettingsForMesh(mi);
            if (!meshSettings.showEdges)
                continue;
            QRhiGraphicsPipeline *edgesPipeline = edgesPipelineForSettings(meshSettings);
            if (!edgesPipeline)
                continue;
            cb->setGraphicsPipeline(edgesPipeline);
            updateStyleUbuf(meshSettings);
            const Document::EdgePassGpuView edgeView = m_doc->edgePassGpuView(m_rhi, mi);
            if (!edgeView.valid || !edgeView.vertexBuffer || edgeView.vertexCount <= 0)
                continue;
            const QRhiCommandBuffer::VertexInput ev(edgeView.vertexBuffer, 0);
            cb->setVertexInput(0, 1, &ev);
            cb->draw(edgeView.vertexCount);
        }
    }

    if (drawBBoxPass && m_bboxPipeline) {
        cb->setGraphicsPipeline(m_bboxPipeline.get());
        cb->setShaderResources();
        for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
            if (!meshVisible(mi))
                continue;
            const RenderSettings meshSettings = renderSettingsForMesh(mi);
            if (!meshSettings.showBoundingBox)
                continue;
            updateStyleUbuf(meshSettings);
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
        cb->setShaderResources();
        for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
            if (!meshVisible(mi))
                continue;
            const RenderSettings meshSettings = renderSettingsForMesh(mi);
            if (!meshSettings.showPoints)
                continue;
            updateStyleUbuf(meshSettings);
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

    if (drawDecoratorPass && m_decoratorPipeline) {
        auto setDecoratorColor = [&](int slot, const QColor &color) -> bool {
            if (slot < 0 || slot >= kDecoratorSlotCount)
                return false;
            QRhiBuffer *decoratorUbuf = m_decoratorUbufs[slot].get();
            QRhiShaderResourceBindings *decoratorSrb = m_decoratorSrbs[slot].get();
            if (!decoratorUbuf || !decoratorSrb)
                return false;
            float decoratorData[kDecoratorUbufSize / sizeof(float)] = {};
            memcpy(decoratorData, mvp.constData(), 64);
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
                if (!setDecoratorColor(slot, colorGetter(mode)))
                    return;
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

        bool drawVertexNormals = false;
        bool drawFaceNormals = false;
        bool drawBoundaryEdges = false;
        bool drawTextureSeams = false;
        for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
            if (!meshVisible(mi))
                continue;
            const MeshRenderMode mode = renderModeForMesh(mi);
            drawVertexNormals = drawVertexNormals || mode.decoratorVertexNormals;
            drawFaceNormals = drawFaceNormals || mode.decoratorFaceNormals;
            drawBoundaryEdges = drawBoundaryEdges || mode.decoratorBoundaryEdges;
            drawTextureSeams = drawTextureSeams || mode.decoratorTextureSeams;
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
        if (drawBoundaryEdges) {
            drawDecoratorKind(
                kDecoratorSlotBoundaryEdges,
                [](const MeshRenderMode &mode) {
                    return mode.decoratorBoundaryEdges;
                },
                [](const MeshRenderMode &mode) {
                    return mode.decoratorBoundaryEdgeColor;
                },
                [](const Document::DecoratorPassGpuView &view) {
                    return view.boundaryEdgesBuffer;
                },
                [](const Document::DecoratorPassGpuView &view) {
                    return view.boundaryEdgesVertexCount;
                });
        }
        if (drawTextureSeams) {
            drawDecoratorKind(
                kDecoratorSlotTextureSeams,
                [](const MeshRenderMode &mode) {
                    return mode.decoratorTextureSeams;
                },
                [](const MeshRenderMode &mode) {
                    return mode.decoratorTextureSeamColor;
                },
                [](const Document::DecoratorPassGpuView &view) {
                    return view.textureSeamsBuffer;
                },
                [](const Document::DecoratorPassGpuView &view) {
                    return view.textureSeamsVertexCount;
                });
        }
    }

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

    if (needMvpForFrame) {
        const QMatrix4x4 view = m_trackball.viewMatrix();
        updateBoundingBoxCornersOverlayPlacement(mvp, view, sz);
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

