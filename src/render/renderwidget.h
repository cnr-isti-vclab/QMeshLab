#pragma once

#include "renderingsettings.h"
#include "renderwidget_internal.h"
#include "meshgpuresourcecache.h"
#include "camerashot.h"
#include "viewtrackball.h"
#include "viewstate.h"
#include <QRhiWidget>
#include <rhi/qrhi.h>
#include <QElapsedTimer>
#include <QMatrix3x3>
#include <QMatrix4x4>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QVector2D>
#include <array>
#include <cstdint>
#include <memory>
#include <functional>
#include <unordered_map>
#include <vector>

class Document;
class RenderOverlayPanel;
class QLabel;
class QTimer;

struct PeerViewCamera {
    QMatrix4x4 view;
    QMatrix4x4 proj;
    QSize viewportSize;
    float nearDist = 0.0f;
    float farDist = 0.0f;
};

class RenderWidget : public QRhiWidget
{
    Q_OBJECT
public:
    enum class ViewMode {
        Scene3D,
        ParametrizationUV,
        RasterImage
    };

    explicit RenderWidget(Document *doc, QWidget *parent = nullptr);
    void setCurrentViewHighlighted(bool highlighted);
    void resetCameraToScene();
    const RenderSettings &renderSettings() const { return m_renderSettings; }
    void setRenderSettings(const RenderSettings &settings);
    QString cameraStateJson() const;
    bool applyCameraStateJson(const QString &jsonText, QString *errorMessage = nullptr);
    QString renderStateJson() const;
    bool applyRenderStateJson(const QString &jsonText, QString *errorMessage = nullptr);
    ViewMode viewMode() const { return m_viewMode; }
    bool canSwitchToViewMode(ViewMode mode, QString *errorMessage = nullptr) const;
    bool setViewMode(ViewMode mode, QString *errorMessage = nullptr);
    bool meshVisible(int index) const;
    void setMeshVisible(int index, bool visible);
    std::vector<bool> meshVisibilityState() const { return m_meshVisibility; }
    void setMeshVisibilityState(const std::vector<bool> &visibility);
    void copyPerMeshRenderModesFrom(const RenderWidget *other);
    bool helpOverlayVisible() const { return m_helpOverlayVisible; }
    void setHelpOverlayVisible(bool visible);
    void toggleHelpOverlayVisible() { setHelpOverlayVisible(!m_helpOverlayVisible); }
    void showQualityVisualization(int meshIndex, bool faceQuality);

    ViewState captureViewState() const;
    void restoreViewState(const ViewState &vs, bool restoreCamera = true);
    ViewTrackball::State trackballState() const { return m_trackball.state(); }
    void applySynchronizedTrackballState(const ViewTrackball::State &state);
    QVector3D trackballCenter() const { return m_trackball.center(); }
    QVector3D cameraEyePosition() const { return m_trackball.cameraEyePosition(); }
    QVector3D cameraViewDirection() const { return m_trackball.cameraViewDirection(); }
    CameraShot cameraShotForViewport(const QSize &pixelSize) const;
    // Offscreen render — blocks until one frame is ready, then grabs the framebuffer.
    // Set transparentBackground=true to clear the color buffer to (0,0,0,0).
    QImage renderOffscreenToImage(const QSize &pixelSize,
                                   bool transparentBackground = false,
                                   QString *errorMessage = nullptr);
    void setPeerViewCameraProvider(std::function<std::vector<PeerViewCamera>()> provider);
    bool showViewFrustumsEnabled() const { return m_renderSettings.showViewCameras; }
    float nearClipDistance() const { return m_trackball.nearClipPlaneDistance(); }
    float farClipDistance() const { return m_trackball.farClipPlaneDistance(); }
    QMatrix4x4 viewMatrix() const { return m_trackball.viewMatrix(); }
    QMatrix4x4 projectionMatrix() const {
        QSize sz = size();
        return m_trackball.projectionMatrix(float(sz.width()) / float(sz.height()));
    }

signals:
    void frameRendered(float cpuMs, float gpuMs, bool gpuTimingSupported, bool gpuSampleValid);
    void trackballCenterPicked(const QVector3D &worldPos);
    void viewActivated(RenderWidget *view);
    void cameraStateChanged(RenderWidget *view);

protected:
    void initialize(QRhiCommandBuffer *cb) override;
    void render(QRhiCommandBuffer *cb) override;
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    // MeshRenderMode is now the public PerMeshRenderSettings type.
    using MeshRenderMode = PerMeshRenderSettings;
    using MainUbufMaterialOverrides = RenderWidgetInternal::MainUbufMaterialOverrides;
    class FillRenderServices;
    class FillMaterialRenderer;
    class PlainFillRenderer;
    class PbrFillRenderer;
    class RadianceScalingFillRenderer;
    struct SceneFillDrawItem {
        int meshIndex;
        QRhiGraphicsPipeline *pipeline = nullptr;
        const FillMaterialRenderer *materialRenderer = nullptr;
        PerMeshRenderSettings meshSettings;
        MeshGpuResourceCache::FillPassView fillView;
    };
    struct SceneFillFramePlan {
        QSize pixelSize;
        QMatrix4x4 proj;
        QMatrix4x4 view;
        QVector3D lightDir;
        std::vector<SceneFillDrawItem> fillItems;

        bool hasDrawItems() const { return !fillItems.empty(); }
    };
    struct SceneBufferDrawItem {
        int meshIndex = -1;
        QRhiGraphicsPipeline *pipeline = nullptr;
        PerMeshRenderSettings meshSettings;
        QRhiBuffer *vertexBuffer = nullptr;
        int vertexCount = 0;
    };
    struct SceneRasterBackplateDrawItem {
        int rasterIndex = -1;
        QSize imageSize;
        QRhiShaderResourceBindings *srb = nullptr;
        bool fitToViewport = true;
    };
struct SceneRasterProjectedDrawItem {
    int rasterIndex = -1;
    QRhiShaderResourceBindings *srb = nullptr;
    QRhiBuffer *vertexBuffer = nullptr;
    int vertexCount = 0;
    bool isCurrent = false;
};
    struct SceneSelectionDrawItem {
        int meshIndex = -1;
        bool drawFaces = false;
        bool drawVertices = false;
        MeshGpuResourceCache::SelectionPassView selectionView;
    };
    enum class SceneDecoratorDrawKind {
        Line,
        FatLine,
        Point
    };
    struct SceneDecoratorDrawItem {
        int meshIndex = -1;
        int slot = -1;
        SceneDecoratorDrawKind kind = SceneDecoratorDrawKind::Line;
        QColor color;
        float width = 1.0f;
        QRhiBuffer *vertexBuffer = nullptr;
        int vertexCount = 0;
    };
    struct SceneFillFrameContext {
        const FillRenderServices &services;
        QRhiCommandBuffer *cb;
        const QSize &pixelSize;
        const QMatrix4x4 &proj;
        const QMatrix4x4 &view;
        const QVector3D &lightDir;
    };
    struct SceneFillDrawContext {
        const SceneFillFrameContext &frame;
        const SceneFillDrawItem &item;
    };
    struct RenderFramePlan {
        ViewMode viewMode = ViewMode::Scene3D;
        QSize pixelSize;
        QMatrix4x4 proj;
        QMatrix4x4 view;
        QVector3D lightDir;
        float rasterOpacity = 1.0f;
        float rasterZoom = 1.0f;
        QVector2D rasterPan = QVector2D(0.5f, 0.5f);
        SceneFillFramePlan sceneFill;
        std::vector<SceneRasterBackplateDrawItem> rasterBackplateItems;
        std::vector<SceneRasterProjectedDrawItem> rasterProjectedItems;
        std::vector<SceneBufferDrawItem> wireItems;
        std::vector<SceneBufferDrawItem> edgeItems;
        std::vector<SceneBufferDrawItem> boundingBoxItems;
        std::vector<SceneBufferDrawItem> pointItems;
        std::vector<SceneSelectionDrawItem> selectionItems;
        std::vector<SceneDecoratorDrawItem> decoratorItems;

        bool hasFillPass() const { return sceneFill.hasDrawItems(); }
        bool hasRasterBackplatePass() const { return !rasterBackplateItems.empty(); }
        bool hasRasterProjectedPass() const { return !rasterProjectedItems.empty(); }
        bool hasWirePass() const { return !wireItems.empty(); }
        bool hasEdgesPass() const { return !edgeItems.empty(); }
        bool hasBoundingBoxPass() const { return !boundingBoxItems.empty(); }
        bool hasPointsPass() const { return !pointItems.empty(); }
        bool hasSelectionPass() const { return !selectionItems.empty(); }
        bool hasDecoratorPass() const { return !decoratorItems.empty(); }

        bool hasSceneDrawItems() const
        {
            return hasFillPass() || hasWirePass() || hasEdgesPass() || hasBoundingBoxPass()
                || hasPointsPass() || hasSelectionPass() || hasDecoratorPass()
                || hasRasterBackplatePass() || hasRasterProjectedPass();
        }
    };
    struct RenderMeshPassRequests {
        int meshIndex = -1;
        PerMeshRenderSettings meshSettings;
        bool fill = false;
        bool wire = false;
        bool edges = false;
        bool boundingBox = false;
        bool points = false;
        bool selection = false;
        bool decoratorNormals = false;
        bool decoratorBoundaries = false;

        bool decorators() const { return decoratorNormals || decoratorBoundaries; }

        bool hasMeshResourceRequests() const
        {
            return fill || wire || edges || boundingBox || points || selection || decorators();
        }
    };
    struct RenderFramePassRequests {
        std::vector<RenderMeshPassRequests> meshes;
        std::vector<int> rasterBackplates;
        std::vector<int> rasterProjected;
        bool fill = false;
        bool wire = false;
        bool edges = false;
        bool boundingBox = false;
        bool points = false;
        bool selection = false;
        bool decoratorNormals = false;
        bool decoratorBoundaries = false;

        bool hasVisibleMeshes() const { return !meshes.empty(); }
        bool hasRasterBackplates() const { return !rasterBackplates.empty(); }
        bool hasRasterProjected() const { return !rasterProjected.empty(); }
        bool hasRasters() const { return hasRasterBackplates() || hasRasterProjected(); }
        bool decorators() const { return decoratorNormals || decoratorBoundaries; }
        bool hasSimpleBufferRequests() const
        {
            return wire || edges || boundingBox || points;
        }
        bool hasMeshResourceRequests() const
        {
            return fill || wire || edges || boundingBox || points || selection || decorators();
        }
    };
    struct RenderFrameRequest {
        ViewMode viewMode = ViewMode::Scene3D;
        QSize pixelSize;
        QMatrix4x4 proj;
        QMatrix4x4 view;
        QVector3D lightDir;
        float rasterOpacity = 1.0f;
        float rasterZoom = 1.0f;
        QVector2D rasterPan = QVector2D(0.5f, 0.5f);
        RenderFramePassRequests passes;
    };

    void createOverlayButtons();
    void layoutOverlayButtons();
    void showInteractionStatusOverlay(const QString &text);
    void emitCameraStateChangedIfNeeded();
    bool computeVisibleSceneBoundingBox(QVector3D &minCorner, QVector3D &maxCorner) const;
    void updateBoundingBoxCornersOverlay();
    void updateBoundingBoxCornersOverlayPlacement(
        const QMatrix4x4 &mvp,
        const QMatrix4x4 &view,
        const QSize &pixelSize);
    void updateQualityHistogramOverlay();
    void bakeCurrentQualityMappingToVertexColor();
    void updateUvScaleOverlay(
        const QMatrix4x4 &mvp,
        const QSize &pixelSize,
        bool showUvReference);
    MeshRenderMode defaultRenderModeForMesh(int meshIndex) const;
    void syncPerMeshRenderModesWithDocument();
    MeshRenderMode renderModeForMesh(int meshIndex) const;
    MeshRenderMode *mutableRenderModeForMesh(int meshIndex);
    void setCurrentMeshSettings(const PerMeshRenderSettings &next);
    void syncOverlaySettingsToCurrentMesh();
    void refreshColorSourceAvailability();
    void ensureRenderResources();
    void ensureCurrentMeshMaskResources(const QSize &pixelSize);
    void ensureDepthPickResources(const QSize &pixelSize);
    void ensureRsGradResources(const QSize &pixelSize);
    void prepareDirtyBuffers(QRhiCommandBuffer *cb);
    void prepareDirtyBuffers(
        QRhiCommandBuffer *cb,
        const RenderFramePassRequests &requests,
        int currentMeshIndex,
        bool drawCurrentMeshHighlight);
    quint32 uploadMainUbuf(
        QRhiCommandBuffer *cb,
        const QMatrix4x4 &mvp,
        const QMatrix4x4 &modelView,
        const QMatrix3x3 &normalMat,
        const PerMeshRenderSettings &settings,
        const QSize &pixelSize,
        bool enableLighting,
        const QVector3D &lightDir = QVector3D(0.0f, 0.0f, 1.0f),
        MainUbufMaterialOverrides materialOverrides = MainUbufMaterialOverrides{},
        quint32 offset = 0);
    quint32 uploadMainUbufForMesh(
        QRhiCommandBuffer *cb,
        int meshIndex,
        const QMatrix4x4 &proj,
        const QMatrix4x4 &view,
        const PerMeshRenderSettings &meshSettings,
        const QSize &pixelSize,
        bool enableLighting,
        const QVector3D &lightDir = QVector3D(0.0f, 0.0f, 1.0f),
        MainUbufMaterialOverrides materialOverrides = MainUbufMaterialOverrides{},
        quint32 offset = 0);
    SceneFillFramePlan buildSceneFillFramePlan(
        const RenderFrameRequest &request);
    RenderFramePlan buildRenderFramePlan(
        const RenderFrameRequest &request);
    RenderFramePassRequests collectRenderFramePassRequests() const;
    void planSimpleBufferPasses(
        const RenderFramePassRequests &requests,
        RenderFramePlan &plan);
    void planRasterBackplatePasses(
        const RenderFramePassRequests &requests,
        RenderFramePlan &plan);
    void planRasterProjectedPasses(
        const RenderFramePassRequests &requests,
        RenderFramePlan &plan);
    void planViewFrustumPasses(RenderFramePlan &plan);
    void planDecoratorPasses(
        const RenderFramePassRequests &requests,
        RenderFramePlan &plan);
    void planSelectionPasses(
        const RenderFramePassRequests &requests,
        RenderFramePlan &plan);
    void renderSceneFillPrepasses(
        QRhiCommandBuffer *cb,
        const RenderFramePlan &plan);
    void renderSceneFillPass(
        QRhiCommandBuffer *cb,
        const RenderFramePlan &plan);
    void renderSceneRasterBackplates(
        QRhiCommandBuffer *cb,
        const RenderFramePlan &plan);
    void renderSceneRasterProjected(
        QRhiCommandBuffer *cb,
        const RenderFramePlan &plan);
    void renderSceneBufferItems(
        QRhiCommandBuffer *cb,
        const RenderFramePlan &plan,
        const std::vector<SceneBufferDrawItem> &items);
    void renderSceneDecoratorItems(
        QRhiCommandBuffer *cb,
        const RenderFramePlan &plan);
    void renderSceneSelectionItems(
        QRhiCommandBuffer *cb,
        const RenderFramePlan &plan);
    void startCenterAnimation(const QVector3D &targetCenter);
    void cancelCenterAnimation();
    void advanceCenterAnimation();
    void updateCameraFrameIfNeeded();
    void ensureVisibilitySize();
    int fillGpuVariantIndexForSettings(const PerMeshRenderSettings &settings) const;
    int pointGpuVariantIndexForSettings(const PerMeshRenderSettings &settings) const;
    QRhiGraphicsPipeline *fillPipelineForSettings(const PerMeshRenderSettings &settings);
    QRhiGraphicsPipeline *wirePipelineForSettings(const PerMeshRenderSettings &settings);
    QRhiGraphicsPipeline *edgesPipelineForSettings(const PerMeshRenderSettings &settings);
    QRhiGraphicsPipeline *fatEdgesPipelineForSettings(const PerMeshRenderSettings &settings);
    QRhiShaderResourceBindings *shaderResourcesForFillTextures(
        QRhiTexture *baseColorTexture,
        QRhiTexture *normalTexture,
        QRhiTexture *occlusionTexture,
        QRhiTexture *roughnessTexture,
        bool nearest = false);
    QRhiShaderResourceBindings *shaderResourcesForTexture(QRhiTexture *texture);
    struct DynamicUbufAllocator {
        quint32 stride = 0;
        quint32 nextOffset = 0;
        int capacity = 0;

        quint32 byteSize() const
        {
            return stride * quint32(capacity > 0 ? capacity : 0);
        }
    };
    void resetDynamicUbufAllocators();
    quint32 allocateDynamicUbufOffset(DynamicUbufAllocator &allocator, const char *debugName);
    void setShaderResourcesWithOffset(
        QRhiCommandBuffer *cb,
        QRhiShaderResourceBindings *srb,
        quint32 offset);
    QRhiTexture *resolveSelectedPbrTexture(
        int meshIndex,
        int textureIndex,
        const MeshGpuResourceCache::FillPassView &fillView) const;
    void executePendingDepthPick(
        QRhiCommandBuffer *cb,
        const QSize &pixelSize);
    void renderCurrentMeshMask(QRhiCommandBuffer *cb, const QSize &pixelSize);
    void processCurrentMeshMask(QRhiCommandBuffer *cb, const QSize &pixelSize);
    void drawCurrentMeshDebugView(QRhiCommandBuffer *cb, const QSize &pixelSize);
    void drawCurrentMeshOutline(QRhiCommandBuffer *cb, const QSize &pixelSize);
    void syncRasterCacheWithDocument();
    void ensureRasterResources(
        QRhiCommandBuffer *cb,
        const RenderFramePassRequests &requests);
    QSize currentRasterImageSize() const;
    QVector2D rasterScreenToImage(
        const QPointF &screenPos,
        const QSize &pixelSize) const;
    void resetRasterView();
    void syncUvCacheWithDocument();
    bool meshHasParametrization(int meshIndex) const;
    bool ensureUvMeshResources(int meshIndex, QRhiCommandBuffer *cb);
    void fitUvViewToCurrentMesh(const QSize &pixelSize);
    void renderParametrization(QRhiCommandBuffer *cb);

    Document *m_doc;
    QRhi *m_rhi = nullptr;
    std::function<std::vector<PeerViewCamera>()> m_peerViewCameraProvider;
    mutable std::vector<float> m_viewFrustumVertices;
    mutable size_t m_viewFrustumCount = 0;
    mutable std::unique_ptr<QRhiBuffer> m_viewFrustumVbuf;
    mutable std::unique_ptr<QRhiBuffer> m_viewFrustumUbuf;
    mutable std::unique_ptr<QRhiShaderResourceBindings> m_viewFrustumSrb;
    bool m_reframeCameraRequested = true;
    bool m_resetTrackballRequested = false;
    bool m_centerAnimActive = false;
    QVector3D m_centerAnimStart;
    QVector3D m_centerAnimTarget;
    QElapsedTimer m_centerAnimTimer;
    int m_centerAnimDurationMs = 200;

    std::unique_ptr<QRhiBuffer> m_ubuf;
    std::unique_ptr<QRhiSampler> m_textureSampler;
    std::unique_ptr<QRhiSampler> m_textureSamplerNearest;
    std::unique_ptr<QRhiSampler> m_rasterSampler;
    std::unique_ptr<QRhiTexture> m_fallbackTexture;
    bool m_fallbackTextureUploadPending = false;
    std::unique_ptr<QRhiTexture> m_fallbackNormalTexture;
    bool m_fallbackNormalTextureUploadPending = false;
    std::unique_ptr<QRhiTexture> m_fallbackOcclusionTexture;
    bool m_fallbackOcclusionTextureUploadPending = false;
    std::unique_ptr<QRhiTexture> m_fallbackRoughnessTexture;
    bool m_fallbackRoughnessTextureUploadPending = false;
    std::unique_ptr<QRhiTexture> m_qualityColorMapTexture;
    bool m_qualityColorMapTextureUploadPending = false;
    QString m_qualityColorMapTextureMapId;
    bool m_qualityColorMapTextureInverted = false;
    bool m_qualityColorMapTextureIsolinesEnabled = false;
    int m_qualityColorMapTextureIsolineCount = 0;
    DynamicUbufAllocator m_mainUbufAllocator;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unique_ptr<QRhiBuffer> m_sceneBackgroundUbuf;
    std::unique_ptr<QRhiShaderResourceBindings> m_sceneBackgroundSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_sceneBackgroundPipeline;
    std::unique_ptr<QRhiBuffer> m_rasterBackplateUbuf;
    std::unique_ptr<QRhiShaderResourceBindings> m_rasterBackplateFallbackSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_rasterBackplatePipeline;
    std::unique_ptr<QRhiBuffer> m_rasterProjectedUbuf;
    DynamicUbufAllocator m_rasterProjectedUbufAllocator;
    std::unique_ptr<QRhiShaderResourceBindings> m_rasterProjectedFallbackSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_rasterProjectedPipeline;
    struct RasterGpu {
        std::uint64_t imageRevision = 0;
        int planeIndex = -1;
        QSize size;
        std::unique_ptr<QRhiTexture> texture;
        std::unique_ptr<QRhiShaderResourceBindings> backplateSrb;
        std::unique_ptr<QRhiShaderResourceBindings> projectedSrb;
        std::unique_ptr<QRhiBuffer> projectedVbuf;
        int projectedVertexCount = 0;
    };
    std::unordered_map<std::uint64_t, RasterGpu> m_rastersGpu;
    struct FillTextureSetKey {
        QRhiTexture *baseColorTexture = nullptr;
        QRhiTexture *normalTexture = nullptr;
        QRhiTexture *occlusionTexture = nullptr;
        QRhiTexture *roughnessTexture = nullptr;
        bool nearest = false;

        bool operator==(const FillTextureSetKey &other) const
        {
            return baseColorTexture == other.baseColorTexture
                && normalTexture == other.normalTexture
                && occlusionTexture == other.occlusionTexture
                && roughnessTexture == other.roughnessTexture
                && nearest == other.nearest;
        }
    };
    struct FillTextureSetKeyHash {
        size_t operator()(const FillTextureSetKey &key) const noexcept
        {
            auto h = [](QRhiTexture *ptr) -> size_t {
                return std::hash<std::uintptr_t>{}(std::uintptr_t(ptr));
            };
            const size_t b = h(key.baseColorTexture);
            const size_t n = h(key.normalTexture);
            const size_t o = h(key.occlusionTexture);
            const size_t r = h(key.roughnessTexture);
            size_t seed = b;
            seed ^= n + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= o + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= r + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= (key.nearest ? size_t(1) : size_t(0)) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };
    std::unordered_map<
        FillTextureSetKey,
        std::unique_ptr<QRhiShaderResourceBindings>,
        FillTextureSetKeyHash> m_textureSrbs;
    std::unordered_map<int, std::unique_ptr<QRhiGraphicsPipeline>> m_fillPipelinesByKey;
    std::unordered_map<int, std::unique_ptr<QRhiGraphicsPipeline>> m_wirePipelinesByKey;
    std::unordered_map<int, std::unique_ptr<QRhiGraphicsPipeline>> m_edgesPipelinesByKey;
    std::unordered_map<int, std::unique_ptr<QRhiGraphicsPipeline>> m_fatEdgesPipelinesByKey;
    std::unique_ptr<QRhiGraphicsPipeline> m_bboxPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_pointsPipeline;
    std::array<std::unique_ptr<QRhiBuffer>, RenderWidgetInternal::kDecoratorSlotCount> m_decoratorUbufs;
    std::array<DynamicUbufAllocator, RenderWidgetInternal::kDecoratorSlotCount> m_decoratorUbufAllocators;
    std::array<std::unique_ptr<QRhiShaderResourceBindings>, RenderWidgetInternal::kDecoratorSlotCount> m_decoratorSrbs;
    std::unique_ptr<QRhiGraphicsPipeline> m_decoratorPipeline;
    std::unique_ptr<QRhiBuffer> m_selectionUbuf;
    DynamicUbufAllocator m_selectionUbufAllocator;
    std::unique_ptr<QRhiShaderResourceBindings> m_selectionSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_selectionFacesPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_selectionVerticesPipeline;
    std::unique_ptr<QRhiBuffer> m_decoratorFatUbuf;
    DynamicUbufAllocator m_decoratorFatUbufAllocator;
    std::unique_ptr<QRhiShaderResourceBindings> m_decoratorFatSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_decoratorFatPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_decoratorPointPipeline;
    // Radiance Scaling gradient buffer (pass 1 pre-pass)
    std::unique_ptr<QRhiTexture> m_rsGradTexture;          // RGBA32F (gx,gy,logZ,1)
    std::unique_ptr<QRhiRenderBuffer> m_rsGradDepth;
    std::unique_ptr<QRhiTextureRenderTarget> m_rsGradRt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_rsGradRp;
    std::unique_ptr<QRhiShaderResourceBindings> m_rsGradSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_rsGradPipeline;
    QSize m_rsGradSize;

    std::unique_ptr<QRhiTexture> m_depthPickTexture;    std::unique_ptr<QRhiRenderBuffer> m_depthPickDepth;
    std::unique_ptr<QRhiTextureRenderTarget> m_depthPickRt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_depthPickRp;
    QSize m_depthPickSize;
    std::unique_ptr<QRhiShaderResourceBindings> m_depthPickSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_depthPickFillPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_depthPickPointsPipeline;
    bool m_depthPickPending = false;
    bool m_depthPickInFlight = false;
    int  m_depthPickSequence = 0;       /* monotonically incremented on each new pick */
    QPoint m_depthPickPos;
    std::unique_ptr<QRhiReadbackResult> m_depthPickReadbackResult;
    std::unique_ptr<QRhiTexture> m_currentMaskTexture;
    std::unique_ptr<QRhiRenderBuffer> m_currentMaskDepth;
    std::unique_ptr<QRhiTextureRenderTarget> m_currentMaskRt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_currentMaskRp;
    std::unique_ptr<QRhiTexture> m_currentMaskBaseTexture;
    std::unique_ptr<QRhiTextureRenderTarget> m_currentMaskBaseRt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_currentMaskBaseRp;
    std::unique_ptr<QRhiTexture> m_currentMaskWorkTexture;
    std::unique_ptr<QRhiTextureRenderTarget> m_currentMaskWorkRt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_currentMaskWorkRp;
    QSize m_currentMaskSize;
    std::unique_ptr<QRhiGraphicsPipeline> m_currentMaskFillPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_currentMaskFillDepthOnlyPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_currentMaskEdgesPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_currentMaskEdgesDepthPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_currentMaskEdgesDepthOnlyPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_currentMaskFatEdgesDepthOnlyPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_currentMaskPointsPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_currentMaskPointsDepthOnlyPipeline;
    std::unique_ptr<QRhiBuffer> m_maskMorphCopyUbuf;
    std::unique_ptr<QRhiBuffer> m_maskMorphDilateUbuf;
    std::unique_ptr<QRhiBuffer> m_maskMorphErodeUbuf;
    std::unique_ptr<QRhiSampler> m_maskMorphSampler;
    std::unique_ptr<QRhiShaderResourceBindings> m_maskMorphMaskToBaseSrb;
    std::unique_ptr<QRhiShaderResourceBindings> m_maskMorphMaskToWorkSrb;
    std::unique_ptr<QRhiShaderResourceBindings> m_maskMorphWorkToMaskSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_maskMorphToBasePipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_maskMorphToWorkPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_maskMorphWorkToMaskPipeline;
    std::unique_ptr<QRhiBuffer> m_outlineExtractUbuf;
    std::unique_ptr<QRhiShaderResourceBindings> m_outlineExtractSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_outlineExtractPipeline;
    std::unique_ptr<QRhiBuffer> m_maskDebugUbuf;
    std::unique_ptr<QRhiShaderResourceBindings> m_maskDebugBaseSrb;
    std::unique_ptr<QRhiShaderResourceBindings> m_maskDebugWorkSrb;
    std::unique_ptr<QRhiShaderResourceBindings> m_maskDebugMaskSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_maskDebugPipeline;
    std::unique_ptr<QRhiBuffer> m_outlineUbuf;
    std::unique_ptr<QRhiSampler> m_outlineSampler;
    std::unique_ptr<QRhiShaderResourceBindings> m_outlineSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_outlinePipeline;
    std::unique_ptr<QRhiBuffer> m_trackballGizmoUbuf;
    std::unique_ptr<QRhiBuffer> m_trackballGizmoVbuf;
    std::unique_ptr<QRhiShaderResourceBindings> m_trackballGizmoSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_trackballGizmoPipeline;
    int m_trackballGizmoVertexCount = 0;
    // Light gizmo (shown during Ctrl+Shift drag)
    std::unique_ptr<QRhiBuffer> m_lightGizmoUbuf;
    std::unique_ptr<QRhiBuffer> m_lightGizmoVbuf;
    std::unique_ptr<QRhiShaderResourceBindings> m_lightGizmoSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_lightGizmoPipeline;
    int m_lightGizmoVertexCount = 0;
    // Headlight rotation state
    QQuaternion m_lightRotation;     // rotates view-space (0,0,1) to current light dir
    bool m_lightDragActive = false;
    QPointF m_lightDragLastPos;
    RenderSettings m_renderSettings;
    RenderOverlayPanel *m_overlayPanel = nullptr;
    QWidget *m_currentViewIndicator = nullptr;
    bool m_currentViewHighlighted = false;
    QLabel *m_bboxMinCornerOverlayLabel = nullptr;
    QLabel *m_bboxMaxCornerOverlayLabel = nullptr;
    QLabel *m_bboxDimXOverlayLabel = nullptr;
    QLabel *m_bboxDimYOverlayLabel = nullptr;
    QLabel *m_bboxDimZOverlayLabel = nullptr;
    QLabel *m_qualityHistogramOverlayLabel = nullptr;
    QLabel *m_helpOverlayLabel = nullptr;
    QLabel *m_interactionStatusOverlayLabel = nullptr;
    QTimer *m_interactionStatusOverlayTimer = nullptr;
    std::array<QLabel *, 11> m_uvScaleXTickLabels {};
    std::array<QLabel *, 11> m_uvScaleYTickLabels {};
    QVector3D m_bboxOverlayMinCorner = QVector3D();
    QVector3D m_bboxOverlayMaxCorner = QVector3D();
    bool m_bboxOverlayCornersValid = false;
    struct QualityHistogramCache {
        bool valid = false;
        bool vertexBased = true;
        QualityHistogramSource sourceSelection = QualityHistogramSource::Auto;
        bool fixedRange = false;
        bool centerOnZero = false;
        float percentileCrop = 0.0f;
        float fixedMin = 0.0f;
        float fixedMax = 1.0f;
        QString colorMapId = QStringLiteral("rainbow");
        bool invertColorMap = false;
        std::uint64_t meshId = 0;
        std::uint64_t geometryRevision = 0;
        int bins = 0;
        float minQ = 0.0f;
        float maxQ = 1.0f;
        int sampleCount = 0;
        std::vector<int> counts;
    };
    QualityHistogramCache m_qualityHistogram;
    bool m_helpOverlayVisible = false;
    bool m_lastBroadcastTrackballStateValid = false;
    ViewTrackball::State m_lastBroadcastTrackballState;
    QElapsedTimer m_frameTimer;
    bool m_currentMaskFromPoints = false;
    ViewTrackball m_trackball;
    ViewMode m_viewMode = ViewMode::Scene3D;
    struct UvMeshGpu {
        struct UvFillVariantGpu {
            std::unique_ptr<QRhiBuffer> vbuf;
            int vertexCount = 0;
        };
        struct UvPointsVariantGpu {
            std::unique_ptr<QRhiBuffer> vbuf;
            int vertexCount = 0;
        };
        bool valid = false;
        std::uint64_t geometryRevision = 0;
        std::uint64_t materialRevision = 0;
        QString qualityColorMapId = QStringLiteral("rainbow");
        bool qualityColorMapInverted = false;
        bool qualityFixedRange = false;
        float qualityRangeMin = 0.0f;
        float qualityRangeMax = 1.0f;
        bool qualityCenterOnZero = false;
        float qualityPercentileCrop = 0.0f;
        std::unique_ptr<QRhiBuffer> wireVbuf;
        int wireVertexCount = 0;
        std::unique_ptr<QRhiBuffer> boundaryEdgesVbuf;
        int boundaryEdgesVertexCount = 0;
        std::unique_ptr<QRhiBuffer> textureSeamsVbuf;
        int textureSeamsVertexCount = 0;
        std::array<UvFillVariantGpu, 5> fillVariants;
        std::array<UvPointsVariantGpu, 3> pointsVariants;
        QVector2D minUv = QVector2D(0.0f, 0.0f);
        QVector2D maxUv = QVector2D(1.0f, 1.0f);
    };
    std::unique_ptr<QRhiBuffer> m_uvBackgroundUbuf;
    std::unique_ptr<QRhiShaderResourceBindings> m_uvBackgroundSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_uvBackgroundPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_uvTextureFillPipeline;
    std::unique_ptr<QRhiBuffer> m_uvTextureQuadVbuf;
    int m_uvTextureQuadVertexCount = 0;
    std::unique_ptr<QRhiBuffer> m_uvAxesVbuf;
    int m_uvAxesVertexCount = 0;
    std::unique_ptr<QRhiBuffer> m_uvUnitBoxVbuf;
    int m_uvUnitBoxVertexCount = 0;
    std::array<std::unique_ptr<QRhiBuffer>, 12> m_uvLineUbufs;
    std::array<std::unique_ptr<QRhiShaderResourceBindings>, 12> m_uvLineSrbs;
    std::unordered_map<int, std::unique_ptr<QRhiGraphicsPipeline>> m_uvLinePipelinesByKey;
    std::unordered_map<std::uint64_t, UvMeshGpu> m_uvMeshGpu;
    bool m_uvFitRequested = true;
    bool m_uvPanning = false;
    QPoint m_uvLastMousePos;
    float m_uvZoom = 1.0f;
    QVector2D m_uvPan = QVector2D(0.5f, 0.5f);
    bool m_rasterPanning = false;
    QPoint m_rasterLastMousePos;
    float m_rasterZoom = 1.0f;
    QVector2D m_rasterPan = QVector2D(0.5f, 0.5f);
    float m_rasterOpacity = 0.75f;
    std::vector<bool> m_meshVisibility;
    std::unordered_map<std::uint64_t, MeshRenderMode> m_meshRenderModes;
    std::unordered_map<std::uint64_t, std::pair<std::uint64_t, std::uint64_t>> m_meshRenderModeRevisions;
};
