#include "renderwidget.h"
#include "document.h"
#include "renderoverlaypanel.h"
#include "renderwidget_internal.h"
#include <wrap/io_trimesh/io_mask.h>
#include <algorithm>

using namespace RenderWidgetInternal;

RenderWidget::MeshRenderMode RenderWidget::defaultRenderModeForMesh(int meshIndex) const
{
    MeshRenderMode mode;
    if (!m_doc || meshIndex < 0 || meshIndex >= m_doc->meshCount())
        return mode;

    const auto &entry = m_doc->mesh(meshIndex);
    const int faceCount = entry.mesh.FN();
    const int edgeCount = entry.mesh.EN();
    const int mask = entry.ioMask;
    const bool hasVertexColors = (mask & vcg::tri::io::Mask::IOM_VERTCOLOR) != 0;
    const bool hasFaceColors = (mask & vcg::tri::io::Mask::IOM_FACECOLOR) != 0;
    const bool hasVertexQuality = (mask & vcg::tri::io::Mask::IOM_VERTQUALITY) != 0;
    const bool hasFaceQuality = (mask & vcg::tri::io::Mask::IOM_FACEQUALITY) != 0;
    const bool hasVertexNormals = (mask & vcg::tri::io::Mask::IOM_VERTNORMAL) != 0;
    const bool hasTextureCoords =
        (mask & vcg::tri::io::Mask::IOM_WEDGTEXCOORD) != 0
        || (mask & vcg::tri::io::Mask::IOM_VERTTEXCOORD) != 0;
    const bool hasTextures = hasTextureCoords && !entry.textureFilePaths.isEmpty();

    if (faceCount > 0) {
        mode.showFill = true;
        mode.showWire = faceCount < kWireframeDefaultFaceThreshold;
        mode.showEdges = false;
        mode.showPoints = false;
        mode.fillColorSource = FillColorSource::Constant;
        if (hasTextures)
            mode.fillColorSource = FillColorSource::Texture;
        else if (hasVertexColors)
            mode.fillColorSource = FillColorSource::PerVertex;
        else if (hasFaceColors)
            mode.fillColorSource = FillColorSource::PerFace;
        else if (hasVertexQuality)
            mode.fillColorSource = FillColorSource::PerVertexQuality;
        else if (hasFaceQuality)
            mode.fillColorSource = FillColorSource::PerFaceQuality;
        mode.pointColorSource = hasVertexColors
            ? PointColorSource::PerVertex
            : (hasVertexQuality ? PointColorSource::PerVertexQuality : PointColorSource::Constant);
        mode.fillLighting = true;
        mode.pointLighting = false;
        mode.wireLighting = false;
    } else if (edgeCount > 0) {
        mode.showFill = false;
        mode.showWire = false;
        mode.showEdges = true;
        mode.showPoints = false;
        mode.edgeSize = 4.0f;
        mode.fillLighting = false;
        mode.pointLighting = false;
        mode.wireLighting = false;
    } else {
        mode.showFill = false;
        mode.showWire = false;
        mode.showEdges = false;
        mode.showPoints = true;
        mode.pointColorSource = hasVertexColors
            ? PointColorSource::PerVertex
            : (hasVertexQuality ? PointColorSource::PerVertexQuality : PointColorSource::Constant);
        mode.pointLighting = hasVertexNormals;
        mode.fillLighting = false;
        mode.wireLighting = false;
    }

    return mode;
}

void RenderWidget::syncPerMeshRenderModesWithDocument()
{
    if (!m_doc) {
        m_meshRenderModes.clear();
        return;
    }

    std::unordered_map<std::uint64_t, bool> aliveMeshIds;
    aliveMeshIds.reserve(size_t(m_doc->meshCount()));
    for (int i = 0; i < m_doc->meshCount(); ++i)
        aliveMeshIds.emplace(m_doc->mesh(i).meshId, true);

    for (auto it = m_meshRenderModes.begin(); it != m_meshRenderModes.end();) {
        if (aliveMeshIds.find(it->first) == aliveMeshIds.end())
            it = m_meshRenderModes.erase(it);
        else
            ++it;
    }

    for (int i = 0; i < m_doc->meshCount(); ++i) {
        const std::uint64_t meshId = m_doc->mesh(i).meshId;
        if (m_meshRenderModes.find(meshId) == m_meshRenderModes.end())
            m_meshRenderModes.emplace(meshId, defaultRenderModeForMesh(i));
    }
}

RenderWidget::MeshRenderMode RenderWidget::renderModeForMesh(int meshIndex) const
{
    if (!m_doc || meshIndex < 0 || meshIndex >= m_doc->meshCount())
        return MeshRenderMode {};
    const std::uint64_t meshId = m_doc->mesh(meshIndex).meshId;
    const auto it = m_meshRenderModes.find(meshId);
    if (it != m_meshRenderModes.end())
        return it->second;
    return defaultRenderModeForMesh(meshIndex);
}

RenderWidget::MeshRenderMode *RenderWidget::mutableRenderModeForMesh(int meshIndex)
{
    if (!m_doc || meshIndex < 0 || meshIndex >= m_doc->meshCount())
        return nullptr;
    const std::uint64_t meshId = m_doc->mesh(meshIndex).meshId;
    auto it = m_meshRenderModes.find(meshId);
    if (it == m_meshRenderModes.end()) {
        it = m_meshRenderModes
                 .emplace(meshId, defaultRenderModeForMesh(meshIndex))
                 .first;
    }
    return &it->second;
}

bool RenderWidget::applyRenderSettingsToCurrentMesh(
    const RenderSettings &prev,
    const RenderSettings &next)
{
    const int meshIndex = m_doc ? m_doc->currentMeshIndex() : -1;
    MeshRenderMode *mode = mutableRenderModeForMesh(meshIndex);
    if (!mode)
        return false;

    bool changed = false;
    auto apply = [&](bool MeshRenderMode::*field, bool before, bool after) {
        if (before == after)
            return;
        if (mode->*field == after)
            return;
        mode->*field = after;
        changed = true;
    };

    apply(&MeshRenderMode::showBoundingBox, prev.showBoundingBox, next.showBoundingBox);
    apply(&MeshRenderMode::showPoints, prev.showPoints, next.showPoints);
    apply(&MeshRenderMode::showEdges, prev.showEdges, next.showEdges);
    apply(&MeshRenderMode::showWire, prev.showWire, next.showWire);
    apply(&MeshRenderMode::showFill, prev.showFill, next.showFill);
    apply(
        &MeshRenderMode::decoratorVertexNormals,
        prev.decoratorVertexNormals,
        next.decoratorVertexNormals);
    apply(
        &MeshRenderMode::decoratorFaceNormals,
        prev.decoratorFaceNormals,
        next.decoratorFaceNormals);
    apply(
        &MeshRenderMode::decoratorBoundaryEdges,
        prev.decoratorBoundaryEdges,
        next.decoratorBoundaryEdges);
    apply(
        &MeshRenderMode::decoratorTextureSeams,
        prev.decoratorTextureSeams,
        next.decoratorTextureSeams);
    apply(&MeshRenderMode::pointLighting, prev.pointLighting, next.pointLighting);
    apply(&MeshRenderMode::wireLighting, prev.wireLighting, next.wireLighting);
    apply(
        &MeshRenderMode::wireBackfaceCulling,
        prev.wireBackfaceCulling,
        next.wireBackfaceCulling);
    apply(&MeshRenderMode::fillLighting, prev.fillLighting, next.fillLighting);
    apply(
        &MeshRenderMode::fillBackfaceCulling,
        prev.fillBackfaceCulling,
        next.fillBackfaceCulling);

    if (prev.fillShading != next.fillShading) {
        mode->fillShading = next.fillShading;
        changed = true;
    }
    if (prev.pointColorSource != next.pointColorSource) {
        mode->pointColorSource = next.pointColorSource;
        changed = true;
    }
    if (prev.fillColorSource != next.fillColorSource) {
        mode->fillColorSource = next.fillColorSource;
        changed = true;
    }

    auto applyColor = [&](QColor MeshRenderMode::*field, const QColor &before, const QColor &after) {
        if (before == after)
            return;
        if (mode->*field == after)
            return;
        mode->*field = after;
        changed = true;
    };
    auto applyFloat = [&](float MeshRenderMode::*field, float before, float after) {
        if (before == after)
            return;
        if (mode->*field == after)
            return;
        mode->*field = after;
        changed = true;
    };

    applyColor(&MeshRenderMode::decoratorVertexNormalColor, prev.decoratorVertexNormalColor, next.decoratorVertexNormalColor);
    applyColor(&MeshRenderMode::decoratorFaceNormalColor, prev.decoratorFaceNormalColor, next.decoratorFaceNormalColor);
    applyColor(&MeshRenderMode::decoratorBoundaryEdgeColor, prev.decoratorBoundaryEdgeColor, next.decoratorBoundaryEdgeColor);
    applyColor(&MeshRenderMode::decoratorTextureSeamColor, prev.decoratorTextureSeamColor, next.decoratorTextureSeamColor);
    applyFloat(
        &MeshRenderMode::decoratorBoundaryWidth,
        prev.decoratorBoundaryWidth,
        next.decoratorBoundaryWidth);
    applyColor(&MeshRenderMode::bboxWireColor, prev.bboxWireColor, next.bboxWireColor);
    applyColor(&MeshRenderMode::pointColor, prev.pointColor, next.pointColor);
    applyFloat(&MeshRenderMode::pointSize, prev.pointSize, next.pointSize);
    applyColor(&MeshRenderMode::edgeColor, prev.edgeColor, next.edgeColor);
    applyFloat(&MeshRenderMode::edgeSize, prev.edgeSize, next.edgeSize);
    applyColor(&MeshRenderMode::wireColor, prev.wireColor, next.wireColor);
    applyFloat(&MeshRenderMode::wireSize, prev.wireSize, next.wireSize);
    applyColor(&MeshRenderMode::fillColor, prev.fillColor, next.fillColor);

    return changed;
}

void RenderWidget::applyRenderModeToSettings(
    RenderSettings &settings,
    const MeshRenderMode &mode) const
{
    settings.showBoundingBox = mode.showBoundingBox;
    settings.showPoints = mode.showPoints;
    settings.showEdges = mode.showEdges;
    settings.showWire = mode.showWire;
    settings.showFill = mode.showFill;
    settings.decoratorVertexNormals = mode.decoratorVertexNormals;
    settings.decoratorFaceNormals = mode.decoratorFaceNormals;
    settings.decoratorBoundaryEdges = mode.decoratorBoundaryEdges;
    settings.decoratorTextureSeams = mode.decoratorTextureSeams;
    settings.pointLighting = mode.pointLighting;
    settings.wireLighting = mode.wireLighting;
    settings.wireBackfaceCulling = mode.wireBackfaceCulling;
    settings.fillLighting = mode.fillLighting;
    settings.fillBackfaceCulling = mode.fillBackfaceCulling;
    settings.fillShading = mode.fillShading;
    settings.pointColorSource = mode.pointColorSource;
    settings.fillColorSource = mode.fillColorSource;
    settings.decoratorVertexNormalColor = mode.decoratorVertexNormalColor;
    settings.decoratorFaceNormalColor = mode.decoratorFaceNormalColor;
    settings.decoratorBoundaryEdgeColor = mode.decoratorBoundaryEdgeColor;
    settings.decoratorTextureSeamColor = mode.decoratorTextureSeamColor;
    settings.decoratorBoundaryWidth = mode.decoratorBoundaryWidth;
    settings.bboxWireColor = mode.bboxWireColor;
    settings.pointColor = mode.pointColor;
    settings.pointSize = mode.pointSize;
    settings.edgeColor = mode.edgeColor;
    settings.edgeSize = mode.edgeSize;
    settings.wireColor = mode.wireColor;
    settings.wireSize = mode.wireSize;
    settings.fillColor = mode.fillColor;
}

RenderSettings RenderWidget::renderSettingsForMesh(int meshIndex) const
{
    RenderSettings meshSettings = m_renderSettings;
    if (meshIndex >= 0 && meshIndex < m_doc->meshCount())
        applyRenderModeToSettings(meshSettings, renderModeForMesh(meshIndex));
    return meshSettings;
}

void RenderWidget::syncOverlaySettingsToCurrentMesh()
{
    RenderSettings synced = m_renderSettings;
    const int meshIndex = m_doc ? m_doc->currentMeshIndex() : -1;
    if (meshIndex >= 0)
        applyRenderModeToSettings(synced, renderModeForMesh(meshIndex));
    m_renderSettings = synced;
    if (m_overlayPanel)
        m_overlayPanel->setSettings(m_renderSettings);
    updateBoundingBoxCornersOverlay();
}

void RenderWidget::refreshColorSourceAvailability()
{
    bool hasVertexColors = false;
    bool hasFaceColors = false;
    bool hasVertexQuality = false;
    bool hasFaceQuality = false;
    bool hasTextures = false;
    bool hasVertexNormals = false;
    const int meshIndex = m_doc ? m_doc->currentMeshIndex() : -1;
    if (meshIndex >= 0 && meshIndex < m_doc->meshCount()) {
        const auto &meshEntry = m_doc->mesh(meshIndex);
        const int mask = meshEntry.ioMask;
        const bool hasTextureCoords =
            (mask & vcg::tri::io::Mask::IOM_WEDGTEXCOORD) != 0
            || (mask & vcg::tri::io::Mask::IOM_VERTTEXCOORD) != 0;
        hasVertexColors = (mask & vcg::tri::io::Mask::IOM_VERTCOLOR) != 0;
        hasFaceColors = (mask & vcg::tri::io::Mask::IOM_FACECOLOR) != 0;
        hasVertexQuality = (mask & vcg::tri::io::Mask::IOM_VERTQUALITY) != 0;
        hasFaceQuality = (mask & vcg::tri::io::Mask::IOM_FACEQUALITY) != 0;
        hasTextures = hasTextureCoords && !meshEntry.textureFilePaths.isEmpty();
        hasVertexNormals = (mask & vcg::tri::io::Mask::IOM_VERTNORMAL) != 0;
    }

    if (m_overlayPanel)
        m_overlayPanel->setPointColorSourceAvailability(hasVertexColors, hasVertexQuality);
    if (m_overlayPanel)
        m_overlayPanel->setPointLightingAvailability(hasVertexNormals);
    if (m_overlayPanel)
        m_overlayPanel->setFillColorSourceAvailability(
            hasVertexColors,
            hasFaceColors,
            hasVertexQuality,
            hasFaceQuality,
            hasTextures);

    RenderSettings corrected = m_renderSettings;
    if (corrected.pointColorSource == PointColorSource::PerVertex && !hasVertexColors)
        corrected.pointColorSource = PointColorSource::Constant;
    if (corrected.pointColorSource == PointColorSource::PerVertexQuality && !hasVertexQuality)
        corrected.pointColorSource = PointColorSource::Constant;
    if (corrected.pointLighting && !hasVertexNormals)
        corrected.pointLighting = false;
    if (corrected.fillColorSource == FillColorSource::PerVertex && !hasVertexColors)
        corrected.fillColorSource = FillColorSource::Constant;
    if (corrected.fillColorSource == FillColorSource::PerFace && !hasFaceColors)
        corrected.fillColorSource = FillColorSource::Constant;
    if (corrected.fillColorSource == FillColorSource::PerVertexQuality && !hasVertexQuality)
        corrected.fillColorSource = FillColorSource::Constant;
    if (corrected.fillColorSource == FillColorSource::PerFaceQuality && !hasFaceQuality)
        corrected.fillColorSource = FillColorSource::Constant;
    if (corrected.fillColorSource == FillColorSource::Texture && !hasTextures)
        corrected.fillColorSource = FillColorSource::Constant;

    if (corrected != m_renderSettings) {
        const RenderSettings prev = m_renderSettings;
        m_renderSettings = corrected;
        applyRenderSettingsToCurrentMesh(prev, m_renderSettings);
        if (m_overlayPanel)
            m_overlayPanel->setSettings(m_renderSettings);
    }
}

int RenderWidget::fillGpuVariantIndexForSettings(const RenderSettings &settings) const
{
    switch (settings.fillColorSource) {
    case FillColorSource::PerVertex: return static_cast<int>(Document::FillGpuVariant::PerVertex);
    case FillColorSource::PerFace: return static_cast<int>(Document::FillGpuVariant::PerFace);
    case FillColorSource::PerVertexQuality:
        return static_cast<int>(Document::FillGpuVariant::PerVertexQuality);
    case FillColorSource::PerFaceQuality:
        return static_cast<int>(Document::FillGpuVariant::PerFaceQuality);
    case FillColorSource::Texture: return static_cast<int>(Document::FillGpuVariant::Texture);
    case FillColorSource::Constant:
    default:
        return static_cast<int>(Document::FillGpuVariant::Constant);
    }
}

int RenderWidget::pointGpuVariantIndexForSettings(const RenderSettings &settings) const
{
    switch (settings.pointColorSource) {
    case PointColorSource::PerVertex: return static_cast<int>(Document::PointGpuVariant::PerVertex);
    case PointColorSource::PerVertexQuality:
        return static_cast<int>(Document::PointGpuVariant::PerVertexQuality);
    case PointColorSource::Constant:
    default:
        return static_cast<int>(Document::PointGpuVariant::Constant);
    }
}

QRhiGraphicsPipeline *RenderWidget::fillPipelineForSettings(const RenderSettings &settings)
{
    if (!m_rhi || !m_srb || !renderTarget())
        return nullptr;
    const int key = int(settings.fillShading) * 2 + (settings.fillBackfaceCulling ? 1 : 0);
    auto it = m_fillPipelinesByKey.find(key);
    if (it != m_fillPipelinesByKey.end())
        return it->second.get();

    auto pipeline = std::unique_ptr<QRhiGraphicsPipeline>(m_rhi->newGraphicsPipeline());
    QString vsPath;
    QString fsPath;
    switch (settings.fillShading) {
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
    if (!vs.isValid() || !fs.isValid())
        return nullptr;

    pipeline->setShaderStages({
        { QRhiShaderStage::Vertex, vs },
        { QRhiShaderStage::Fragment, fs }
    });
    pipeline->setDepthTest(true);
    pipeline->setDepthWrite(true);
    pipeline->setCullMode(
        settings.fillBackfaceCulling
            ? QRhiGraphicsPipeline::Back
            : QRhiGraphicsPipeline::None);

    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({ { kFillVertexStrideFloats * sizeof(float) } });
    if (settings.fillShading == FillShading::Flat) {
        inputLayout.setAttributes({
            { 0, 0, QRhiVertexInputAttribute::Float3, 0 },
            { 0, 1, QRhiVertexInputAttribute::Float4, 6 * sizeof(float) },
            { 0, 2, QRhiVertexInputAttribute::Float3, 10 * sizeof(float) }
        });
    } else {
        inputLayout.setAttributes({
            { 0, 0, QRhiVertexInputAttribute::Float3, 0 },
            { 0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float) },
            { 0, 2, QRhiVertexInputAttribute::Float4, 6 * sizeof(float) },
            { 0, 3, QRhiVertexInputAttribute::Float3, 10 * sizeof(float) }
        });
    }
    pipeline->setVertexInputLayout(inputLayout);
    pipeline->setShaderResourceBindings(m_srb.get());
    pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    if (!pipeline->create())
        return nullptr;

    auto inserted = m_fillPipelinesByKey.emplace(key, std::move(pipeline));
    return inserted.first->second.get();
}

QRhiGraphicsPipeline *RenderWidget::wirePipelineForSettings(const RenderSettings &settings)
{
    if (!m_rhi || !m_srb || !renderTarget())
        return nullptr;
    const int key = settings.wireBackfaceCulling ? 1 : 0;
    auto it = m_wirePipelinesByKey.find(key);
    if (it != m_wirePipelinesByKey.end())
        return it->second.get();

    auto pipeline = std::unique_ptr<QRhiGraphicsPipeline>(m_rhi->newGraphicsPipeline());
    QShader vs = loadShader(QStringLiteral(":/shaders/fill_wire.vert.qsb"));
    QShader fs = loadShader(QStringLiteral(":/shaders/fill_wire_overlay.frag.qsb"));
    if (!vs.isValid() || !fs.isValid())
        return nullptr;

    pipeline->setShaderStages({
        { QRhiShaderStage::Vertex, vs },
        { QRhiShaderStage::Fragment, fs }
    });
    pipeline->setDepthTest(true);
    pipeline->setDepthWrite(false);
    pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
    pipeline->setCullMode(
        settings.wireBackfaceCulling
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
    pipeline->setTargetBlends({ blend });

    QRhiVertexInputLayout wireLayout;
    wireLayout.setBindings({ { 6 * sizeof(float) } });
    wireLayout.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float3, 0 },
        { 0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float) }
    });
    pipeline->setVertexInputLayout(wireLayout);
    pipeline->setShaderResourceBindings(m_srb.get());
    pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    if (!pipeline->create())
        return nullptr;

    auto inserted = m_wirePipelinesByKey.emplace(key, std::move(pipeline));
    return inserted.first->second.get();
}

QRhiGraphicsPipeline *RenderWidget::edgesPipelineForSettings(const RenderSettings &settings)
{
    if (!m_rhi || !m_srb || !renderTarget())
        return nullptr;
    const int key = int(std::lround(qMax(1.0f, settings.edgeSize) * 10.0f));
    auto it = m_edgesPipelinesByKey.find(key);
    if (it != m_edgesPipelinesByKey.end())
        return it->second.get();

    auto pipeline = std::unique_ptr<QRhiGraphicsPipeline>(m_rhi->newGraphicsPipeline());
    QShader vs = loadShader(QStringLiteral(":/shaders/overlay_edges.vert.qsb"));
    QShader fs = loadShader(QStringLiteral(":/shaders/overlay_edges.frag.qsb"));
    if (!vs.isValid() || !fs.isValid())
        return nullptr;

    pipeline->setShaderStages({
        { QRhiShaderStage::Vertex, vs },
        { QRhiShaderStage::Fragment, fs }
    });
    pipeline->setTopology(QRhiGraphicsPipeline::Lines);
    pipeline->setDepthTest(true);
    pipeline->setDepthWrite(true);
    pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
    pipeline->setDepthBias(-1);
    pipeline->setSlopeScaledDepthBias(-1.0f);
    pipeline->setCullMode(QRhiGraphicsPipeline::None);
    pipeline->setLineWidth(qMax(1.0f, settings.edgeSize));
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.opColor = QRhiGraphicsPipeline::Add;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.opAlpha = QRhiGraphicsPipeline::Add;
    pipeline->setTargetBlends({ blend });

    QRhiVertexInputLayout edgesLayout;
    edgesLayout.setBindings({ { 3 * sizeof(float) } });
    edgesLayout.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, 0 } });
    pipeline->setVertexInputLayout(edgesLayout);
    pipeline->setShaderResourceBindings(m_srb.get());
    pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    if (!pipeline->create())
        return nullptr;

    auto inserted = m_edgesPipelinesByKey.emplace(key, std::move(pipeline));
    return inserted.first->second.get();
}

QRhiGraphicsPipeline *RenderWidget::fatEdgesPipelineForSettings(const RenderSettings &settings)
{
    if (!m_rhi || !m_srb || !renderTarget())
        return nullptr;
    const int key = int(std::lround(qMax(1.0f, settings.edgeSize) * 10.0f));
    auto it = m_fatEdgesPipelinesByKey.find(key);
    if (it != m_fatEdgesPipelinesByKey.end())
        return it->second.get();

    auto pipeline = std::unique_ptr<QRhiGraphicsPipeline>(m_rhi->newGraphicsPipeline());
    QShader vs = loadShader(QStringLiteral(":/shaders/overlay_fat_edges.vert.qsb"));
    QShader fs = loadShader(QStringLiteral(":/shaders/overlay_fat_edges.frag.qsb"));
    if (!vs.isValid() || !fs.isValid())
        return nullptr;

    pipeline->setShaderStages({
        { QRhiShaderStage::Vertex, vs },
        { QRhiShaderStage::Fragment, fs }
    });
    pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
    pipeline->setDepthTest(true);
    pipeline->setDepthWrite(true);
    pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
    pipeline->setDepthBias(-1);
    pipeline->setSlopeScaledDepthBias(-1.0f);
    pipeline->setCullMode(QRhiGraphicsPipeline::None);
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.opColor = QRhiGraphicsPipeline::Add;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.opAlpha = QRhiGraphicsPipeline::Add;
    pipeline->setTargetBlends({ blend });

    QRhiVertexInputLayout edgesLayout;
    edgesLayout.setBindings({ { 8 * sizeof(float) } });
    edgesLayout.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float3, 0 },
        { 0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float) },
        { 0, 2, QRhiVertexInputAttribute::Float, 6 * sizeof(float) },
        { 0, 3, QRhiVertexInputAttribute::Float, 7 * sizeof(float) }
    });
    pipeline->setVertexInputLayout(edgesLayout);
    pipeline->setShaderResourceBindings(m_srb.get());
    pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    if (!pipeline->create())
        return nullptr;

    auto inserted = m_fatEdgesPipelinesByKey.emplace(key, std::move(pipeline));
    return inserted.first->second.get();
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
