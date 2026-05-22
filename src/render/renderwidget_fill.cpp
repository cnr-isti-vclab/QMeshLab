#include "renderwidget.h"
#include "document.h"
#include "renderwidget_internal.h"
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

private:
    RenderWidget &m_widget;
};

class RenderWidget::FillMaterialRenderer
{
public:
    virtual ~FillMaterialRenderer() = default;

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
        ctx.services.uploadMainUbufForMesh(
            ctx.cb,
            item.meshIndex,
            ctx.proj,
            ctx.view,
            item.meshSettings,
            ctx.pixelSize,
            true,
            ctx.lightDir,
            MainUbufMaterialOverrides {
                item.meshSettings.fillPbr.normalScale * batch.normalScale,
                1.0f,
                1.0f });

        // When Texture source is selected, resolve the user-chosen texture by index;
        // fall back to the batch's baked base-colour texture if it cannot be found.
        QRhiTexture *albedo = batch.baseColorTexture;
        if (item.meshSettings.fillPlain.colorSource == FillColorSource::Texture) {
            if (QRhiTexture *t = ctx.services.resolveSelectedPbrTexture(
                    item.meshIndex,
                    item.meshSettings.fillPlain.textureIndex,
                    item.fillView)) {
                albedo = t;
            }
        }
        ctx.cb->setShaderResources(
            ctx.services.shaderResourcesForFillTextures(albedo, nullptr, nullptr, nullptr));
        drawBatchGeometry(ctx.cb, batch);
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
        const auto &pbr = item.meshSettings.fillPbr;
        QRhiTexture *albedo =
            (pbr.albedoSource == FillPbrTextureSource::Texture)
            ? ctx.services.resolveSelectedPbrTexture(item.meshIndex, pbr.albedoIndex, item.fillView) : nullptr;
        QRhiTexture *normal =
            (pbr.normalSource == FillPbrTextureSource::Texture)
            ? ctx.services.resolveSelectedPbrTexture(item.meshIndex, pbr.normalIndex, item.fillView) : nullptr;
        QRhiTexture *occlusion =
            (pbr.occlusionSource == FillPbrTextureSource::Texture)
            ? ctx.services.resolveSelectedPbrTexture(item.meshIndex, pbr.occlusionIndex, item.fillView) : nullptr;
        QRhiTexture *roughness =
            (pbr.roughnessSource == FillPbrTextureSource::Texture)
            ? ctx.services.resolveSelectedPbrTexture(item.meshIndex, pbr.roughnessIndex, item.fillView) : nullptr;
        QRhiTexture *resolvedNormal = normal ? normal : batch.normalTexture;
        PerMeshRenderSettings pbrSettings = item.meshSettings;
        if (pbrSettings.fillPbr.normalSource == FillPbrTextureSource::Texture && !resolvedNormal)
            pbrSettings.fillPbr.normalSource = FillPbrTextureSource::None;

        ctx.services.uploadMainUbufForMesh(
            ctx.cb,
            item.meshIndex,
            ctx.proj,
            ctx.view,
            pbrSettings,
            ctx.pixelSize,
            true,
            ctx.lightDir,
            MainUbufMaterialOverrides {
                pbr.normalScale * batch.normalScale,
                pbr.occlusionStrength * batch.occlusionStrength,
                pbr.roughnessFactor * batch.roughnessFactor });
        ctx.cb->setShaderResources(ctx.services.shaderResourcesForFillTextures(
            albedo    ? albedo    : batch.baseColorTexture,
            resolvedNormal,
            occlusion ? occlusion : batch.occlusionTexture,
            roughness ? roughness : batch.roughnessTexture));
        drawBatchGeometry(ctx.cb, batch);
    }
};

class RenderWidget::RadianceScalingFillRenderer final : public FillMaterialRenderer
{
public:
    void drawBatch(
        const SceneFillDrawContext &ctx,
        const MeshGpuResourceCache::FillBatchView &batch) const override
    {
        const SceneFillDrawItem &item = ctx.item;
        ctx.services.uploadMainUbufForMesh(
            ctx.cb,
            item.meshIndex,
            ctx.proj,
            ctx.view,
            item.meshSettings,
            ctx.pixelSize,
            true,
            ctx.lightDir,
            MainUbufMaterialOverrides {
                item.meshSettings.fillRs.enhancement,
                1.0f,
                1.0f });
        QRhiTexture *gradTex = ctx.services.radianceScalingGradientTexture();
        ctx.cb->setShaderResources(ctx.services.shaderResourcesForFillTextures(
            batch.baseColorTexture, gradTex, nullptr, nullptr));
        drawBatchGeometry(ctx.cb, batch);
    }
};

void RenderWidget::renderRadianceScalingGradientPass(
    QRhiCommandBuffer *cb,
    const QSize &pixelSize,
    const QMatrix4x4 &proj,
    const QMatrix4x4 &view,
    const QVector3D &lightDir)
{
    // Radiance Scaling needs a first pass into a floating-point gradient texture
    // before the normal fill pass can sample its neighbourhood.
    bool anyRsMesh = false;
    for (int mi = 0; mi < m_doc->meshCount() && !anyRsMesh; ++mi) {
        if (!meshVisible(mi))
            continue;
        const PerMeshRenderSettings ms = renderModeForMesh(mi);
        if (ms.showFill && ms.fillMaterial == FillMaterial::RadianceScaling)
            anyRsMesh = true;
    }
    if (!anyRsMesh)
        return;

    ensureRsGradResources(pixelSize);
    if (!m_rsGradRt || !m_rsGradPipeline || !m_rsGradSrb)
        return;

    cb->beginPass(m_rsGradRt.get(), QColor(0, 0, 0, 0), { 1.0f, 0 }, nullptr);
    cb->setViewport({ 0, 0, float(pixelSize.width()), float(pixelSize.height()) });
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
            if (!hasDrawableBatchGeometry(batch))
                continue;
            uploadMainUbufForMesh(
                cb,
                mi,
                proj,
                view,
                meshSettings,
                pixelSize,
                true,
                lightDir,
                MainUbufMaterialOverrides {
                    meshSettings.fillRs.enhancement,
                    1.0f,
                    1.0f });
            cb->setShaderResources(m_rsGradSrb.get());
            drawBatchGeometry(cb, batch);
        }
    }
    cb->endPass();
}

void RenderWidget::renderSceneFillPass(
    QRhiCommandBuffer *cb,
    const QSize &pixelSize,
    const QMatrix4x4 &proj,
    const QMatrix4x4 &view,
    const QVector3D &lightDir)
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

    std::vector<SceneFillDrawItem> drawItems;
    drawItems.reserve(m_doc->meshCount());
    for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
        if (!meshVisible(mi))
            continue;
        const PerMeshRenderSettings meshSettings = renderModeForMesh(mi);
        if (!meshSettings.showFill)
            continue;
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

        drawItems.push_back(SceneFillDrawItem {
            mi,
            fillPipeline,
            materialRenderer,
            meshSettings,
            fillView
        });
    }

    const FillRenderServices services(*this);
    for (const SceneFillDrawItem &item : drawItems) {
        cb->setGraphicsPipeline(item.pipeline);
        const SceneFillDrawContext fillCtx {
            services,
            cb,
            proj,
            view,
            pixelSize,
            lightDir,
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
