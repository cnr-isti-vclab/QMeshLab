#include "renderwidget.h"
#include "document.h"
#include "renderwidget_internal.h"

using namespace RenderWidgetInternal;

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
        RenderWidget &widget = ctx.widget;
        widget.uploadMainUbufForMesh(
            ctx.cb,
            ctx.meshIndex,
            ctx.proj,
            ctx.view,
            ctx.meshSettings,
            ctx.pixelSize,
            true,
            ctx.lightDir,
            MainUbufMaterialOverrides {
                ctx.meshSettings.fillPbr.normalScale * batch.normalScale,
                1.0f,
                1.0f });

        // When Texture source is selected, resolve the user-chosen texture by index;
        // fall back to the batch's baked base-colour texture if it cannot be found.
        QRhiTexture *albedo = batch.baseColorTexture;
        if (ctx.meshSettings.fillPlain.colorSource == FillColorSource::Texture) {
            if (QRhiTexture *t = widget.resolveSelectedPbrTexture(
                    ctx.meshIndex,
                    ctx.meshSettings.fillPlain.textureIndex,
                    ctx.fillView)) {
                albedo = t;
            }
        }
        ctx.cb->setShaderResources(
            widget.shaderResourcesForFillTextures(albedo, nullptr, nullptr, nullptr));
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
        RenderWidget &widget = ctx.widget;
        const auto &pbr = ctx.meshSettings.fillPbr;
        QRhiTexture *albedo =
            (pbr.albedoSource == FillPbrTextureSource::Texture)
            ? widget.resolveSelectedPbrTexture(ctx.meshIndex, pbr.albedoIndex, ctx.fillView) : nullptr;
        QRhiTexture *normal =
            (pbr.normalSource == FillPbrTextureSource::Texture)
            ? widget.resolveSelectedPbrTexture(ctx.meshIndex, pbr.normalIndex, ctx.fillView) : nullptr;
        QRhiTexture *occlusion =
            (pbr.occlusionSource == FillPbrTextureSource::Texture)
            ? widget.resolveSelectedPbrTexture(ctx.meshIndex, pbr.occlusionIndex, ctx.fillView) : nullptr;
        QRhiTexture *roughness =
            (pbr.roughnessSource == FillPbrTextureSource::Texture)
            ? widget.resolveSelectedPbrTexture(ctx.meshIndex, pbr.roughnessIndex, ctx.fillView) : nullptr;
        QRhiTexture *resolvedNormal = normal ? normal : batch.normalTexture;
        PerMeshRenderSettings pbrSettings = ctx.meshSettings;
        if (pbrSettings.fillPbr.normalSource == FillPbrTextureSource::Texture && !resolvedNormal)
            pbrSettings.fillPbr.normalSource = FillPbrTextureSource::None;

        widget.uploadMainUbufForMesh(
            ctx.cb,
            ctx.meshIndex,
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
        ctx.cb->setShaderResources(widget.shaderResourcesForFillTextures(
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
        RenderWidget &widget = ctx.widget;
        widget.uploadMainUbufForMesh(
            ctx.cb,
            ctx.meshIndex,
            ctx.proj,
            ctx.view,
            ctx.meshSettings,
            ctx.pixelSize,
            true,
            ctx.lightDir,
            MainUbufMaterialOverrides {
                ctx.meshSettings.fillRs.enhancement,
                1.0f,
                1.0f });
        QRhiTexture *gradTex = widget.m_rsGradTexture ? widget.m_rsGradTexture.get() : nullptr;
        ctx.cb->setShaderResources(widget.shaderResourcesForFillTextures(
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

        static const PlainFillRenderer plainFillRenderer;
        static const PbrFillRenderer pbrFillRenderer;
        static const RadianceScalingFillRenderer radianceScalingFillRenderer;
        const FillMaterialRenderer *materialRenderer = nullptr;
        switch (meshSettings.fillMaterial) {
        case FillMaterial::Plain:
            materialRenderer = &plainFillRenderer;
            break;
        case FillMaterial::Pbr:
            materialRenderer = &pbrFillRenderer;
            break;
        case FillMaterial::RadianceScaling:
            materialRenderer = &radianceScalingFillRenderer;
            break;
        }
        if (!materialRenderer)
            continue;

        const SceneFillDrawContext fillCtx {
            *this,
            cb,
            mi,
            proj,
            view,
            pixelSize,
            lightDir,
            meshSettings,
            fillView
        };
        for (int bi = 0; bi < fillView.batchCount; ++bi) {
            const auto &batch = fillView.batches[bi];
            if (!hasDrawableBatchGeometry(batch))
                continue;
            materialRenderer->drawBatch(fillCtx, batch);
        }
    }
}
