#include "renderwidget.h"
#include "document.h"
#include "renderwidget_internal.h"
#include <algorithm>
#include <vector>

using namespace RenderWidgetInternal;

class RenderWidget::FillRenderServices final
{
public:
    explicit FillRenderServices(RenderWidget &widget) : m_widget(widget) {}

    void uploadMainUbufForMesh(
        QRhiCommandBuffer *cb,
        int meshIndex,
        const QMatrix4x4 &proj,
        const QMatrix4x4 &view,
        const PerMeshRenderSettings &meshSettings,
        const QSize &pixelSize,
        bool enableLighting,
        const QVector3D &lightDir,
        MainUbufMaterialOverrides materialOverrides) const
    {
        m_widget.uploadMainUbufForMesh(
            cb,
            meshIndex,
            proj,
            view,
            meshSettings,
            pixelSize,
            enableLighting,
            lightDir,
            materialOverrides);
    }

    QRhiTexture *resolveSelectedPbrTexture(
        int meshIndex,
        int textureIndex,
        const MeshGpuResourceCache::FillPassView &fillView) const
    {
        return m_widget.resolveSelectedPbrTexture(meshIndex, textureIndex, fillView);
    }

    QRhiShaderResourceBindings *shaderResourcesForFillTextures(
        QRhiTexture *baseColorTexture,
        QRhiTexture *normalTexture,
        QRhiTexture *occlusionTexture,
        QRhiTexture *roughnessTexture,
        bool nearest = false) const
    {
        return m_widget.shaderResourcesForFillTextures(
            baseColorTexture,
            normalTexture,
            occlusionTexture,
            roughnessTexture,
            nearest);
    }

    QRhiTexture *radianceScalingGradientTexture() const
    {
        return m_widget.m_rsGradTexture ? m_widget.m_rsGradTexture.get() : nullptr;
    }

    bool ensureRadianceScalingGradientResources(const QSize &pixelSize) const
    {
        m_widget.ensureRsGradResources(pixelSize);
        return m_widget.m_rsGradRt && m_widget.m_rsGradPipeline && m_widget.m_rsGradSrb;
    }

    QRhiTextureRenderTarget *radianceScalingGradientRenderTarget() const
    {
        return m_widget.m_rsGradRt.get();
    }

    QRhiGraphicsPipeline *radianceScalingGradientPipeline() const
    {
        return m_widget.m_rsGradPipeline.get();
    }

    QRhiShaderResourceBindings *radianceScalingGradientShaderResources() const
    {
        return m_widget.m_rsGradSrb.get();
    }

private:
    RenderWidget &m_widget;
};

class RenderWidget::FillMaterialRenderer
{
public:
    virtual ~FillMaterialRenderer() = default;

    virtual void renderPrepass(
        const SceneFillFrameContext &ctx,
        const std::vector<SceneFillDrawItem> &drawItems) const
    {
        Q_UNUSED(ctx);
        Q_UNUSED(drawItems);
    }

    virtual void drawBatch(
        const SceneFillDrawContext &ctx,
        const MeshGpuResourceCache::FillBatchView &batch) const = 0;
};

class RenderWidget::PlainFillRenderer final : public FillMaterialRenderer
{
public:
    void drawBatch(
        const SceneFillDrawContext &ctx,
        const MeshGpuResourceCache::FillBatchView &batch) const override
    {
        const SceneFillDrawItem &item = ctx.item;
        const SceneFillFrameContext &frame = ctx.frame;
        frame.services.uploadMainUbufForMesh(
            frame.cb,
            item.meshIndex,
            frame.proj,
            frame.view,
            item.meshSettings,
            frame.pixelSize,
            true,
            frame.lightDir,
            MainUbufMaterialOverrides {
                item.meshSettings.fillPbr.normalScale * batch.normalScale,
                1.0f,
                1.0f });

        // When Texture source is selected, resolve the user-chosen texture by index;
        // fall back to the batch's baked base-colour texture if it cannot be found.
        QRhiTexture *albedo = batch.baseColorTexture;
        if (item.meshSettings.fillPlain.colorSource == FillColorSource::Texture) {
            if (QRhiTexture *t = frame.services.resolveSelectedPbrTexture(
                    item.meshIndex,
                    item.meshSettings.fillPlain.textureIndex,
                    item.fillView)) {
                albedo = t;
            }
        }
        frame.cb->setShaderResources(
            frame.services.shaderResourcesForFillTextures(albedo, nullptr, nullptr, nullptr));
        drawBatchGeometry(frame.cb, batch);
    }
};

class RenderWidget::PbrFillRenderer final : public FillMaterialRenderer
{
public:
    void drawBatch(
        const SceneFillDrawContext &ctx,
        const MeshGpuResourceCache::FillBatchView &batch) const override
    {
        const SceneFillDrawItem &item = ctx.item;
        const SceneFillFrameContext &frame = ctx.frame;
        const auto &pbr = item.meshSettings.fillPbr;
        QRhiTexture *albedo =
            (pbr.albedoSource == FillPbrTextureSource::Texture)
            ? frame.services.resolveSelectedPbrTexture(item.meshIndex, pbr.albedoIndex, item.fillView) : nullptr;
        QRhiTexture *normal =
            (pbr.normalSource == FillPbrTextureSource::Texture)
            ? frame.services.resolveSelectedPbrTexture(item.meshIndex, pbr.normalIndex, item.fillView) : nullptr;
        QRhiTexture *occlusion =
            (pbr.occlusionSource == FillPbrTextureSource::Texture)
            ? frame.services.resolveSelectedPbrTexture(item.meshIndex, pbr.occlusionIndex, item.fillView) : nullptr;
        QRhiTexture *roughness =
            (pbr.roughnessSource == FillPbrTextureSource::Texture)
            ? frame.services.resolveSelectedPbrTexture(item.meshIndex, pbr.roughnessIndex, item.fillView) : nullptr;
        QRhiTexture *resolvedNormal = normal ? normal : batch.normalTexture;
        PerMeshRenderSettings pbrSettings = item.meshSettings;
        if (pbrSettings.fillPbr.normalSource == FillPbrTextureSource::Texture && !resolvedNormal)
            pbrSettings.fillPbr.normalSource = FillPbrTextureSource::None;

        frame.services.uploadMainUbufForMesh(
            frame.cb,
            item.meshIndex,
            frame.proj,
            frame.view,
            pbrSettings,
            frame.pixelSize,
            true,
            frame.lightDir,
            MainUbufMaterialOverrides {
                pbr.normalScale * batch.normalScale,
                pbr.occlusionStrength * batch.occlusionStrength,
                pbr.roughnessFactor * batch.roughnessFactor });
        frame.cb->setShaderResources(frame.services.shaderResourcesForFillTextures(
            albedo    ? albedo    : batch.baseColorTexture,
            resolvedNormal,
            occlusion ? occlusion : batch.occlusionTexture,
            roughness ? roughness : batch.roughnessTexture));
        drawBatchGeometry(frame.cb, batch);
    }
};

class RenderWidget::RadianceScalingFillRenderer final : public FillMaterialRenderer
{
public:
    void renderPrepass(
        const SceneFillFrameContext &frame,
        const std::vector<SceneFillDrawItem> &drawItems) const override
    {
        const auto itemUsesThisRenderer = [this](const SceneFillDrawItem &item) {
            return item.materialRenderer == this;
        };
        if (std::none_of(drawItems.begin(), drawItems.end(), itemUsesThisRenderer))
            return;

        if (!frame.services.ensureRadianceScalingGradientResources(frame.pixelSize))
            return;

        frame.cb->beginPass(
            frame.services.radianceScalingGradientRenderTarget(),
            QColor(0, 0, 0, 0),
            { 1.0f, 0 },
            nullptr);
        frame.cb->setViewport({
            0,
            0,
            float(frame.pixelSize.width()),
            float(frame.pixelSize.height())
        });
        frame.cb->setGraphicsPipeline(frame.services.radianceScalingGradientPipeline());
        for (const SceneFillDrawItem &item : drawItems) {
            if (!itemUsesThisRenderer(item))
                continue;
            for (int bi = 0; bi < item.fillView.batchCount; ++bi) {
                const auto &batch = item.fillView.batches[bi];
                if (!hasDrawableBatchGeometry(batch))
                    continue;
                frame.services.uploadMainUbufForMesh(
                    frame.cb,
                    item.meshIndex,
                    frame.proj,
                    frame.view,
                    item.meshSettings,
                    frame.pixelSize,
                    true,
                    frame.lightDir,
                    MainUbufMaterialOverrides {
                        item.meshSettings.fillRs.enhancement,
                        1.0f,
                        1.0f });
                frame.cb->setShaderResources(
                    frame.services.radianceScalingGradientShaderResources());
                drawBatchGeometry(frame.cb, batch);
            }
        }
        frame.cb->endPass();
    }

    void drawBatch(
        const SceneFillDrawContext &ctx,
        const MeshGpuResourceCache::FillBatchView &batch) const override
    {
        const SceneFillDrawItem &item = ctx.item;
        const SceneFillFrameContext &frame = ctx.frame;
        frame.services.uploadMainUbufForMesh(
            frame.cb,
            item.meshIndex,
            frame.proj,
            frame.view,
            item.meshSettings,
            frame.pixelSize,
            true,
            frame.lightDir,
            MainUbufMaterialOverrides {
                item.meshSettings.fillRs.enhancement,
                1.0f,
                1.0f });
        QRhiTexture *gradTex = frame.services.radianceScalingGradientTexture();
        frame.cb->setShaderResources(frame.services.shaderResourcesForFillTextures(
            batch.baseColorTexture, gradTex, nullptr, nullptr));
        drawBatchGeometry(frame.cb, batch);
    }
};

RenderWidget::SceneFillFramePlan RenderWidget::buildSceneFillFramePlan(
    const RenderWidget::RenderFrameRequest &request)
{
    static const PlainFillRenderer plainFillRenderer;
    static const PbrFillRenderer pbrFillRenderer;
    static const RadianceScalingFillRenderer radianceScalingFillRenderer;
    auto rendererForMaterial = [&](FillMaterial material) -> const FillMaterialRenderer * {
        switch (material) {
        case FillMaterial::Plain:
            return &plainFillRenderer;
        case FillMaterial::Pbr:
            return &pbrFillRenderer;
        case FillMaterial::RadianceScaling:
            return &radianceScalingFillRenderer;
        }
        return nullptr;
    };

    SceneFillFramePlan plan;
    plan.pixelSize = request.pixelSize;
    plan.proj = request.proj;
    plan.view = request.view;
    plan.lightDir = request.lightDir;
    plan.fillItems.reserve(request.passes.meshes.size());
    for (const RenderMeshPassRequests &meshRequest : request.passes.meshes) {
        if (!meshRequest.fill)
            continue;
        const int mi = meshRequest.meshIndex;
        const PerMeshRenderSettings &meshSettings = meshRequest.meshSettings;
        QRhiGraphicsPipeline *fillPipeline = fillPipelineForSettings(meshSettings);
        if (!fillPipeline)
            continue;
        const auto fillVariant = static_cast<Document::FillGpuVariant>(
            fillGpuVariantIndexForSettings(meshSettings));
        const Document::FillPassGpuView fillView =
            m_doc->fillPassGpuView(m_rhi, mi, fillVariant);
        if (!fillView.valid)
            continue;

        const FillMaterialRenderer *materialRenderer =
            rendererForMaterial(meshSettings.fillMaterial);
        if (!materialRenderer)
            continue;

        plan.fillItems.push_back(SceneFillDrawItem {
            mi,
            fillPipeline,
            materialRenderer,
            meshSettings,
            fillView
        });
    }
    return plan;
}

void RenderWidget::renderSceneFillPrepasses(
    QRhiCommandBuffer *cb,
    const RenderFramePlan &plan)
{
    const SceneFillFramePlan &fillPlan = plan.sceneFill;
    const FillRenderServices services(*this);
    const SceneFillFrameContext frameCtx {
        services,
        cb,
        fillPlan.pixelSize,
        fillPlan.proj,
        fillPlan.view,
        fillPlan.lightDir
    };

    std::vector<const FillMaterialRenderer *> prepassRenderers;
    for (const SceneFillDrawItem &item : fillPlan.fillItems) {
        if (!item.materialRenderer)
            continue;
        if (std::find(prepassRenderers.begin(), prepassRenderers.end(), item.materialRenderer)
            != prepassRenderers.end()) {
            continue;
        }
        prepassRenderers.push_back(item.materialRenderer);
        item.materialRenderer->renderPrepass(frameCtx, fillPlan.fillItems);
    }
}

void RenderWidget::renderSceneFillPass(
    QRhiCommandBuffer *cb,
    const RenderFramePlan &plan)
{
    const SceneFillFramePlan &fillPlan = plan.sceneFill;
    const FillRenderServices services(*this);
    const SceneFillFrameContext frameCtx {
        services,
        cb,
        fillPlan.pixelSize,
        fillPlan.proj,
        fillPlan.view,
        fillPlan.lightDir
    };
    for (const SceneFillDrawItem &item : fillPlan.fillItems) {
        cb->setGraphicsPipeline(item.pipeline);
        const SceneFillDrawContext fillCtx {
            frameCtx,
            item
        };
        for (int bi = 0; bi < item.fillView.batchCount; ++bi) {
            const auto &batch = item.fillView.batches[bi];
            if (!hasDrawableBatchGeometry(batch))
                continue;
            item.materialRenderer->drawBatch(fillCtx, batch);
        }
    }
}
