#include "renderwidget.h"
#include "document.h"

RenderWidget::RenderFramePlan RenderWidget::buildRenderFramePlan(
    const QSize &pixelSize,
    const QMatrix4x4 &proj,
    const QMatrix4x4 &view,
    const QVector3D &lightDir,
    bool drawFillPass,
    bool drawWirePass,
    bool drawEdgesPass,
    bool drawBBoxPass,
    bool drawPointsPass)
{
    RenderFramePlan plan;
    plan.viewMode = m_viewMode;
    plan.pixelSize = pixelSize;
    plan.proj = proj;
    plan.view = view;
    plan.lightDir = lightDir;
    plan.drawFillPass = drawFillPass;

    if (plan.drawFillPass)
        plan.sceneFill = buildSceneFillFramePlan(pixelSize, proj, view, lightDir);

    auto appendBufferDrawItem =
        [](std::vector<SceneBufferDrawItem> &items,
           int meshIndex,
           QRhiGraphicsPipeline *pipeline,
           const PerMeshRenderSettings &meshSettings,
           QRhiBuffer *vertexBuffer,
           int vertexCount) {
            if (!pipeline || !vertexBuffer || vertexCount <= 0)
                return;
            items.push_back(SceneBufferDrawItem {
                meshIndex,
                pipeline,
                meshSettings,
                vertexBuffer,
                vertexCount
            });
        };

    const bool buildSimpleBufferItems =
        drawWirePass || drawEdgesPass || (drawBBoxPass && m_bboxPipeline)
        || (drawPointsPass && m_pointsPipeline);
    if (buildSimpleBufferItems) {
        for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
            if (!meshVisible(mi))
                continue;

            const PerMeshRenderSettings meshSettings = renderModeForMesh(mi);

            if (drawWirePass && meshSettings.showWire) {
                const MeshGpuResourceCache::WirePassView wireView =
                    m_doc->wirePassGpuView(m_rhi, mi);
                if (wireView.valid) {
                    appendBufferDrawItem(
                        plan.wireItems,
                        mi,
                        wirePipelineForSettings(meshSettings),
                        meshSettings,
                        wireView.vertexBuffer,
                        wireView.vertexCount);
                }
            }

            if (drawEdgesPass && meshSettings.showEdges) {
                bool edgeItemAppended = false;
                const MeshGpuResourceCache::EdgeFatPassView fatView =
                    m_doc->edgeFatPassGpuView(m_rhi, mi);
                if (fatView.valid) {
                    const size_t before = plan.edgeItems.size();
                    appendBufferDrawItem(
                        plan.edgeItems,
                        mi,
                        fatEdgesPipelineForSettings(meshSettings),
                        meshSettings,
                        fatView.vertexBuffer,
                        fatView.vertexCount);
                    edgeItemAppended = (plan.edgeItems.size() != before);
                }

                if (!edgeItemAppended) {
                    const MeshGpuResourceCache::EdgePassView lineView =
                        m_doc->edgePassGpuView(m_rhi, mi);
                    if (lineView.valid) {
                        appendBufferDrawItem(
                            plan.edgeItems,
                            mi,
                            edgesPipelineForSettings(meshSettings),
                            meshSettings,
                            lineView.vertexBuffer,
                            lineView.vertexCount);
                    }
                }
            }

            if (drawBBoxPass && m_bboxPipeline && meshSettings.showBoundingBox) {
                const MeshGpuResourceCache::BBoxPassView bboxView =
                    m_doc->bboxPassGpuView(m_rhi, mi);
                if (bboxView.valid) {
                    appendBufferDrawItem(
                        plan.boundingBoxItems,
                        mi,
                        m_bboxPipeline.get(),
                        meshSettings,
                        bboxView.vertexBuffer,
                        bboxView.vertexCount);
                }
            }

            if (drawPointsPass && m_pointsPipeline && meshSettings.showPoints) {
                const auto pointVariant = static_cast<Document::PointGpuVariant>(
                    pointGpuVariantIndexForSettings(meshSettings));
                const MeshGpuResourceCache::PointsPassView pointsView =
                    m_doc->pointsPassGpuView(m_rhi, mi, pointVariant);
                if (pointsView.valid) {
                    appendBufferDrawItem(
                        plan.pointItems,
                        mi,
                        m_pointsPipeline.get(),
                        meshSettings,
                        pointsView.vertexBuffer,
                        pointsView.vertexCount);
                }
            }
        }
    }

    return plan;
}
