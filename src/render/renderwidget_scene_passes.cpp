#include "renderwidget.h"
#include "document.h"
#include "renderwidget_internal.h"
#include <cstring>

using namespace RenderWidgetInternal;

void RenderWidget::renderSceneBufferItems(
    QRhiCommandBuffer *cb,
    const RenderFramePlan &plan,
    const std::vector<SceneBufferDrawItem> &items)
{
    if (items.empty())
        return;

    cb->setViewport({
        0,
        0,
        float(plan.pixelSize.width()),
        float(plan.pixelSize.height())
    });
    for (const SceneBufferDrawItem &item : items) {
        if (!item.pipeline || !item.vertexBuffer || item.vertexCount <= 0)
            continue;
        cb->setGraphicsPipeline(item.pipeline);
        cb->setShaderResources(m_srb.get());
        uploadMainUbufForMesh(
            cb,
            item.meshIndex,
            plan.proj,
            plan.view,
            item.meshSettings,
            plan.pixelSize,
            true,
            plan.lightDir);
        const QRhiCommandBuffer::VertexInput binding(item.vertexBuffer, 0);
        cb->setVertexInput(0, 1, &binding);
        cb->draw(item.vertexCount);
    }
}

void RenderWidget::renderSceneDecoratorItems(
    QRhiCommandBuffer *cb,
    const RenderFramePlan &plan)
{
    if (plan.decoratorItems.empty())
        return;

    const QSize &sz = plan.pixelSize;
    const QMatrix4x4 frameVp = plan.proj * plan.view;
    auto uploadDecoratorColor = [&](const SceneDecoratorDrawItem &item) -> bool {
        if (item.slot < 0 || item.slot >= kDecoratorSlotCount)
            return false;
        if (item.meshIndex < 0 || item.meshIndex >= m_doc->meshCount())
            return false;
        QRhiBuffer *decoratorUbuf = m_decoratorUbufs[item.slot].get();
        QRhiShaderResourceBindings *decoratorSrb = m_decoratorSrbs[item.slot].get();
        if (!decoratorUbuf || !decoratorSrb)
            return false;
        float decoratorData[kDecoratorUbufSize / sizeof(float)] = {};
        const QMatrix4x4 meshMvp = frameVp * m_doc->mesh(item.meshIndex).transform;
        memcpy(decoratorData, meshMvp.constData(), 64);
        decoratorData[16] = item.color.redF();
        decoratorData[17] = item.color.greenF();
        decoratorData[18] = item.color.blueF();
        decoratorData[19] = item.color.alphaF();
        QRhiResourceUpdateBatch *uDecor = m_rhi->nextResourceUpdateBatch();
        uDecor->updateDynamicBuffer(
            decoratorUbuf, 0, kDecoratorUbufSize, decoratorData);
        cb->resourceUpdate(uDecor);
        return true;
    };
    auto uploadDecoratorFat = [&](const SceneDecoratorDrawItem &item) -> bool {
        if (item.meshIndex < 0 || item.meshIndex >= m_doc->meshCount())
            return false;
        if (!m_decoratorFatUbuf || !m_decoratorFatSrb)
            return false;
        float fatData[kDecoratorFatUbufSize / sizeof(float)] = {};
        const QMatrix4x4 meshMvp = frameVp * m_doc->mesh(item.meshIndex).transform;
        memcpy(fatData, meshMvp.constData(), 64);
        fatData[16] = item.color.redF();
        fatData[17] = item.color.greenF();
        fatData[18] = item.color.blueF();
        fatData[19] = item.color.alphaF();
        fatData[20] = qMax(0.5f, item.width);
        fatData[21] = 1.0f / float(qMax(1, sz.width()));
        fatData[22] = 1.0f / float(qMax(1, sz.height()));
        QRhiResourceUpdateBatch *uFat = m_rhi->nextResourceUpdateBatch();
        uFat->updateDynamicBuffer(
            m_decoratorFatUbuf.get(), 0, kDecoratorFatUbufSize, fatData);
        cb->resourceUpdate(uFat);
        return true;
    };

    cb->setViewport({ 0, 0, float(sz.width()), float(sz.height()) });
    for (const SceneDecoratorDrawItem &item : plan.decoratorItems) {
        if (!item.vertexBuffer || item.vertexCount <= 0)
            continue;

        QRhiShaderResourceBindings *srb = nullptr;
        switch (item.kind) {
        case SceneDecoratorDrawKind::Line:
            if (!m_decoratorPipeline || !uploadDecoratorColor(item))
                continue;
            srb = m_decoratorSrbs[item.slot].get();
            cb->setGraphicsPipeline(m_decoratorPipeline.get());
            cb->setShaderResources(srb);
            break;
        case SceneDecoratorDrawKind::FatLine:
            if (!m_decoratorFatPipeline || !uploadDecoratorFat(item))
                continue;
            cb->setGraphicsPipeline(m_decoratorFatPipeline.get());
            cb->setShaderResources(m_decoratorFatSrb.get());
            break;
        case SceneDecoratorDrawKind::Point:
            if (!m_decoratorPointPipeline || !uploadDecoratorColor(item))
                continue;
            srb = m_decoratorSrbs[item.slot].get();
            cb->setGraphicsPipeline(m_decoratorPointPipeline.get());
            cb->setShaderResources(srb);
            break;
        }

        const QRhiCommandBuffer::VertexInput binding(item.vertexBuffer, 0);
        cb->setVertexInput(0, 1, &binding);
        cb->draw(item.vertexCount);
    }
}

void RenderWidget::renderSceneSelectionItems(
    QRhiCommandBuffer *cb,
    const RenderFramePlan &plan)
{
    if (plan.selectionItems.empty()
        || !m_selectionUbuf
        || !m_selectionSrb
        || (!m_selectionFacesPipeline && !m_selectionVerticesPipeline)) {
        return;
    }

    const QSize &sz = plan.pixelSize;
    cb->setViewport({ 0, 0, float(sz.width()), float(sz.height()) });
    const QMatrix4x4 frameVp = plan.proj * plan.view;

    for (const SceneSelectionDrawItem &item : plan.selectionItems) {
        if (item.meshIndex < 0 || item.meshIndex >= m_doc->meshCount())
            continue;
        const MeshGpuResourceCache::SelectionPassView &selectionView = item.selectionView;
        if (!selectionView.valid)
            continue;

        float selectionData[kDecoratorUbufSize / sizeof(float)] = {};
        const QMatrix4x4 meshMvp = frameVp * m_doc->mesh(item.meshIndex).transform;
        memcpy(selectionData, meshMvp.constData(), 64);
        selectionData[16] = 1.0f;
        selectionData[17] = 0.0f;
        selectionData[18] = 0.0f;
        selectionData[19] = 0.5f;
        QRhiResourceUpdateBatch *uSel = m_rhi->nextResourceUpdateBatch();
        uSel->updateDynamicBuffer(
            m_selectionUbuf.get(), 0, kDecoratorUbufSize, selectionData);
        cb->resourceUpdate(uSel);

        if (item.drawFaces
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

        if (item.drawVertices
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
