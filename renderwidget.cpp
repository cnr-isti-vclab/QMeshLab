#include "renderwidget.h"
#include "document.h"
#include "renderoverlaypanel.h"
#include <wrap/io_trimesh/io_mask.h>
#include <QFile>
#include <QImage>
#include <QMouseEvent>
#include <QResizeEvent>
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

namespace {
constexpr int kUbufSize = 288;
constexpr int kUbufFloatCount = kUbufSize / sizeof(float);
constexpr int kUbufBBoxColorOffset = 176 / sizeof(float);
constexpr int kUbufPointColorOffset = 192 / sizeof(float);
constexpr int kUbufPointParamsOffset = 208 / sizeof(float);
constexpr int kUbufWireColorOffset = 224 / sizeof(float);
constexpr int kUbufWireParamsOffset = 240 / sizeof(float);
constexpr int kUbufFillColorOffset = 256 / sizeof(float);
constexpr int kUbufLightingParamsOffset = 272 / sizeof(float);
constexpr int kFillVertexStrideFloats = 13;
constexpr int kPointsVertexStrideFloats = 11;
constexpr int kMaskMorphUbufSize = 16;
constexpr int kMaskDebugUbufSize = 16;
constexpr int kOutlineUbufSize = 32;
}

RenderWidget::RenderWidget(Document *doc, QWidget *parent)
    : QRhiWidget(parent), m_doc(doc)
{
    createOverlayButtons();
    refreshColorSourceAvailability();

    connect(m_doc, &Document::meshAdded, this, [this](int) {
        m_reframeCameraRequested = true;
        applySceneDefaultRenderModeIfNeeded();
        refreshColorSourceAvailability();
        m_textureSrbs.clear();
        update();
    });
    connect(m_doc, &Document::meshRemoved, this, [this](int) {
        m_reframeCameraRequested = true;
        if (m_doc->meshCount() == 0)
            m_applySceneDefaultRenderMode = true;
        refreshColorSourceAvailability();
        m_textureSrbs.clear();
        update();
    });
    connect(m_doc, &Document::meshVisibilityChanged, this, [this](int, bool) {
        update();
    });
    connect(m_doc, &Document::currentMeshChanged, this, [this](int) {
        update();
    });
}

void RenderWidget::setShadingMode(ShadingMode mode)
{
    if (mode == ShadingMode::Wireframe) {
        const bool fillChanged = !m_renderSettings.showFill;
        const bool wireChanged = !m_renderSettings.showWire;
        m_renderSettings.showWire = true;
        m_renderSettings.showFill = true;

        if (fillChanged)
            m_fillPipeline.reset();
        if (wireChanged)
            m_wirePipeline.reset();

        if (m_overlayPanel) {
            m_overlayPanel->setSettings(m_renderSettings);
        }
        update();
        return;
    }

    if (m_shadingMode == mode)
        return;

    m_shadingMode = mode;
    m_renderSettings.fillShading = (mode == ShadingMode::Flat) ? FillShading::Flat : FillShading::Smooth;
    if (m_overlayPanel)
        m_overlayPanel->setSettings(m_renderSettings);
    m_fillPipeline.reset();
    update();
}

void RenderWidget::createOverlayButtons()
{
    m_overlayPanel = new RenderOverlayPanel(this);
    m_overlayPanel->setSettings(m_renderSettings);

    connect(m_overlayPanel, &RenderOverlayPanel::settingsChanged, this,
            [this](const RenderSettings &settings) {
        const RenderSettings prev = m_renderSettings;
        m_renderSettings = settings;

        m_shadingMode = (m_renderSettings.fillShading == FillShading::Flat)
            ? ShadingMode::Flat
            : ShadingMode::Smooth;

        if (prev.showFill != m_renderSettings.showFill
            || prev.fillShading != m_renderSettings.fillShading
            || prev.fillBackfaceCulling != m_renderSettings.fillBackfaceCulling) {
            m_fillPipeline.reset();
        }
        if (prev.showWire != m_renderSettings.showWire
            || prev.wireBackfaceCulling != m_renderSettings.wireBackfaceCulling) {
            m_wirePipeline.reset();
        }
        update();
        layoutOverlayButtons();
    });

    layoutOverlayButtons();
}

void RenderWidget::layoutOverlayButtons()
{
    if (!m_overlayPanel)
        return;

    m_overlayPanel->adjustSize();
    m_overlayPanel->move(8, 8);
    m_overlayPanel->raise();
}

void RenderWidget::applySceneDefaultRenderModeIfNeeded()
{
    if (!m_applySceneDefaultRenderMode)
        return;

    if (m_doc->meshCount() <= 0)
        return;

    m_applySceneDefaultRenderMode = false;

    bool hasFaces = false;
    bool hasVertexColors = false;
    bool hasVertexNormals = false;
    for (int i = 0; i < m_doc->meshCount(); ++i) {
        if (m_doc->mesh(i).mesh.FN() > 0) {
            hasFaces = true;
            break;
        }
        const int mask = m_doc->mesh(i).ioMask;
        hasVertexColors = hasVertexColors || ((mask & vcg::tri::io::Mask::IOM_VERTCOLOR) != 0);
        hasVertexNormals = hasVertexNormals || ((mask & vcg::tri::io::Mask::IOM_VERTNORMAL) != 0);
    }

    if (hasFaces)
        return;

    m_renderSettings.showPoints = true;
    m_renderSettings.showWire = false;
    m_renderSettings.showFill = false;
    m_renderSettings.currentPass = RenderPass::Points;
    m_renderSettings.pointColorSource = hasVertexColors
        ? PointColorSource::PerVertex
        : PointColorSource::Constant;
    m_renderSettings.pointLighting = hasVertexNormals;

    m_shadingMode = ShadingMode::Smooth;
    m_fillPipeline.reset();
    m_wirePipeline.reset();

    if (m_overlayPanel)
        m_overlayPanel->setSettings(m_renderSettings);
}

void RenderWidget::refreshColorSourceAvailability()
{
    bool hasVertexColors = false;
    bool hasFaceColors = false;
    bool hasVertexNormals = false;
    for (int i = 0; i < m_doc->meshCount(); ++i) {
        const int mask = m_doc->mesh(i).ioMask;
        hasVertexColors = hasVertexColors || ((mask & vcg::tri::io::Mask::IOM_VERTCOLOR) != 0);
        hasFaceColors = hasFaceColors || ((mask & vcg::tri::io::Mask::IOM_FACECOLOR) != 0);
        hasVertexNormals = hasVertexNormals || ((mask & vcg::tri::io::Mask::IOM_VERTNORMAL) != 0);
    }

    if (m_overlayPanel)
        m_overlayPanel->setPointColorSourceAvailability(hasVertexColors);
    if (m_overlayPanel)
        m_overlayPanel->setPointLightingAvailability(hasVertexNormals);
    if (m_overlayPanel)
        m_overlayPanel->setFillColorSourceAvailability(hasVertexColors, hasFaceColors);

    RenderSettings corrected = m_renderSettings;
    if (corrected.pointColorSource == PointColorSource::PerVertex && !hasVertexColors)
        corrected.pointColorSource = PointColorSource::Constant;
    if (corrected.pointLighting && !hasVertexNormals)
        corrected.pointLighting = false;
    if (corrected.fillColorSource == FillColorSource::PerVertex && !hasVertexColors)
        corrected.fillColorSource = FillColorSource::Constant;
    if (corrected.fillColorSource == FillColorSource::PerFace && !hasFaceColors)
        corrected.fillColorSource = FillColorSource::Constant;

    if (corrected != m_renderSettings) {
        m_renderSettings = corrected;
        if (m_overlayPanel)
            m_overlayPanel->setSettings(m_renderSettings);
    }
}

int RenderWidget::fillGpuVariantIndexForCurrentSettings() const
{
    switch (m_renderSettings.fillColorSource) {
    case FillColorSource::PerVertex: return static_cast<int>(Document::FillGpuVariant::PerVertex);
    case FillColorSource::PerFace: return static_cast<int>(Document::FillGpuVariant::PerFace);
    case FillColorSource::Constant:
    default:
        return static_cast<int>(Document::FillGpuVariant::Constant);
    }
}

int RenderWidget::pointGpuVariantIndexForCurrentSettings() const
{
    switch (m_renderSettings.pointColorSource) {
    case PointColorSource::PerVertex: return static_cast<int>(Document::PointGpuVariant::PerVertex);
    case PointColorSource::Constant:
    default:
        return static_cast<int>(Document::PointGpuVariant::Constant);
    }
}

QRhiShaderResourceBindings *RenderWidget::shaderResourcesForTexture(QRhiTexture *texture)
{
    if (!texture || !m_rhi || !m_ubuf || !m_textureSampler)
        return m_srb.get();

    auto it = m_textureSrbs.find(texture);
    if (it != m_textureSrbs.end())
        return it->second.get();

    auto textureSrb = std::unique_ptr<QRhiShaderResourceBindings>(m_rhi->newShaderResourceBindings());
    textureSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0,
            QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_ubuf.get()),
        QRhiShaderResourceBinding::sampledTexture(
            1,
            QRhiShaderResourceBinding::FragmentStage,
            texture,
            m_textureSampler.get())
    });
    if (!textureSrb->create())
        return m_srb.get();

    QRhiShaderResourceBindings *raw = textureSrb.get();
    m_textureSrbs.emplace(texture, std::move(textureSrb));
    return raw;
}

void RenderWidget::updateCameraFrameIfNeeded()
{
    if (!m_reframeCameraRequested)
        return;
    if (m_doc->meshCount() == 0)
        return;

    vcg::Box3f bbox;
    for (int i = 0; i < m_doc->meshCount(); ++i)
        bbox.Add(m_doc->mesh(i).mesh.bbox);
    if (bbox.IsNull())
        return;

    auto c = bbox.Center();
    m_center = QVector3D(c[0], c[1], c[2]);
    m_radius = bbox.Diag() / 2.0f;
    m_distance = m_radius * 3.0f;
    m_reframeCameraRequested = false;
}

void RenderWidget::ensureCurrentMeshMaskResources(const QSize &pixelSize)
{
    if (!m_rhi || pixelSize.isEmpty())
        return;

    if (m_currentMaskRt && m_currentMaskSize == pixelSize)
        return;

    m_currentMaskFillPipeline.reset();
    m_currentMaskPointsPipeline.reset();
    m_maskMorphMaskToBaseSrb.reset();
    m_maskMorphMaskToWorkSrb.reset();
    m_maskMorphWorkToMaskSrb.reset();
    m_maskMorphToBasePipeline.reset();
    m_maskMorphToWorkPipeline.reset();
    m_maskMorphWorkToMaskPipeline.reset();
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

void RenderWidget::ensureRenderResources()
{
    if (m_rhi != rhi()) {
        if (m_rhi)
            m_doc->releaseRhiGpuResources(m_rhi);
        m_rhi = rhi();
        m_fillPipeline.reset();
        m_wirePipeline.reset();
        m_bboxPipeline.reset();
        m_pointsPipeline.reset();
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
        m_currentMaskPointsPipeline.reset();
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
        m_maskDebugUbuf.reset();
        m_maskDebugBaseSrb.reset();
        m_maskDebugWorkSrb.reset();
        m_maskDebugMaskSrb.reset();
        m_maskDebugPipeline.reset();
        m_outlineUbuf.reset();
        m_outlineSampler.reset();
        m_outlineSrb.reset();
        m_outlinePipeline.reset();
        m_srb.reset();
        m_ubuf.reset();
        m_textureSampler.reset();
        m_fallbackTexture.reset();
        m_fallbackTextureUploadPending = false;
        m_textureSrbs.clear();
    }

    if (!m_rhi || !renderTarget())
        return;

    if (!m_ubuf) {
        // Uniform buffer: mvp + modelView + normalMat + bbox/point/wire/fill parameters
        m_ubuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUbufSize));
        m_ubuf->create();
    }

    if (!m_textureSampler) {
        m_textureSampler.reset(
            m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                              QRhiSampler::Repeat, QRhiSampler::Repeat));
        m_textureSampler->create();
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
        m_srb->create();
    }

    if (!m_outlineUbuf) {
        m_outlineUbuf.reset(
            m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kOutlineUbufSize));
        m_outlineUbuf->create();
    }
    if (!m_outlineSampler) {
        m_outlineSampler.reset(
            m_rhi->newSampler(
                QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
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
    if (!m_maskDebugBaseSrb
        && m_maskDebugUbuf
        && m_maskMorphSampler
        && m_currentMaskBaseTexture) {
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
                m_maskMorphSampler.get())
        });
        m_maskDebugBaseSrb->create();
    }
    if (!m_maskDebugWorkSrb
        && m_maskDebugUbuf
        && m_maskMorphSampler
        && m_currentMaskWorkTexture) {
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
                m_maskMorphSampler.get())
        });
        m_maskDebugWorkSrb->create();
    }
    if (!m_maskDebugMaskSrb
        && m_maskDebugUbuf
        && m_maskMorphSampler
        && m_currentMaskTexture) {
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
                m_maskMorphSampler.get())
        });
        m_maskDebugMaskSrb->create();
    }
    if (!m_outlineSrb && m_outlineUbuf && m_outlineSampler && m_currentMaskTexture) {
        m_outlineSrb.reset(m_rhi->newShaderResourceBindings());
        m_outlineSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::FragmentStage,
                m_outlineUbuf.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1,
                QRhiShaderResourceBinding::FragmentStage,
                m_currentMaskTexture.get(),
                m_outlineSampler.get())
        });
        m_outlineSrb->create();
    }

    if (!m_renderSettings.showFill) {
        m_fillPipeline.reset();
    } else if (!m_fillPipeline) {
        m_fillPipeline.reset(m_rhi->newGraphicsPipeline());

        QString vsPath;
        QString fsPath;
        switch (m_renderSettings.fillShading) {
        case FillShading::Smooth:
            vsPath = QStringLiteral(":/shaders/color.vert.qsb");
            fsPath = QStringLiteral(":/shaders/color.frag.qsb");
            break;
        case FillShading::Flat:
            vsPath = QStringLiteral(":/shaders/flat.vert.qsb");
            fsPath = QStringLiteral(":/shaders/flat.frag.qsb");
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

    if (!m_renderSettings.showWire) {
        m_wirePipeline.reset();
    } else if (!m_wirePipeline) {
        m_wirePipeline.reset(m_rhi->newGraphicsPipeline());

        QShader vs = loadShader(QStringLiteral(":/shaders/wireframe.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/wireframe_lines.frag.qsb"));
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

    if (!m_pointsPipeline) {
        m_pointsPipeline.reset(m_rhi->newGraphicsPipeline());

        QShader vs = loadShader(QStringLiteral(":/shaders/points.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/points.frag.qsb"));
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

    if (!m_currentMaskFillPipeline && m_currentMaskRp) {
        m_currentMaskFillPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/current_mask.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/current_mask.frag.qsb"));
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
            m_currentMaskFillPipeline->setCullMode(QRhiGraphicsPipeline::Back);
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

    if (!m_currentMaskPointsPipeline && m_currentMaskRp) {
        m_currentMaskPointsPipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/current_mask.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/current_mask.frag.qsb"));
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

    if (!m_maskMorphToBasePipeline && m_maskMorphMaskToBaseSrb && m_currentMaskBaseRp) {
        m_maskMorphToBasePipeline.reset(m_rhi->newGraphicsPipeline());
        QShader vs = loadShader(QStringLiteral(":/shaders/outline.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/mask_morph.frag.qsb"));
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
        QShader vs = loadShader(QStringLiteral(":/shaders/outline.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/mask_morph.frag.qsb"));
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
        QShader vs = loadShader(QStringLiteral(":/shaders/outline.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/mask_morph.frag.qsb"));
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
        QShader vs = loadShader(QStringLiteral(":/shaders/outline.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/mask_debug.frag.qsb"));
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
        QShader vs = loadShader(QStringLiteral(":/shaders/outline.vert.qsb"));
        QShader fs = loadShader(QStringLiteral(":/shaders/outline.frag.qsb"));
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
}

void RenderWidget::prepareDirtyBuffers(QRhiCommandBuffer *cb)
{
    if (!m_rhi)
        return;

    updateCameraFrameIfNeeded();

    if (m_fallbackTextureUploadPending && m_fallbackTexture) {
        QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
        QImage white(1, 1, QImage::Format_RGBA8888);
        white.fill(Qt::white);
        QRhiTextureUploadEntry entry(0, 0, QRhiTextureSubresourceUploadDescription(white));
        u->uploadTexture(m_fallbackTexture.get(), QRhiTextureUploadDescription({ entry }));
        cb->resourceUpdate(u);
        m_fallbackTextureUploadPending = false;
    }

    const bool needFill =
        m_renderSettings.showFill || m_renderSettings.highlightCurrentMesh;
    const bool needWire = m_renderSettings.showWire;
    const bool needPoints =
        m_renderSettings.showPoints || m_renderSettings.highlightCurrentMesh;
    const bool needBBox = m_renderSettings.showBoundingBox;
    if (!needFill && !needWire && !needPoints && !needBBox)
        return;

    const auto pointVariant = static_cast<Document::PointGpuVariant>(
        pointGpuVariantIndexForCurrentSettings());
    const auto fillVariant = static_cast<Document::FillGpuVariant>(
        m_renderSettings.showFill
            ? fillGpuVariantIndexForCurrentSettings()
            : static_cast<int>(Document::FillGpuVariant::Constant));

    for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
        const auto &meshEntry = m_doc->mesh(mi);
        if (!meshEntry.visible)
            continue;
        m_doc->ensureMeshGpuResources(
            m_rhi,
            cb,
            mi,
            fillVariant,
            pointVariant,
            needFill,
            needWire,
            needPoints,
            needBBox);
    }
}

void RenderWidget::renderCurrentMeshMask(QRhiCommandBuffer *cb, const QSize &pixelSize)
{
    if (!m_renderSettings.highlightCurrentMesh)
        return;

    m_currentMaskFromPoints = false;

    const int currentMeshIndex = m_doc->currentMeshIndex();
    if (currentMeshIndex < 0)
        return;

    ensureCurrentMeshMaskResources(pixelSize);
    if (!m_currentMaskRt)
        return;

    cb->beginPass(m_currentMaskRt.get(), Qt::transparent, { 1.0f, 0 }, nullptr);
    cb->setViewport({ 0, 0, float(pixelSize.width()), float(pixelSize.height()) });

    const auto pointVariant = static_cast<Document::PointGpuVariant>(
        pointGpuVariantIndexForCurrentSettings());
    const auto fillVariant = static_cast<Document::FillGpuVariant>(
        m_renderSettings.showFill
            ? fillGpuVariantIndexForCurrentSettings()
            : static_cast<int>(Document::FillGpuVariant::Constant));

    bool drewSurface = false;
    if (m_currentMaskFillPipeline) {
        const Document::FillPassGpuView fillView =
            m_doc->fillPassGpuView(m_rhi, currentMeshIndex, fillVariant);
        cb->setGraphicsPipeline(m_currentMaskFillPipeline.get());
        cb->setShaderResources();
        for (int bi = 0; bi < fillView.batchCount; ++bi) {
            const auto &batch = fillView.batches[bi];
            if (!batch.vertexBuffer)
                continue;
            if (batch.indexCount == 0 && batch.vertexCount == 0)
                continue;
            drewSurface = true;
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

    if (!drewSurface && m_currentMaskPointsPipeline) {
        const Document::PointsPassGpuView pointsView =
            m_doc->pointsPassGpuView(m_rhi, currentMeshIndex, pointVariant);
        cb->setGraphicsPipeline(m_currentMaskPointsPipeline.get());
        cb->setShaderResources();
        if (pointsView.valid && pointsView.vertexBuffer && pointsView.vertexCount > 0) {
            m_currentMaskFromPoints = true;
            const QRhiCommandBuffer::VertexInput pv(pointsView.vertexBuffer, 0);
            cb->setVertexInput(0, 1, &pv);
            cb->draw(pointsView.vertexCount);
        }
    }

    cb->endPass();
}

void RenderWidget::processCurrentMeshMask(QRhiCommandBuffer *cb, const QSize &pixelSize)
{
    if (!m_renderSettings.highlightCurrentMesh)
        return;
    if (!m_currentMaskRt || !m_currentMaskBaseRt || !m_currentMaskWorkRt)
        return;
    if (!m_maskMorphCopyUbuf
        || !m_maskMorphDilateUbuf
        || !m_maskMorphErodeUbuf
        || !m_maskMorphMaskToBaseSrb
        || !m_maskMorphMaskToWorkSrb
        || !m_maskMorphWorkToMaskSrb)
        return;
    if (!m_maskMorphToBasePipeline
        || !m_maskMorphToWorkPipeline
        || !m_maskMorphWorkToMaskPipeline)
        return;

    const float invW = 1.0f / float(qMax(1, pixelSize.width()));
    const float invH = 1.0f / float(qMax(1, pixelSize.height()));

    auto updateMorphParams = [&](QRhiBuffer *ubuf, float mode, float radiusPx) {
        // mode: 0 = dilate, 1 = erode
        const float params[4] = { invW, invH, radiusPx, mode };
        QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
        u->updateDynamicBuffer(ubuf, 0, kMaskMorphUbufSize, params);
        cb->resourceUpdate(u);
    };

    // Snapshot base point mask for debugging/inspection before any morphology.
    updateMorphParams(m_maskMorphCopyUbuf.get(), 0.0f, 0.0f);
    cb->beginPass(m_currentMaskBaseRt.get(), Qt::transparent, { 1.0f, 0 }, nullptr);
    cb->setGraphicsPipeline(m_maskMorphToBasePipeline.get());
    cb->setViewport({ 0, 0, float(pixelSize.width()), float(pixelSize.height()) });
    cb->setShaderResources(m_maskMorphMaskToBaseSrb.get());
    cb->draw(3);
    cb->endPass();

    if (!m_currentMaskFromPoints)
        return;

    // Point-cloud outline pipeline:
    // 1) dilate(base, dilateRadius) -> work
    // 2) erode(work, erodeRadius) -> mask
    // 3) edge filter on final mask texture
    // This keeps mask rasterization cheap (1px points) and pushes expansion to screen space.
    const float dilateRadiusPx = qMax(0.0f, m_renderSettings.currentMeshDilateRadius);
    const float erosionRadiusPx = qBound(
        0.0f,
        m_renderSettings.currentMeshErodeRadius,
        dilateRadiusPx);
    if (dilateRadiusPx <= 0.0f && erosionRadiusPx <= 0.0f)
        return;

    updateMorphParams(m_maskMorphDilateUbuf.get(), 0.0f, dilateRadiusPx);
    cb->beginPass(m_currentMaskWorkRt.get(), Qt::transparent, { 1.0f, 0 }, nullptr);
    cb->setGraphicsPipeline(m_maskMorphToWorkPipeline.get());
    cb->setViewport({ 0, 0, float(pixelSize.width()), float(pixelSize.height()) });
    cb->setShaderResources(m_maskMorphMaskToWorkSrb.get());
    cb->draw(3);
    cb->endPass();

    updateMorphParams(m_maskMorphErodeUbuf.get(), 1.0f, erosionRadiusPx);
    cb->beginPass(m_currentMaskRt.get(), Qt::transparent, { 1.0f, 0 }, nullptr);
    cb->setGraphicsPipeline(m_maskMorphWorkToMaskPipeline.get());
    cb->setViewport({ 0, 0, float(pixelSize.width()), float(pixelSize.height()) });
    cb->setShaderResources(m_maskMorphWorkToMaskSrb.get());
    cb->draw(3);
    cb->endPass();
}

void RenderWidget::drawCurrentMeshDebugView(QRhiCommandBuffer *cb, const QSize &pixelSize)
{
    if (!m_maskDebugPipeline || !m_maskDebugUbuf)
        return;

    QRhiShaderResourceBindings *srb = nullptr;
    switch (m_renderSettings.currentMeshDebugView) {
    case CurrentMeshDebugView::Outline:
        return;
    case CurrentMeshDebugView::BaseMask:
        srb = m_maskDebugBaseSrb.get();
        break;
    case CurrentMeshDebugView::DilatedMask:
        srb = m_currentMaskFromPoints ? m_maskDebugWorkSrb.get() : m_maskDebugBaseSrb.get();
        break;
    case CurrentMeshDebugView::ErodedMask:
        srb = m_maskDebugMaskSrb.get();
        break;
    }
    if (!srb)
        return;

    const float debugData[4] = {
        1.0f / float(qMax(1, pixelSize.width())),
        1.0f / float(qMax(1, pixelSize.height())),
        m_rhi->isYUpInFramebuffer() ? 1.0f : 0.0f,
        0.0f
    };
    QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
    u->updateDynamicBuffer(m_maskDebugUbuf.get(), 0, kMaskDebugUbufSize, debugData);
    cb->resourceUpdate(u);

    cb->setGraphicsPipeline(m_maskDebugPipeline.get());
    cb->setViewport({ 0, 0, float(pixelSize.width()), float(pixelSize.height()) });
    cb->setShaderResources(srb);
    cb->draw(3);
}

void RenderWidget::drawCurrentMeshOutline(QRhiCommandBuffer *cb, const QSize &pixelSize)
{
    if (!m_renderSettings.highlightCurrentMesh)
        return;

    if (m_renderSettings.currentMeshDebugView != CurrentMeshDebugView::Outline) {
        drawCurrentMeshDebugView(cb, pixelSize);
        return;
    }

    if (!m_outlinePipeline || !m_outlineSrb || !m_outlineUbuf)
        return;
    if (!m_currentMaskTexture)
        return;

    // For point-cloud highlighting, outline width is controlled by dilate/erode radius difference.
    // Keep edge extraction local and stable.
    const float widthPx = m_currentMaskFromPoints
        ? 1.0f
        : qMax(1.0f, m_renderSettings.currentMeshOutlineWidth);
    float outlineData[8] = {};
    outlineData[0] = m_renderSettings.currentMeshOutlineColor.redF();
    outlineData[1] = m_renderSettings.currentMeshOutlineColor.greenF();
    outlineData[2] = m_renderSettings.currentMeshOutlineColor.blueF();
    outlineData[3] = m_renderSettings.currentMeshOutlineColor.alphaF();
    outlineData[4] = widthPx;
    outlineData[5] = 1.0f / float(qMax(1, pixelSize.width()));
    outlineData[6] = 1.0f / float(qMax(1, pixelSize.height()));
    // Offscreen texture sampling needs a vertical flip on Y-up framebuffers.
    outlineData[7] = m_rhi->isYUpInFramebuffer() ? 1.0f : 0.0f;

    QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
    u->updateDynamicBuffer(m_outlineUbuf.get(), 0, kOutlineUbufSize, outlineData);
    cb->resourceUpdate(u);

    cb->setGraphicsPipeline(m_outlinePipeline.get());
    cb->setViewport({ 0, 0, float(pixelSize.width()), float(pixelSize.height()) });
    cb->setShaderResources(m_outlineSrb.get());
    cb->draw(3);
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

    const bool drawFillPass = m_renderSettings.showFill;
    const bool drawWirePass = m_renderSettings.showWire;
    const bool drawBBoxPass = m_renderSettings.showBoundingBox;
    const bool drawPointsPass = m_renderSettings.showPoints;
    const bool drawCurrentMeshHighlight =
        m_renderSettings.highlightCurrentMesh && (m_doc->currentMeshIndex() >= 0);
    const bool anyDrawPass =
        drawFillPass || drawWirePass || drawBBoxPass || drawPointsPass || drawCurrentMeshHighlight;

    if (anyDrawPass)
        prepareDirtyBuffers(cb);

    m_frameTimer.start();

    const QSize sz = renderTarget()->pixelSize();

    QRhiResourceUpdateBatch *u = nullptr;
    if (anyDrawPass) {
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

        // Pack uniform: mat4 mvp + mat4 modelView + mat3 as 3 vec4 (std140) + render colors/params.
        float ubufData[kUbufFloatCount] = {};
        memcpy(ubufData, mvp.constData(), 64);
        memcpy(ubufData + 16, modelView.constData(), 64);
        // std140: mat3 is stored as 3 columns of vec4
        const float *n = normalMat.constData();
        ubufData[32] = n[0]; ubufData[33] = n[1]; ubufData[34] = n[2]; ubufData[35] = 0;
        ubufData[36] = n[3]; ubufData[37] = n[4]; ubufData[38] = n[5]; ubufData[39] = 0;
        ubufData[40] = n[6]; ubufData[41] = n[7]; ubufData[42] = n[8]; ubufData[43] = 0;
        ubufData[kUbufBBoxColorOffset + 0] = m_renderSettings.bboxWireColor.redF();
        ubufData[kUbufBBoxColorOffset + 1] = m_renderSettings.bboxWireColor.greenF();
        ubufData[kUbufBBoxColorOffset + 2] = m_renderSettings.bboxWireColor.blueF();
        ubufData[kUbufBBoxColorOffset + 3] = m_renderSettings.bboxWireColor.alphaF();
        ubufData[kUbufPointColorOffset + 0] = m_renderSettings.pointColor.redF();
        ubufData[kUbufPointColorOffset + 1] = m_renderSettings.pointColor.greenF();
        ubufData[kUbufPointColorOffset + 2] = m_renderSettings.pointColor.blueF();
        ubufData[kUbufPointColorOffset + 3] = m_renderSettings.pointColor.alphaF();
        ubufData[kUbufPointParamsOffset + 0] = m_renderSettings.pointSize;
        ubufData[kUbufWireColorOffset + 0] = m_renderSettings.wireColor.redF();
        ubufData[kUbufWireColorOffset + 1] = m_renderSettings.wireColor.greenF();
        ubufData[kUbufWireColorOffset + 2] = m_renderSettings.wireColor.blueF();
        // Wire pass is intentionally translucent to compose independently over fill/effects.
        ubufData[kUbufWireColorOffset + 3] = m_renderSettings.wireColor.alphaF() * 0.7f;
        ubufData[kUbufWireParamsOffset + 0] = m_renderSettings.wireSize;
        ubufData[kUbufFillColorOffset + 0] = m_renderSettings.fillColor.redF();
        ubufData[kUbufFillColorOffset + 1] = m_renderSettings.fillColor.greenF();
        ubufData[kUbufFillColorOffset + 2] = m_renderSettings.fillColor.blueF();
        ubufData[kUbufFillColorOffset + 3] = m_renderSettings.fillColor.alphaF();
        // bbox lighting removed: slot 0 intentionally unused/reserved.
        ubufData[kUbufLightingParamsOffset + 0] = 0.0f;
        ubufData[kUbufLightingParamsOffset + 1] = m_renderSettings.pointLighting ? 1.0f : 0.0f;
        ubufData[kUbufLightingParamsOffset + 2] = m_renderSettings.wireLighting ? 1.0f : 0.0f;
        ubufData[kUbufLightingParamsOffset + 3] = m_renderSettings.fillLighting ? 1.0f : 0.0f;

        u = m_rhi->nextResourceUpdateBatch();
        u->updateDynamicBuffer(m_ubuf.get(), 0, kUbufSize, ubufData);
    }

    if (drawCurrentMeshHighlight) {
        if (u) {
            cb->resourceUpdate(u);
            u = nullptr;
        }
        renderCurrentMeshMask(cb, sz);
        processCurrentMeshMask(cb, sz);
    }

    const auto fillVariant = static_cast<Document::FillGpuVariant>(
        fillGpuVariantIndexForCurrentSettings());
    const auto pointVariant = static_cast<Document::PointGpuVariant>(
        pointGpuVariantIndexForCurrentSettings());

    cb->beginPass(renderTarget(), QColor(40, 40, 40), { 1.0f, 0 }, u);

    if (drawFillPass && m_fillPipeline) {
        cb->setGraphicsPipeline(m_fillPipeline.get());
        cb->setViewport({ 0, 0, float(sz.width()), float(sz.height()) });

        for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
            const auto &meshEntry = m_doc->mesh(mi);
            if (!meshEntry.visible)
                continue;
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

    if (drawWirePass && m_wirePipeline) {
        cb->setGraphicsPipeline(m_wirePipeline.get());
        cb->setViewport({ 0, 0, float(sz.width()), float(sz.height()) });
        cb->setShaderResources();

        for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
            const auto &meshEntry = m_doc->mesh(mi);
            if (!meshEntry.visible)
                continue;
            const Document::WirePassGpuView wireView = m_doc->wirePassGpuView(m_rhi, mi);
            if (!wireView.valid || !wireView.vertexBuffer || wireView.vertexCount <= 0)
                continue;
            const QRhiCommandBuffer::VertexInput vbufBinding(wireView.vertexBuffer, 0);
            cb->setVertexInput(0, 1, &vbufBinding);
            cb->draw(wireView.vertexCount);
        }
    }

    if (drawBBoxPass && m_bboxPipeline) {
        cb->setGraphicsPipeline(m_bboxPipeline.get());
        cb->setShaderResources();
        for (int mi = 0; mi < m_doc->meshCount(); ++mi) {
            const auto &meshEntry = m_doc->mesh(mi);
            if (!meshEntry.visible)
                continue;
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
            const auto &meshEntry = m_doc->mesh(mi);
            if (!meshEntry.visible)
                continue;
            const Document::PointsPassGpuView pointsView =
                m_doc->pointsPassGpuView(m_rhi, mi, pointVariant);
            if (!pointsView.valid || !pointsView.vertexBuffer || pointsView.vertexCount <= 0)
                continue;
            const QRhiCommandBuffer::VertexInput pv(pointsView.vertexBuffer, 0);
            cb->setVertexInput(0, 1, &pv);
            cb->draw(pointsView.vertexCount);
        }
    }

    if (drawCurrentMeshHighlight)
        drawCurrentMeshOutline(cb, sz);

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
