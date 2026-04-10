#include "renderwidget.h"
#include "document.h"
#include "meshgpuresourcecache.h"
#include "renderwidget_internal.h"
#include <QElapsedTimer>
#include <QImage>

using namespace RenderWidgetInternal;

void RenderWidget::updateCameraFrameIfNeeded()
{
    if (!m_reframeCameraRequested)
        return;
    if (m_doc->meshCount() == 0)
        return;

    vcg::Box3f bbox;
    bool hasVisibleMesh = false;
    for (int i = 0; i < m_doc->meshCount(); ++i) {
        if (!meshVisible(i))
            continue;
        bbox.Add(m_doc->mesh(i).mesh.bbox);
        hasVisibleMesh = true;
    }
    if (!hasVisibleMesh)
        return;
    if (bbox.IsNull())
        return;

    auto c = bbox.Center();
    const QVector3D center(c[0], c[1], c[2]);
    const float radius = qMax(1e-4f, bbox.Diag() / 2.0f);
    if (m_resetTrackballRequested)
        m_trackball.resetToFrame(center, radius, radius * 3.0f);
    else
        m_trackball.setFrame(center, radius, radius * 3.0f);
    m_reframeCameraRequested = false;
    m_resetTrackballRequested = false;
    m_centerAnimActive = false;
}

void RenderWidget::ensureCurrentMeshMaskResources(const QSize &pixelSize)
{
    if (!m_rhi || pixelSize.isEmpty())
        return;

    if (m_currentMaskRt && m_currentMaskSize == pixelSize)
        return;

    m_currentMaskFillPipeline.reset();
    m_currentMaskFillDepthOnlyPipeline.reset();
    m_currentMaskEdgesPipeline.reset();
    m_currentMaskEdgesDepthPipeline.reset();
    m_currentMaskEdgesDepthOnlyPipeline.reset();
    m_currentMaskPointsPipeline.reset();
    m_currentMaskPointsDepthOnlyPipeline.reset();
    m_maskMorphMaskToBaseSrb.reset();
    m_maskMorphMaskToWorkSrb.reset();
    m_maskMorphWorkToMaskSrb.reset();
    m_maskMorphToBasePipeline.reset();
    m_maskMorphToWorkPipeline.reset();
    m_maskMorphWorkToMaskPipeline.reset();
    m_outlineExtractUbuf.reset();
    m_outlineExtractSrb.reset();
    m_outlineExtractPipeline.reset();
    m_maskDebugBaseSrb.reset();
    m_maskDebugWorkSrb.reset();
    m_maskDebugMaskSrb.reset();
    m_maskDebugPipeline.reset();
    m_outlineSrb.reset();
    m_outlinePipeline.reset();
    m_currentMaskBaseRt.reset();
    m_currentMaskBaseRp.reset();
    m_currentMaskBaseTexture.reset();
    m_currentMaskRt.reset();
    m_currentMaskRp.reset();
    m_currentMaskDepth.reset();
    m_currentMaskTexture.reset();
    m_currentMaskWorkRt.reset();
    m_currentMaskWorkRp.reset();
    m_currentMaskWorkTexture.reset();

    m_currentMaskTexture.reset(
        m_rhi->newTexture(
            QRhiTexture::RGBA8,
            pixelSize,
            1,
            QRhiTexture::RenderTarget));
    if (!m_currentMaskTexture || !m_currentMaskTexture->create()) {
        m_currentMaskTexture.reset();
        m_currentMaskSize = QSize();
        return;
    }

    m_currentMaskDepth.reset(m_rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, pixelSize, 1));
    if (!m_currentMaskDepth || !m_currentMaskDepth->create()) {
        m_currentMaskDepth.reset();
        m_currentMaskTexture.reset();
        m_currentMaskSize = QSize();
        return;
    }

    QRhiTextureRenderTargetDescription rtDesc(QRhiColorAttachment(m_currentMaskTexture.get()));
    rtDesc.setDepthStencilBuffer(m_currentMaskDepth.get());
    m_currentMaskRt.reset(m_rhi->newTextureRenderTarget(rtDesc));
    if (!m_currentMaskRt) {
        m_currentMaskDepth.reset();
        m_currentMaskTexture.reset();
        m_currentMaskSize = QSize();
        return;
    }

    m_currentMaskRp.reset(m_currentMaskRt->newCompatibleRenderPassDescriptor());
    m_currentMaskRt->setRenderPassDescriptor(m_currentMaskRp.get());
    if (!m_currentMaskRt->create()) {
        m_currentMaskRp.reset();
        m_currentMaskRt.reset();
        m_currentMaskDepth.reset();
        m_currentMaskTexture.reset();
        m_currentMaskSize = QSize();
        return;
    }

    m_currentMaskBaseTexture.reset(
        m_rhi->newTexture(
            QRhiTexture::RGBA8,
            pixelSize,
            1,
            QRhiTexture::RenderTarget));
    if (!m_currentMaskBaseTexture || !m_currentMaskBaseTexture->create()) {
        m_currentMaskBaseTexture.reset();
        m_currentMaskRp.reset();
        m_currentMaskRt.reset();
        m_currentMaskDepth.reset();
        m_currentMaskTexture.reset();
        m_currentMaskSize = QSize();
        return;
    }

    QRhiTextureRenderTargetDescription baseRtDesc(QRhiColorAttachment(m_currentMaskBaseTexture.get()));
    baseRtDesc.setDepthStencilBuffer(m_currentMaskDepth.get());
    m_currentMaskBaseRt.reset(m_rhi->newTextureRenderTarget(baseRtDesc));
    if (!m_currentMaskBaseRt) {
        m_currentMaskBaseTexture.reset();
        m_currentMaskRp.reset();
        m_currentMaskRt.reset();
        m_currentMaskDepth.reset();
        m_currentMaskTexture.reset();
        m_currentMaskSize = QSize();
        return;
    }

    m_currentMaskBaseRp.reset(m_currentMaskBaseRt->newCompatibleRenderPassDescriptor());
    m_currentMaskBaseRt->setRenderPassDescriptor(m_currentMaskBaseRp.get());
    if (!m_currentMaskBaseRt->create()) {
        m_currentMaskBaseRp.reset();
        m_currentMaskBaseRt.reset();
        m_currentMaskBaseTexture.reset();
        m_currentMaskRp.reset();
        m_currentMaskRt.reset();
        m_currentMaskDepth.reset();
        m_currentMaskTexture.reset();
        m_currentMaskSize = QSize();
        return;
    }

    m_currentMaskWorkTexture.reset(
        m_rhi->newTexture(
            QRhiTexture::RGBA8,
            pixelSize,
            1,
            QRhiTexture::RenderTarget));
    if (!m_currentMaskWorkTexture || !m_currentMaskWorkTexture->create()) {
        m_currentMaskWorkTexture.reset();
        m_currentMaskBaseRp.reset();
        m_currentMaskBaseRt.reset();
        m_currentMaskBaseTexture.reset();
        m_currentMaskRp.reset();
        m_currentMaskRt.reset();
        m_currentMaskDepth.reset();
        m_currentMaskTexture.reset();
        m_currentMaskSize = QSize();
        return;
    }

    QRhiTextureRenderTargetDescription workRtDesc(QRhiColorAttachment(m_currentMaskWorkTexture.get()));
    m_currentMaskWorkRt.reset(m_rhi->newTextureRenderTarget(workRtDesc));
    if (!m_currentMaskWorkRt) {
        m_currentMaskWorkTexture.reset();
        m_currentMaskBaseRp.reset();
        m_currentMaskBaseRt.reset();
        m_currentMaskBaseTexture.reset();
        m_currentMaskRp.reset();
        m_currentMaskRt.reset();
        m_currentMaskDepth.reset();
        m_currentMaskTexture.reset();
        m_currentMaskSize = QSize();
        return;
    }

    m_currentMaskWorkRp.reset(m_currentMaskWorkRt->newCompatibleRenderPassDescriptor());
    m_currentMaskWorkRt->setRenderPassDescriptor(m_currentMaskWorkRp.get());
    if (!m_currentMaskWorkRt->create()) {
        m_currentMaskWorkRp.reset();
        m_currentMaskWorkRt.reset();
        m_currentMaskWorkTexture.reset();
        m_currentMaskBaseRp.reset();
        m_currentMaskBaseRt.reset();
        m_currentMaskBaseTexture.reset();
        m_currentMaskRp.reset();
        m_currentMaskRt.reset();
        m_currentMaskDepth.reset();
        m_currentMaskTexture.reset();
        m_currentMaskSize = QSize();
        return;
    }

    m_currentMaskSize = pixelSize;
}

void RenderWidget::ensureDepthPickResources(const QSize &pixelSize)
{
    if (!m_rhi || pixelSize.isEmpty())
        return;

    if (m_depthPickRt && m_depthPickSize == pixelSize)
        return;

    m_depthPickFillPipeline.reset();
    m_depthPickPointsPipeline.reset();
    m_depthPickSrb.reset();
    m_depthPickRp.reset();
    m_depthPickRt.reset();
    m_depthPickDepth.reset();
    m_depthPickTexture.reset();

    m_depthPickTexture.reset(
        m_rhi->newTexture(
            QRhiTexture::RGBA8,
            pixelSize,
            1,
            QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    if (!m_depthPickTexture || !m_depthPickTexture->create()) {
        m_depthPickTexture.reset();
        m_depthPickSize = QSize();
        return;
    }

    m_depthPickDepth.reset(m_rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, pixelSize, 1));
    if (!m_depthPickDepth || !m_depthPickDepth->create()) {
        m_depthPickDepth.reset();
        m_depthPickTexture.reset();
        m_depthPickSize = QSize();
        return;
    }

    QRhiTextureRenderTargetDescription rtDesc(QRhiColorAttachment(m_depthPickTexture.get()));
    rtDesc.setDepthStencilBuffer(m_depthPickDepth.get());
    m_depthPickRt.reset(m_rhi->newTextureRenderTarget(rtDesc));
    if (!m_depthPickRt) {
        m_depthPickDepth.reset();
        m_depthPickTexture.reset();
        m_depthPickSize = QSize();
        return;
    }

    m_depthPickRp.reset(m_depthPickRt->newCompatibleRenderPassDescriptor());
    m_depthPickRt->setRenderPassDescriptor(m_depthPickRp.get());
    if (!m_depthPickRt->create()) {
        m_depthPickRp.reset();
        m_depthPickRt.reset();
        m_depthPickDepth.reset();
        m_depthPickTexture.reset();
        m_depthPickSize = QSize();
        return;
    }

    if (!m_depthPickSrb && m_ubuf) {
        m_depthPickSrb.reset(m_rhi->newShaderResourceBindings());
        m_depthPickSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::VertexStage,
                m_ubuf.get())
        });
        if (!m_depthPickSrb->create())
            m_depthPickSrb.reset();
    }

    if (m_depthPickSrb && !m_depthPickFillPipeline) {
        m_depthPickFillPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/depth_pick.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/depth_pick.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load depth-pick fill shaders");
            m_depthPickFillPipeline.reset();
        } else {
            m_depthPickFillPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_depthPickFillPipeline->setDepthTest(true);
            m_depthPickFillPipeline->setDepthWrite(true);
            m_depthPickFillPipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiVertexInputLayout layout;
            layout.setBindings({ { kFillVertexStrideFloats * sizeof(float) } });
            layout.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, 0 } });
            m_depthPickFillPipeline->setVertexInputLayout(layout);
            m_depthPickFillPipeline->setShaderResourceBindings(m_depthPickSrb.get());
            m_depthPickFillPipeline->setRenderPassDescriptor(m_depthPickRp.get());
            if (!m_depthPickFillPipeline->create()) {
                qWarning("Failed to create depth-pick fill pipeline");
                m_depthPickFillPipeline.reset();
            }
        }
    }

    if (m_depthPickSrb && !m_depthPickPointsPipeline) {
        m_depthPickPointsPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/depth_pick.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/depth_pick.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load depth-pick points shaders");
            m_depthPickPointsPipeline.reset();
        } else {
            m_depthPickPointsPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_depthPickPointsPipeline->setTopology(QRhiGraphicsPipeline::Points);
            m_depthPickPointsPipeline->setDepthTest(true);
            m_depthPickPointsPipeline->setDepthWrite(true);
            m_depthPickPointsPipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiVertexInputLayout layout;
            layout.setBindings({ { kPointsVertexStrideFloats * sizeof(float) } });
            layout.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, 0 } });
            m_depthPickPointsPipeline->setVertexInputLayout(layout);
            m_depthPickPointsPipeline->setShaderResourceBindings(m_depthPickSrb.get());
            m_depthPickPointsPipeline->setRenderPassDescriptor(m_depthPickRp.get());
            if (!m_depthPickPointsPipeline->create()) {
                qWarning("Failed to create depth-pick points pipeline");
                m_depthPickPointsPipeline.reset();
            }
        }
    }

    m_depthPickSize = pixelSize;
}

void RenderWidget::ensureRenderResources()
{
    if (m_rhi != rhi()) {
        if (m_rhi)
            m_doc->releaseRhiGpuResources(m_rhi);
        m_rhi = rhi();
        m_fillPipeline.reset();
        m_wirePipeline.reset();
        m_edgesPipeline.reset();
        m_fillPipelinesByKey.clear();
        m_wirePipelinesByKey.clear();
        m_edgesPipelinesByKey.clear();
        m_fatEdgesPipelinesByKey.clear();
        m_bboxPipeline.reset();
        m_pointsPipeline.reset();
        m_decoratorPipeline.reset();
        m_decoratorFatUbuf.reset();
        m_decoratorFatSrb.reset();
        m_decoratorFatPipeline.reset();
        for (auto &srb : m_decoratorSrbs)
            srb.reset();
        for (auto &ubuf : m_decoratorUbufs)
            ubuf.reset();
        m_depthPickTexture.reset();
        m_depthPickDepth.reset();
        m_depthPickRt.reset();
        m_depthPickRp.reset();
        m_depthPickSize = QSize();
        m_depthPickSrb.reset();
        m_depthPickFillPipeline.reset();
        m_depthPickPointsPipeline.reset();
        m_depthPickPending = false;
        m_depthPickInFlight = false;
        m_depthPickReadbackResult.reset();
        m_currentMaskBaseTexture.reset();
        m_currentMaskBaseRt.reset();
        m_currentMaskBaseRp.reset();
        m_currentMaskTexture.reset();
        m_currentMaskDepth.reset();
        m_currentMaskRt.reset();
        m_currentMaskRp.reset();
        m_currentMaskWorkTexture.reset();
        m_currentMaskWorkRt.reset();
        m_currentMaskWorkRp.reset();
        m_currentMaskSize = QSize();
        m_currentMaskFillPipeline.reset();
        m_currentMaskFillDepthOnlyPipeline.reset();
        m_currentMaskEdgesPipeline.reset();
        m_currentMaskEdgesDepthPipeline.reset();
        m_currentMaskEdgesDepthOnlyPipeline.reset();
        m_currentMaskPointsPipeline.reset();
        m_currentMaskPointsDepthOnlyPipeline.reset();
        m_maskMorphCopyUbuf.reset();
        m_maskMorphDilateUbuf.reset();
        m_maskMorphErodeUbuf.reset();
        m_maskMorphSampler.reset();
        m_maskMorphMaskToBaseSrb.reset();
        m_maskMorphMaskToWorkSrb.reset();
        m_maskMorphWorkToMaskSrb.reset();
        m_maskMorphToBasePipeline.reset();
        m_maskMorphToWorkPipeline.reset();
        m_maskMorphWorkToMaskPipeline.reset();
        m_outlineExtractUbuf.reset();
        m_outlineExtractSrb.reset();
        m_outlineExtractPipeline.reset();
        m_maskDebugUbuf.reset();
        m_maskDebugBaseSrb.reset();
        m_maskDebugWorkSrb.reset();
        m_maskDebugMaskSrb.reset();
        m_maskDebugPipeline.reset();
        m_outlineUbuf.reset();
        m_outlineSampler.reset();
        m_outlineSrb.reset();
        m_outlinePipeline.reset();
        m_trackballGizmoUbuf.reset();
        m_trackballGizmoVbuf.reset();
        m_trackballGizmoSrb.reset();
        m_trackballGizmoPipeline.reset();
        m_trackballGizmoVertexCount = 0;
        m_srb.reset();
        m_ubuf.reset();
        m_textureSampler.reset();
        m_fallbackTexture.reset();
        m_fallbackTextureUploadPending = false;
        m_textureSrbs.clear();
        m_uvBackgroundUbuf.reset();
        m_uvBackgroundSrb.reset();
        m_uvBackgroundPipeline.reset();
        m_uvTextureFillPipeline.reset();
        m_uvUnitBoxVbuf.reset();
        m_uvUnitBoxVertexCount = 0;
        m_uvMeshGpu.clear();
    }

    if (!m_rhi || !renderTarget())
        return;

    if (!m_ubuf) {
        // Uniform buffer: mvp + modelView + normalMat + bbox/point/wire/fill parameters
        m_ubuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUbufSize));
        if (!m_ubuf || !m_ubuf->create()) {
            m_ubuf.reset();
            return;
        }
    }

    if (!m_textureSampler) {
        m_textureSampler.reset(
            m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                              QRhiSampler::Repeat, QRhiSampler::Repeat));
        if (!m_textureSampler || !m_textureSampler->create()) {
            m_textureSampler.reset();
            return;
        }
    }

    if (!m_fallbackTexture) {
        m_fallbackTexture.reset(
            m_rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1), 1));
        if (m_fallbackTexture && m_fallbackTexture->create()) {
            m_fallbackTextureUploadPending = true;
        } else {
            m_fallbackTexture.reset();
            m_fallbackTextureUploadPending = false;
        }
    }

    ensureCurrentMeshMaskResources(renderTarget()->pixelSize());

    if (!m_srb) {
        if (!m_ubuf || !m_textureSampler || !m_fallbackTexture)
            return;
        m_srb.reset(m_rhi->newShaderResourceBindings());
        m_srb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                m_ubuf.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1,
                QRhiShaderResourceBinding::FragmentStage,
                m_fallbackTexture.get(),
                m_textureSampler.get())
        });
        if (!m_srb->create()) {
            m_srb.reset();
            return;
        }
    }

    for (int slot = 0; slot < kDecoratorSlotCount; ++slot) {
        if (!m_decoratorUbufs[slot]) {
            m_decoratorUbufs[slot].reset(
                m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kDecoratorUbufSize));
            m_decoratorUbufs[slot]->create();
        }
        if (!m_decoratorSrbs[slot] && m_decoratorUbufs[slot]) {
            m_decoratorSrbs[slot].reset(m_rhi->newShaderResourceBindings());
            m_decoratorSrbs[slot]->setBindings({
                QRhiShaderResourceBinding::uniformBuffer(
                    0,
                    QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                    m_decoratorUbufs[slot].get())
            });
            m_decoratorSrbs[slot]->create();
        }
    }

    if (!m_outlineUbuf) {
        m_outlineUbuf.reset(
            m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kOutlineUbufSize));
        m_outlineUbuf->create();
    }
    if (!m_outlineSampler) {
        m_outlineSampler.reset(
            m_rhi->newSampler(
                QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        m_outlineSampler->create();
    }
    if (!m_maskMorphCopyUbuf) {
        m_maskMorphCopyUbuf.reset(
            m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kMaskMorphUbufSize));
        m_maskMorphCopyUbuf->create();
    }
    if (!m_maskMorphDilateUbuf) {
        m_maskMorphDilateUbuf.reset(
            m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kMaskMorphUbufSize));
        m_maskMorphDilateUbuf->create();
    }
    if (!m_maskMorphErodeUbuf) {
        m_maskMorphErodeUbuf.reset(
            m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kMaskMorphUbufSize));
        m_maskMorphErodeUbuf->create();
    }
    if (!m_maskMorphSampler) {
        m_maskMorphSampler.reset(
            m_rhi->newSampler(
                QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        m_maskMorphSampler->create();
    }
    if (!m_maskDebugUbuf) {
        m_maskDebugUbuf.reset(
            m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kMaskDebugUbufSize));
        m_maskDebugUbuf->create();
    }
    if (!m_maskMorphMaskToBaseSrb
        && m_maskMorphCopyUbuf
        && m_maskMorphSampler
        && m_currentMaskTexture
        && m_currentMaskBaseTexture) {
        m_maskMorphMaskToBaseSrb.reset(m_rhi->newShaderResourceBindings());
        m_maskMorphMaskToBaseSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::FragmentStage,
                m_maskMorphCopyUbuf.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1,
                QRhiShaderResourceBinding::FragmentStage,
                m_currentMaskTexture.get(),
                m_maskMorphSampler.get())
        });
        m_maskMorphMaskToBaseSrb->create();
    }
    if (!m_maskMorphMaskToWorkSrb
        && m_maskMorphDilateUbuf
        && m_maskMorphSampler
        && m_currentMaskBaseTexture
        && m_currentMaskWorkTexture) {
        m_maskMorphMaskToWorkSrb.reset(m_rhi->newShaderResourceBindings());
        m_maskMorphMaskToWorkSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::FragmentStage,
                m_maskMorphDilateUbuf.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1,
                QRhiShaderResourceBinding::FragmentStage,
                m_currentMaskBaseTexture.get(),
                m_maskMorphSampler.get())
        });
        m_maskMorphMaskToWorkSrb->create();
    }
    if (!m_maskMorphWorkToMaskSrb
        && m_maskMorphErodeUbuf
        && m_maskMorphSampler
        && m_currentMaskTexture
        && m_currentMaskWorkTexture) {
        m_maskMorphWorkToMaskSrb.reset(m_rhi->newShaderResourceBindings());
        m_maskMorphWorkToMaskSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::FragmentStage,
                m_maskMorphErodeUbuf.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1,
                QRhiShaderResourceBinding::FragmentStage,
                m_currentMaskWorkTexture.get(),
                m_maskMorphSampler.get())
        });
        m_maskMorphWorkToMaskSrb->create();
    }
    if (!m_outlineExtractUbuf) {
        m_outlineExtractUbuf.reset(
            m_rhi->newBuffer(
                QRhiBuffer::Dynamic,
                QRhiBuffer::UniformBuffer,
                kOutlineExtractUbufSize));
        m_outlineExtractUbuf->create();
    }
    if (!m_outlineExtractSrb
        && m_outlineExtractUbuf
        && m_maskMorphSampler
        && m_currentMaskBaseTexture) {
        m_outlineExtractSrb.reset(m_rhi->newShaderResourceBindings());
        m_outlineExtractSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::FragmentStage,
                m_outlineExtractUbuf.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1,
                QRhiShaderResourceBinding::FragmentStage,
                m_currentMaskBaseTexture.get(),
                m_maskMorphSampler.get())
        });
        m_outlineExtractSrb->create();
    }
    if (!m_outlineExtractPipeline && m_outlineExtractSrb && m_currentMaskWorkRp) {
        m_outlineExtractPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/selection_outline.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/selection_outline_extract.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load outline-extract shaders");
            m_outlineExtractPipeline.reset();
        } else {
            m_outlineExtractPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_outlineExtractPipeline->setDepthTest(false);
            m_outlineExtractPipeline->setDepthWrite(false);
            m_outlineExtractPipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiVertexInputLayout layout;
            m_outlineExtractPipeline->setVertexInputLayout(layout);
            m_outlineExtractPipeline->setShaderResourceBindings(m_outlineExtractSrb.get());
            m_outlineExtractPipeline->setRenderPassDescriptor(m_currentMaskWorkRp.get());
            if (!m_outlineExtractPipeline->create()) {
                qWarning("Failed to create outline-extract pipeline");
                m_outlineExtractPipeline.reset();
            }
        }
    }
    if (!m_maskDebugBaseSrb
        && m_maskDebugUbuf
        && m_maskMorphSampler
        && m_currentMaskBaseTexture
        && m_currentMaskTexture) {
        m_maskDebugBaseSrb.reset(m_rhi->newShaderResourceBindings());
        m_maskDebugBaseSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::FragmentStage,
                m_maskDebugUbuf.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1,
                QRhiShaderResourceBinding::FragmentStage,
                m_currentMaskBaseTexture.get(),
                m_maskMorphSampler.get()),
            QRhiShaderResourceBinding::sampledTexture(
                2,
                QRhiShaderResourceBinding::FragmentStage,
                m_currentMaskTexture.get(),
                m_maskMorphSampler.get())
        });
        m_maskDebugBaseSrb->create();
    }
    if (!m_maskDebugWorkSrb
        && m_maskDebugUbuf
        && m_maskMorphSampler
        && m_currentMaskWorkTexture
        && m_currentMaskTexture) {
        m_maskDebugWorkSrb.reset(m_rhi->newShaderResourceBindings());
        m_maskDebugWorkSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::FragmentStage,
                m_maskDebugUbuf.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1,
                QRhiShaderResourceBinding::FragmentStage,
                m_currentMaskWorkTexture.get(),
                m_maskMorphSampler.get()),
            QRhiShaderResourceBinding::sampledTexture(
                2,
                QRhiShaderResourceBinding::FragmentStage,
                m_currentMaskTexture.get(),
                m_maskMorphSampler.get())
        });
        m_maskDebugWorkSrb->create();
    }
    if (!m_maskDebugMaskSrb
        && m_maskDebugUbuf
        && m_maskMorphSampler
        && m_currentMaskTexture
        && m_currentMaskBaseTexture) {
        m_maskDebugMaskSrb.reset(m_rhi->newShaderResourceBindings());
        m_maskDebugMaskSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::FragmentStage,
                m_maskDebugUbuf.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1,
                QRhiShaderResourceBinding::FragmentStage,
                m_currentMaskTexture.get(),
                m_maskMorphSampler.get()),
            QRhiShaderResourceBinding::sampledTexture(
                2,
                QRhiShaderResourceBinding::FragmentStage,
                m_currentMaskBaseTexture.get(),
                m_maskMorphSampler.get())
        });
        m_maskDebugMaskSrb->create();
    }
    if (!m_outlineSrb
        && m_outlineUbuf
        && m_outlineSampler
        && m_currentMaskWorkTexture
        && m_currentMaskBaseTexture
        && m_currentMaskTexture) {
        m_outlineSrb.reset(m_rhi->newShaderResourceBindings());
        m_outlineSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::FragmentStage,
                m_outlineUbuf.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1,
                QRhiShaderResourceBinding::FragmentStage,
                m_currentMaskWorkTexture.get(),
                m_outlineSampler.get()),
            QRhiShaderResourceBinding::sampledTexture(
                2,
                QRhiShaderResourceBinding::FragmentStage,
                m_currentMaskBaseTexture.get(),
                m_outlineSampler.get()),
            QRhiShaderResourceBinding::sampledTexture(
                3,
                QRhiShaderResourceBinding::FragmentStage,
                m_currentMaskTexture.get(),
                m_outlineSampler.get())
        });
        m_outlineSrb->create();
    }

    if (!m_fillPipeline) {
        m_fillPipeline.reset(m_rhi->newGraphicsPipeline());

        QString vsPath;
        QString fsPath;
        switch (m_renderSettings.fillShading) {
        case FillShading::Smooth:
            vsPath = QStringLiteral(":/shaders/fill_smooth.vert.qsb");
            fsPath = QStringLiteral(":/shaders/fill_smooth.frag.qsb");
            break;
        case FillShading::Flat:
            vsPath = QStringLiteral(":/shaders/fill_flat.vert.qsb");
            fsPath = QStringLiteral(":/shaders/fill_flat.frag.qsb");
            break;
        }

        QShader vs = loadShader(vsPath);
        QShader fs = loadShader(fsPath);
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load shaders");
            m_fillPipeline.reset();
            return;
        }

        m_fillPipeline->setShaderStages({
            { QRhiShaderStage::Vertex, vs },
            { QRhiShaderStage::Fragment, fs }
        });

        m_fillPipeline->setDepthTest(true);
        m_fillPipeline->setDepthWrite(true);
        m_fillPipeline->setCullMode(
            m_renderSettings.fillBackfaceCulling
                ? QRhiGraphicsPipeline::Back
                : QRhiGraphicsPipeline::None);

        QRhiVertexInputLayout inputLayout;
        // Fill vertex layout: position(3f) + normal(3f) + meshColor(4f) + texInfo(uv + useTexture flag).
        inputLayout.setBindings({ { kFillVertexStrideFloats * sizeof(float) } });
        if (m_renderSettings.fillShading == FillShading::Flat) {
            inputLayout.setAttributes({
                { 0, 0, QRhiVertexInputAttribute::Float3, 0 },                 // position
                { 0, 1, QRhiVertexInputAttribute::Float4, 6 * sizeof(float) }, // mesh color + use flag
                { 0, 2, QRhiVertexInputAttribute::Float3, 10 * sizeof(float) } // uv + use texture flag
            });
        } else {
            inputLayout.setAttributes({
                { 0, 0, QRhiVertexInputAttribute::Float3, 0 },                 // position
                { 0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float) }, // normal
                { 0, 2, QRhiVertexInputAttribute::Float4, 6 * sizeof(float) }, // mesh color + use flag
                { 0, 3, QRhiVertexInputAttribute::Float3, 10 * sizeof(float) } // uv + use texture flag
            });
        }
        m_fillPipeline->setVertexInputLayout(inputLayout);
        m_fillPipeline->setShaderResourceBindings(m_srb.get());
        m_fillPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

        if (!m_fillPipeline->create()) {
            qWarning("Failed to create fill pipeline");
            m_fillPipeline.reset();
        }
    }

    if (!m_wirePipeline) {
        m_wirePipeline.reset(m_rhi->newGraphicsPipeline());

        QShader vs = loadShader(QStringLiteral(":/shaders/fill_wire.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/fill_wire_overlay.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load wireframe shaders");
            m_wirePipeline.reset();
            return;
        }

        m_wirePipeline->setShaderStages({
            { QRhiShaderStage::Vertex, vs },
            { QRhiShaderStage::Fragment, fs }
        });
        m_wirePipeline->setDepthTest(true);
        m_wirePipeline->setDepthWrite(false);
        m_wirePipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        m_wirePipeline->setCullMode(
            m_renderSettings.wireBackfaceCulling
                ? QRhiGraphicsPipeline::Back
                : QRhiGraphicsPipeline::None);

        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.opColor = QRhiGraphicsPipeline::Add;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.opAlpha = QRhiGraphicsPipeline::Add;
        m_wirePipeline->setTargetBlends({ blend });

        QRhiVertexInputLayout wireLayout;
        wireLayout.setBindings({ { 6 * sizeof(float) } });
        wireLayout.setAttributes({
            { 0, 0, QRhiVertexInputAttribute::Float3, 0 },             // position
            { 0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float) } // barycentric
        });
        m_wirePipeline->setVertexInputLayout(wireLayout);
        m_wirePipeline->setShaderResourceBindings(m_srb.get());
        m_wirePipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

        if (!m_wirePipeline->create()) {
            qWarning("Failed to create wireframe pipeline");
            m_wirePipeline.reset();
        }
    }

    if (!m_edgesPipeline) {
        m_edgesPipeline.reset(m_rhi->newGraphicsPipeline());

        QShader vs = loadShader(QStringLiteral(":/shaders/overlay_edges.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/overlay_edges.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load edges shaders");
            m_edgesPipeline.reset();
            return;
        }

        m_edgesPipeline->setShaderStages({
            { QRhiShaderStage::Vertex, vs },
            { QRhiShaderStage::Fragment, fs }
        });
        m_edgesPipeline->setTopology(QRhiGraphicsPipeline::Lines);
        m_edgesPipeline->setDepthTest(true);
        m_edgesPipeline->setDepthWrite(true);
        m_edgesPipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        m_edgesPipeline->setDepthBias(-1);
        m_edgesPipeline->setSlopeScaledDepthBias(-1.0f);
        m_edgesPipeline->setCullMode(QRhiGraphicsPipeline::None);
        m_edgesPipeline->setLineWidth(qMax(1.0f, m_renderSettings.edgeSize));

        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.opColor = QRhiGraphicsPipeline::Add;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.opAlpha = QRhiGraphicsPipeline::Add;
        m_edgesPipeline->setTargetBlends({ blend });

        QRhiVertexInputLayout edgesLayout;
        edgesLayout.setBindings({ { 3 * sizeof(float) } });
        edgesLayout.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, 0 } });
        m_edgesPipeline->setVertexInputLayout(edgesLayout);
        m_edgesPipeline->setShaderResourceBindings(m_srb.get());
        m_edgesPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

        if (!m_edgesPipeline->create()) {
            qWarning("Failed to create edges pipeline");
            m_edgesPipeline.reset();
        }
    }

    if (!m_bboxPipeline) {
        m_bboxPipeline.reset(m_rhi->newGraphicsPipeline());

        QShader vs = loadShader(QStringLiteral(":/shaders/overlay_bbox.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/overlay_bbox.frag.qsb"));
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

    if (!m_pointsPipeline) {
        m_pointsPipeline.reset(m_rhi->newGraphicsPipeline());

        QShader vs = loadShader(QStringLiteral(":/shaders/overlay_points.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/overlay_points.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load points shaders");
            m_pointsPipeline.reset();
            return;
        }

        m_pointsPipeline->setShaderStages({
            { QRhiShaderStage::Vertex, vs },
            { QRhiShaderStage::Fragment, fs }
        });
        m_pointsPipeline->setTopology(QRhiGraphicsPipeline::Points);
        m_pointsPipeline->setDepthTest(true);
        m_pointsPipeline->setDepthWrite(true);
        m_pointsPipeline->setCullMode(QRhiGraphicsPipeline::None);

        QRhiVertexInputLayout ptsLayout;
        // Points vertex layout: position(3f) + meshColor(3f) + useMeshColorFlag(1f)
        // + normal(3f) + useNormalFlag(1f)
        ptsLayout.setBindings({ { kPointsVertexStrideFloats * sizeof(float) } });
        ptsLayout.setAttributes({
            { 0, 0, QRhiVertexInputAttribute::Float3, 0 },                 // position
            { 0, 1, QRhiVertexInputAttribute::Float4, 3 * sizeof(float) }, // mesh color + use flag
            { 0, 2, QRhiVertexInputAttribute::Float4, 7 * sizeof(float) }  // normal + use flag
        });
        m_pointsPipeline->setVertexInputLayout(ptsLayout);
        m_pointsPipeline->setShaderResourceBindings(m_srb.get());
        m_pointsPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

        if (!m_pointsPipeline->create()) {
            qWarning("Failed to create points pipeline");
            m_pointsPipeline.reset();
        }
    }

    if (!m_decoratorPipeline && m_decoratorSrbs[kDecoratorSlotVertexNormals]) {
        m_decoratorPipeline.reset(m_rhi->newGraphicsPipeline());

        QShader vs = loadShader(QStringLiteral(":/shaders/overlay_decorator.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/overlay_decorator.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load decorator shaders");
            m_decoratorPipeline.reset();
        } else {
            m_decoratorPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_decoratorPipeline->setTopology(QRhiGraphicsPipeline::Lines);
            m_decoratorPipeline->setDepthTest(true);
            m_decoratorPipeline->setDepthWrite(false);
            m_decoratorPipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
            m_decoratorPipeline->setDepthBias(-1);
            m_decoratorPipeline->setSlopeScaledDepthBias(-1.0f);
            m_decoratorPipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiVertexInputLayout layout;
            layout.setBindings({ { 3 * sizeof(float) } });
            layout.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, 0 } });
            m_decoratorPipeline->setVertexInputLayout(layout);
            m_decoratorPipeline->setShaderResourceBindings(
                m_decoratorSrbs[kDecoratorSlotVertexNormals].get());
            m_decoratorPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
            if (!m_decoratorPipeline->create()) {
                qWarning("Failed to create decorator pipeline");
                m_decoratorPipeline.reset();
            }
        }
    }

    if (!m_decoratorFatUbuf) {
        m_decoratorFatUbuf.reset(
            m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kDecoratorFatUbufSize));
        if (!m_decoratorFatUbuf || !m_decoratorFatUbuf->create())
            m_decoratorFatUbuf.reset();
    }
    if (!m_decoratorFatSrb && m_decoratorFatUbuf) {
        m_decoratorFatSrb.reset(m_rhi->newShaderResourceBindings());
        m_decoratorFatSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                m_decoratorFatUbuf.get())
        });
        if (!m_decoratorFatSrb->create())
            m_decoratorFatSrb.reset();
    }
    if (!m_decoratorFatPipeline && m_decoratorFatSrb) {
        m_decoratorFatPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/overlay_fat_decorator.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/overlay_fat_decorator.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load fat decorator shaders");
            m_decoratorFatPipeline.reset();
        } else {
            m_decoratorFatPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_decoratorFatPipeline->setTopology(QRhiGraphicsPipeline::Triangles);
            m_decoratorFatPipeline->setDepthTest(true);
            m_decoratorFatPipeline->setDepthWrite(false);
            m_decoratorFatPipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
            m_decoratorFatPipeline->setDepthBias(-1);
            m_decoratorFatPipeline->setSlopeScaledDepthBias(-1.0f);
            m_decoratorFatPipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiGraphicsPipeline::TargetBlend blend;
            blend.enable = true;
            blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
            blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            blend.opColor = QRhiGraphicsPipeline::Add;
            blend.srcAlpha = QRhiGraphicsPipeline::One;
            blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            blend.opAlpha = QRhiGraphicsPipeline::Add;
            m_decoratorFatPipeline->setTargetBlends({ blend });
            QRhiVertexInputLayout layout;
            layout.setBindings({ { 8 * sizeof(float) } });
            layout.setAttributes({
                { 0, 0, QRhiVertexInputAttribute::Float3, 0 },
                { 0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float) },
                { 0, 2, QRhiVertexInputAttribute::Float, 6 * sizeof(float) },
                { 0, 3, QRhiVertexInputAttribute::Float, 7 * sizeof(float) }
            });
            m_decoratorFatPipeline->setVertexInputLayout(layout);
            m_decoratorFatPipeline->setShaderResourceBindings(m_decoratorFatSrb.get());
            m_decoratorFatPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
            if (!m_decoratorFatPipeline->create()) {
                qWarning("Failed to create fat decorator pipeline");
                m_decoratorFatPipeline.reset();
            }
        }
    }

    if (!m_currentMaskFillPipeline && m_currentMaskRp) {
        m_currentMaskFillPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/selection_mask.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/selection_mask.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load current-mask shaders");
            m_currentMaskFillPipeline.reset();
        } else {
            m_currentMaskFillPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_currentMaskFillPipeline->setDepthTest(true);
            m_currentMaskFillPipeline->setDepthWrite(true);
            m_currentMaskFillPipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
            // Slight negative bias avoids self-occlusion when comparing current mesh
            // against a scene depth prepass that already contains the same geometry.
            m_currentMaskFillPipeline->setDepthBias(-1);
            m_currentMaskFillPipeline->setSlopeScaledDepthBias(-1.0f);
            // Outline mask generation should include both front and back faces.
            m_currentMaskFillPipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiVertexInputLayout layout;
            layout.setBindings({ { kFillVertexStrideFloats * sizeof(float) } });
            layout.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, 0 } });
            m_currentMaskFillPipeline->setVertexInputLayout(layout);
            m_currentMaskFillPipeline->setShaderResourceBindings(m_srb.get());
            m_currentMaskFillPipeline->setRenderPassDescriptor(m_currentMaskRp.get());
            if (!m_currentMaskFillPipeline->create()) {
                qWarning("Failed to create current-mask fill pipeline");
                m_currentMaskFillPipeline.reset();
            }
        }
    }

    if (!m_currentMaskFillDepthOnlyPipeline && m_currentMaskRp) {
        m_currentMaskFillDepthOnlyPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/depth_pick.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/depth_pick.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load current-mask fill depth-only shaders");
            m_currentMaskFillDepthOnlyPipeline.reset();
        } else {
            m_currentMaskFillDepthOnlyPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_currentMaskFillDepthOnlyPipeline->setDepthTest(true);
            m_currentMaskFillDepthOnlyPipeline->setDepthWrite(true);
            m_currentMaskFillDepthOnlyPipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiVertexInputLayout layout;
            layout.setBindings({ { kFillVertexStrideFloats * sizeof(float) } });
            layout.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, 0 } });
            m_currentMaskFillDepthOnlyPipeline->setVertexInputLayout(layout);
            m_currentMaskFillDepthOnlyPipeline->setShaderResourceBindings(m_srb.get());
            m_currentMaskFillDepthOnlyPipeline->setRenderPassDescriptor(m_currentMaskRp.get());
            if (!m_currentMaskFillDepthOnlyPipeline->create()) {
                qWarning("Failed to create current-mask fill depth-only pipeline");
                m_currentMaskFillDepthOnlyPipeline.reset();
            }
        }
    }

    if (!m_currentMaskEdgesPipeline && m_currentMaskRp) {
        m_currentMaskEdgesPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/selection_mask.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/selection_mask.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load current-mask edge shaders");
            m_currentMaskEdgesPipeline.reset();
        } else {
            m_currentMaskEdgesPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_currentMaskEdgesPipeline->setTopology(QRhiGraphicsPipeline::Lines);
            // Edge-mesh outline mask should capture all visible segments robustly.
            m_currentMaskEdgesPipeline->setDepthTest(false);
            m_currentMaskEdgesPipeline->setDepthWrite(false);
            m_currentMaskEdgesPipeline->setCullMode(QRhiGraphicsPipeline::None);
            m_currentMaskEdgesPipeline->setLineWidth(1.0f);
            QRhiVertexInputLayout layout;
            layout.setBindings({ { 3 * sizeof(float) } });
            layout.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, 0 } });
            m_currentMaskEdgesPipeline->setVertexInputLayout(layout);
            m_currentMaskEdgesPipeline->setShaderResourceBindings(m_srb.get());
            m_currentMaskEdgesPipeline->setRenderPassDescriptor(m_currentMaskRp.get());
            if (!m_currentMaskEdgesPipeline->create()) {
                qWarning("Failed to create current-mask edges pipeline");
                m_currentMaskEdgesPipeline.reset();
            }
        }
    }

    if (!m_currentMaskEdgesDepthPipeline && m_currentMaskRp) {
        m_currentMaskEdgesDepthPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/selection_mask.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/selection_mask.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load current-mask depth-edges shaders");
            m_currentMaskEdgesDepthPipeline.reset();
        } else {
            m_currentMaskEdgesDepthPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_currentMaskEdgesDepthPipeline->setTopology(QRhiGraphicsPipeline::Lines);
            m_currentMaskEdgesDepthPipeline->setDepthTest(true);
            m_currentMaskEdgesDepthPipeline->setDepthWrite(true);
            m_currentMaskEdgesDepthPipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
            m_currentMaskEdgesDepthPipeline->setDepthBias(-1);
            m_currentMaskEdgesDepthPipeline->setSlopeScaledDepthBias(-1.0f);
            m_currentMaskEdgesDepthPipeline->setCullMode(QRhiGraphicsPipeline::None);
            m_currentMaskEdgesDepthPipeline->setLineWidth(1.0f);
            QRhiVertexInputLayout layout;
            layout.setBindings({ { 3 * sizeof(float) } });
            layout.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, 0 } });
            m_currentMaskEdgesDepthPipeline->setVertexInputLayout(layout);
            m_currentMaskEdgesDepthPipeline->setShaderResourceBindings(m_srb.get());
            m_currentMaskEdgesDepthPipeline->setRenderPassDescriptor(m_currentMaskRp.get());
            if (!m_currentMaskEdgesDepthPipeline->create()) {
                qWarning("Failed to create current-mask depth-edges pipeline");
                m_currentMaskEdgesDepthPipeline.reset();
            }
        }
    }

    if (!m_currentMaskEdgesDepthOnlyPipeline && m_currentMaskRp) {
        m_currentMaskEdgesDepthOnlyPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/depth_pick.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/depth_pick.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load current-mask edge depth-only shaders");
            m_currentMaskEdgesDepthOnlyPipeline.reset();
        } else {
            m_currentMaskEdgesDepthOnlyPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_currentMaskEdgesDepthOnlyPipeline->setTopology(QRhiGraphicsPipeline::Lines);
            m_currentMaskEdgesDepthOnlyPipeline->setDepthTest(true);
            m_currentMaskEdgesDepthOnlyPipeline->setDepthWrite(true);
            m_currentMaskEdgesDepthOnlyPipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
            m_currentMaskEdgesDepthOnlyPipeline->setCullMode(QRhiGraphicsPipeline::None);
            m_currentMaskEdgesDepthOnlyPipeline->setLineWidth(1.0f);
            QRhiVertexInputLayout layout;
            layout.setBindings({ { 3 * sizeof(float) } });
            layout.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, 0 } });
            m_currentMaskEdgesDepthOnlyPipeline->setVertexInputLayout(layout);
            m_currentMaskEdgesDepthOnlyPipeline->setShaderResourceBindings(m_srb.get());
            m_currentMaskEdgesDepthOnlyPipeline->setRenderPassDescriptor(m_currentMaskRp.get());
            if (!m_currentMaskEdgesDepthOnlyPipeline->create()) {
                qWarning("Failed to create current-mask edge depth-only pipeline");
                m_currentMaskEdgesDepthOnlyPipeline.reset();
            }
        }
    }

    if (!m_currentMaskPointsPipeline && m_currentMaskRp) {
        m_currentMaskPointsPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/selection_mask.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/selection_mask.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load current-mask point shaders");
            m_currentMaskPointsPipeline.reset();
        } else {
            m_currentMaskPointsPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_currentMaskPointsPipeline->setTopology(QRhiGraphicsPipeline::Points);
            // Point-cloud outline mask: draw all vertices as pure occupancy (no depth/shading).
            m_currentMaskPointsPipeline->setDepthTest(false);
            m_currentMaskPointsPipeline->setDepthWrite(false);
            m_currentMaskPointsPipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiVertexInputLayout layout;
            layout.setBindings({ { kPointsVertexStrideFloats * sizeof(float) } });
            layout.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, 0 } });
            m_currentMaskPointsPipeline->setVertexInputLayout(layout);
            m_currentMaskPointsPipeline->setShaderResourceBindings(m_srb.get());
            m_currentMaskPointsPipeline->setRenderPassDescriptor(m_currentMaskRp.get());
            if (!m_currentMaskPointsPipeline->create()) {
                qWarning("Failed to create current-mask points pipeline");
                m_currentMaskPointsPipeline.reset();
            }
        }
    }

    if (!m_currentMaskPointsDepthOnlyPipeline && m_currentMaskRp) {
        m_currentMaskPointsDepthOnlyPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/depth_pick.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/depth_pick.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load current-mask points depth-only shaders");
            m_currentMaskPointsDepthOnlyPipeline.reset();
        } else {
            m_currentMaskPointsDepthOnlyPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_currentMaskPointsDepthOnlyPipeline->setTopology(QRhiGraphicsPipeline::Points);
            m_currentMaskPointsDepthOnlyPipeline->setDepthTest(true);
            m_currentMaskPointsDepthOnlyPipeline->setDepthWrite(true);
            m_currentMaskPointsDepthOnlyPipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiVertexInputLayout layout;
            layout.setBindings({ { kPointsVertexStrideFloats * sizeof(float) } });
            layout.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, 0 } });
            m_currentMaskPointsDepthOnlyPipeline->setVertexInputLayout(layout);
            m_currentMaskPointsDepthOnlyPipeline->setShaderResourceBindings(m_srb.get());
            m_currentMaskPointsDepthOnlyPipeline->setRenderPassDescriptor(m_currentMaskRp.get());
            if (!m_currentMaskPointsDepthOnlyPipeline->create()) {
                qWarning("Failed to create current-mask points depth-only pipeline");
                m_currentMaskPointsDepthOnlyPipeline.reset();
            }
        }
    }

    if (!m_maskMorphToBasePipeline && m_maskMorphMaskToBaseSrb && m_currentMaskBaseRp) {
        m_maskMorphToBasePipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/selection_outline.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/selection_mask_morph.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load mask-morph shaders (to base)");
            m_maskMorphToBasePipeline.reset();
        } else {
            m_maskMorphToBasePipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_maskMorphToBasePipeline->setDepthTest(false);
            m_maskMorphToBasePipeline->setDepthWrite(false);
            m_maskMorphToBasePipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiVertexInputLayout layout;
            m_maskMorphToBasePipeline->setVertexInputLayout(layout);
            m_maskMorphToBasePipeline->setShaderResourceBindings(m_maskMorphMaskToBaseSrb.get());
            m_maskMorphToBasePipeline->setRenderPassDescriptor(m_currentMaskBaseRp.get());
            if (!m_maskMorphToBasePipeline->create()) {
                qWarning("Failed to create mask-morph pipeline (to base)");
                m_maskMorphToBasePipeline.reset();
            }
        }
    }

    if (!m_maskMorphToWorkPipeline && m_maskMorphMaskToWorkSrb && m_currentMaskWorkRp) {
        m_maskMorphToWorkPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/selection_outline.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/selection_mask_morph.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load mask-morph shaders (to work)");
            m_maskMorphToWorkPipeline.reset();
        } else {
            m_maskMorphToWorkPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_maskMorphToWorkPipeline->setDepthTest(false);
            m_maskMorphToWorkPipeline->setDepthWrite(false);
            m_maskMorphToWorkPipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiVertexInputLayout layout;
            m_maskMorphToWorkPipeline->setVertexInputLayout(layout);
            m_maskMorphToWorkPipeline->setShaderResourceBindings(m_maskMorphMaskToWorkSrb.get());
            m_maskMorphToWorkPipeline->setRenderPassDescriptor(m_currentMaskWorkRp.get());
            if (!m_maskMorphToWorkPipeline->create()) {
                qWarning("Failed to create mask-morph pipeline (to work)");
                m_maskMorphToWorkPipeline.reset();
            }
        }
    }

    if (!m_maskMorphWorkToMaskPipeline && m_maskMorphWorkToMaskSrb && m_currentMaskRp) {
        m_maskMorphWorkToMaskPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/selection_outline.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/selection_mask_morph.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load mask-morph shaders (to mask)");
            m_maskMorphWorkToMaskPipeline.reset();
        } else {
            m_maskMorphWorkToMaskPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_maskMorphWorkToMaskPipeline->setDepthTest(false);
            m_maskMorphWorkToMaskPipeline->setDepthWrite(false);
            m_maskMorphWorkToMaskPipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiVertexInputLayout layout;
            m_maskMorphWorkToMaskPipeline->setVertexInputLayout(layout);
            m_maskMorphWorkToMaskPipeline->setShaderResourceBindings(m_maskMorphWorkToMaskSrb.get());
            m_maskMorphWorkToMaskPipeline->setRenderPassDescriptor(m_currentMaskRp.get());
            if (!m_maskMorphWorkToMaskPipeline->create()) {
                qWarning("Failed to create mask-morph pipeline (to mask)");
                m_maskMorphWorkToMaskPipeline.reset();
            }
        }
    }

    if (!m_maskDebugPipeline && m_maskDebugBaseSrb) {
        m_maskDebugPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/selection_outline.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/selection_mask_debug.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load mask-debug shaders");
            m_maskDebugPipeline.reset();
        } else {
            m_maskDebugPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_maskDebugPipeline->setDepthTest(false);
            m_maskDebugPipeline->setDepthWrite(false);
            m_maskDebugPipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiVertexInputLayout layout;
            m_maskDebugPipeline->setVertexInputLayout(layout);
            m_maskDebugPipeline->setShaderResourceBindings(m_maskDebugBaseSrb.get());
            m_maskDebugPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
            if (!m_maskDebugPipeline->create()) {
                qWarning("Failed to create mask-debug pipeline");
                m_maskDebugPipeline.reset();
            }
        }
    }

    if (!m_outlinePipeline && m_outlineSrb) {
        m_outlinePipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/selection_outline.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/selection_outline.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load outline shaders");
            m_outlinePipeline.reset();
        } else {
            m_outlinePipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_outlinePipeline->setDepthTest(false);
            m_outlinePipeline->setDepthWrite(false);
            m_outlinePipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiGraphicsPipeline::TargetBlend blend;
            blend.enable = true;
            blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
            blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            blend.opColor = QRhiGraphicsPipeline::Add;
            blend.srcAlpha = QRhiGraphicsPipeline::One;
            blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            blend.opAlpha = QRhiGraphicsPipeline::Add;
            m_outlinePipeline->setTargetBlends({ blend });
            QRhiVertexInputLayout layout;
            m_outlinePipeline->setVertexInputLayout(layout);
            m_outlinePipeline->setShaderResourceBindings(m_outlineSrb.get());
            m_outlinePipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
            if (!m_outlinePipeline->create()) {
                qWarning("Failed to create outline pipeline");
                m_outlinePipeline.reset();
            }
        }
    }

    if (!m_trackballGizmoUbuf) {
        m_trackballGizmoUbuf.reset(
            m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kTrackballGizmoUbufSize));
        m_trackballGizmoUbuf->create();
    }

    if (!m_trackballGizmoVbuf) {
        const auto &verts = trackballGizmoVertices();
        m_trackballGizmoVbuf.reset(
            m_rhi->newBuffer(
                QRhiBuffer::Dynamic,
                QRhiBuffer::VertexBuffer,
                static_cast<quint32>(verts.size() * sizeof(float))));
        m_trackballGizmoVbuf->create();
        m_trackballGizmoVertexCount = int(verts.size() / 6);
    }

    if (!m_trackballGizmoSrb && m_trackballGizmoUbuf) {
        m_trackballGizmoSrb.reset(m_rhi->newShaderResourceBindings());
        m_trackballGizmoSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::VertexStage,
                m_trackballGizmoUbuf.get())
        });
        m_trackballGizmoSrb->create();
    }

    if (!m_trackballGizmoPipeline && m_trackballGizmoSrb) {
        m_trackballGizmoPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/overlay_trackball_gizmo.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/overlay_trackball_gizmo.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            qWarning("Failed to load trackball gizmo shaders");
            m_trackballGizmoPipeline.reset();
        } else {
            m_trackballGizmoPipeline->setShaderStages({
                { QRhiShaderStage::Vertex, vs },
                { QRhiShaderStage::Fragment, fs }
            });
            m_trackballGizmoPipeline->setTopology(QRhiGraphicsPipeline::Lines);
            m_trackballGizmoPipeline->setDepthTest(true);
            m_trackballGizmoPipeline->setDepthWrite(false);
            m_trackballGizmoPipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
            m_trackballGizmoPipeline->setCullMode(QRhiGraphicsPipeline::None);
            QRhiGraphicsPipeline::TargetBlend blend;
            blend.enable = true;
            blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
            blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            blend.opColor = QRhiGraphicsPipeline::Add;
            blend.srcAlpha = QRhiGraphicsPipeline::One;
            blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            blend.opAlpha = QRhiGraphicsPipeline::Add;
            m_trackballGizmoPipeline->setTargetBlends({ blend });
            QRhiVertexInputLayout layout;
            layout.setBindings({ { 6 * sizeof(float) } });
            layout.setAttributes({
                { 0, 0, QRhiVertexInputAttribute::Float3, 0 },
                { 0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float) }
            });
            m_trackballGizmoPipeline->setVertexInputLayout(layout);
            m_trackballGizmoPipeline->setShaderResourceBindings(m_trackballGizmoSrb.get());
            m_trackballGizmoPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
            if (!m_trackballGizmoPipeline->create()) {
                qWarning("Failed to create trackball gizmo pipeline");
                m_trackballGizmoPipeline.reset();
            }
        }
    }
}
