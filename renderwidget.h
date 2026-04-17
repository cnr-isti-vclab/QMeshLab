#pragma once

#include "renderingsettings.h"
#include "viewtrackball.h"
#include <QRhiWidget>
#include <rhi/qrhi.h>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QPoint>
#include <QString>
#include <QVector2D>
#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

class Document;
class RenderOverlayPanel;
class QLabel;

class RenderWidget : public QRhiWidget
{
    Q_OBJECT
public:
    enum class ViewMode {
        Scene3D,
        ParametrizationUV
    };

    explicit RenderWidget(Document *doc, QWidget *parent = nullptr);
    void setCurrentViewHighlighted(bool highlighted);
    void resetCameraToScene();
    const RenderSettings &renderSettings() const { return m_renderSettings; }
    void setRenderSettings(const RenderSettings &settings);
    QString cameraStateJson() const;
    bool applyCameraStateJson(const QString &jsonText, QString *errorMessage = nullptr);
    ViewMode viewMode() const { return m_viewMode; }
    bool setViewMode(ViewMode mode, QString *errorMessage = nullptr);
    bool meshVisible(int index) const;
    void setMeshVisible(int index, bool visible);
    std::vector<bool> meshVisibilityState() const { return m_meshVisibility; }
    void setMeshVisibilityState(const std::vector<bool> &visibility);
    void copyPerMeshRenderModesFrom(const RenderWidget *other);

signals:
    void frameRendered(float cpuMs, float gpuMs, bool gpuTimingSupported, bool gpuSampleValid);
    void trackballCenterPicked(const QVector3D &worldPos);
    void viewActivated(RenderWidget *view);

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
    struct MeshRenderMode {
        bool showBoundingBox = false;
        bool showPoints = false;
        bool showEdges = false;
        bool showWire = true;
        bool showFill = true;
        bool showSelection = true;
        bool showSelectionVertices = true;
        bool showSelectionFaces = true;
        bool decoratorVertexNormals = false;
        bool decoratorFaceNormals = false;
        bool decoratorBoundaryEdges = false;
        bool decoratorTextureSeams = false;
        bool pointLighting = false;
        bool wireLighting = false;
        bool wireBackfaceCulling = true;
        bool fillLighting = true;
        bool fillBackfaceCulling = true;
        FillShading fillShading = FillShading::Smooth;
        PointColorSource pointColorSource = PointColorSource::Constant;
        FillColorSource fillColorSource = FillColorSource::Constant;
        QColor decoratorVertexNormalColor = QColor(70, 200, 255);
        QColor decoratorFaceNormalColor = QColor(70, 255, 120);
        QColor decoratorBoundaryEdgeColor = QColor(0, 255, 0);
        QColor decoratorTextureSeamColor = QColor(255, 80, 255);
        float decoratorBoundaryWidth = 4.0f;
        QColor bboxWireColor = QColor(245, 190, 60);
        QColor pointColor = QColor(255, 191, 51);
        float pointSize = 4.0f;
        QColor edgeColor = QColor(25, 25, 28);
        float edgeSize = 1.0f;
        QColor wireColor = QColor(15, 15, 20);
        float wireSize = 1.5f;
        QColor fillColor = QColor(153, 153, 179);
    };

    void createOverlayButtons();
    void layoutOverlayButtons();
    bool computeVisibleSceneBoundingBox(QVector3D &minCorner, QVector3D &maxCorner) const;
    void updateBoundingBoxCornersOverlay();
    void updateBoundingBoxCornersOverlayPlacement(
        const QMatrix4x4 &mvp,
        const QMatrix4x4 &view,
        const QSize &pixelSize);
    void updateQualityHistogramOverlay();
    void updateUvScaleOverlay(
        const QMatrix4x4 &mvp,
        const QSize &pixelSize,
        bool showUvReference);
    MeshRenderMode defaultRenderModeForMesh(int meshIndex) const;
    void syncPerMeshRenderModesWithDocument();
    MeshRenderMode renderModeForMesh(int meshIndex) const;
    MeshRenderMode *mutableRenderModeForMesh(int meshIndex);
    bool applyRenderSettingsToCurrentMesh(const RenderSettings &prev, const RenderSettings &next);
    void applyRenderModeToSettings(RenderSettings &settings, const MeshRenderMode &mode) const;
    RenderSettings renderSettingsForMesh(int meshIndex) const;
    void syncOverlaySettingsToCurrentMesh();
    void refreshColorSourceAvailability();
    void ensureRenderResources();
    void ensureCurrentMeshMaskResources(const QSize &pixelSize);
    void ensureDepthPickResources(const QSize &pixelSize);
    void prepareDirtyBuffers(QRhiCommandBuffer *cb);
    void startCenterAnimation(const QVector3D &targetCenter);
    void cancelCenterAnimation();
    void advanceCenterAnimation();
    void updateCameraFrameIfNeeded();
    void ensureVisibilitySize();
    int fillGpuVariantIndexForSettings(const RenderSettings &settings) const;
    int pointGpuVariantIndexForSettings(const RenderSettings &settings) const;
    QRhiGraphicsPipeline *fillPipelineForSettings(const RenderSettings &settings);
    QRhiGraphicsPipeline *wirePipelineForSettings(const RenderSettings &settings);
    QRhiGraphicsPipeline *edgesPipelineForSettings(const RenderSettings &settings);
    QRhiGraphicsPipeline *fatEdgesPipelineForSettings(const RenderSettings &settings);
    QRhiShaderResourceBindings *shaderResourcesForTexture(QRhiTexture *texture);
    void executePendingDepthPick(
        QRhiCommandBuffer *cb,
        const QSize &pixelSize);
    void renderCurrentMeshMask(QRhiCommandBuffer *cb, const QSize &pixelSize);
    void processCurrentMeshMask(QRhiCommandBuffer *cb, const QSize &pixelSize);
    void drawCurrentMeshDebugView(QRhiCommandBuffer *cb, const QSize &pixelSize);
    void drawCurrentMeshOutline(QRhiCommandBuffer *cb, const QSize &pixelSize);
    void syncUvCacheWithDocument();
    bool meshHasParametrization(int meshIndex) const;
    bool ensureUvMeshResources(int meshIndex, QRhiCommandBuffer *cb);
    void fitUvViewToCurrentMesh(const QSize &pixelSize);
    void renderParametrization(QRhiCommandBuffer *cb);

    Document *m_doc;
    QRhi *m_rhi = nullptr;
    bool m_reframeCameraRequested = true;
    bool m_resetTrackballRequested = false;
    bool m_centerAnimActive = false;
    QVector3D m_centerAnimStart;
    QVector3D m_centerAnimTarget;
    QElapsedTimer m_centerAnimTimer;
    int m_centerAnimDurationMs = 200;

    std::unique_ptr<QRhiBuffer> m_ubuf;
    std::unique_ptr<QRhiSampler> m_textureSampler;
    std::unique_ptr<QRhiTexture> m_fallbackTexture;
    bool m_fallbackTextureUploadPending = false;
    std::unique_ptr<QRhiTexture> m_qualityColorMapTexture;
    bool m_qualityColorMapTextureUploadPending = false;
    QString m_qualityColorMapTextureMapId;
    bool m_qualityColorMapTextureInverted = false;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unique_ptr<QRhiBuffer> m_sceneBackgroundUbuf;
    std::unique_ptr<QRhiShaderResourceBindings> m_sceneBackgroundSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_sceneBackgroundPipeline;
    std::unordered_map<QRhiTexture *, std::unique_ptr<QRhiShaderResourceBindings>> m_textureSrbs;
    std::unordered_map<int, std::unique_ptr<QRhiGraphicsPipeline>> m_fillPipelinesByKey;
    std::unordered_map<int, std::unique_ptr<QRhiGraphicsPipeline>> m_wirePipelinesByKey;
    std::unordered_map<int, std::unique_ptr<QRhiGraphicsPipeline>> m_edgesPipelinesByKey;
    std::unordered_map<int, std::unique_ptr<QRhiGraphicsPipeline>> m_fatEdgesPipelinesByKey;
    std::unique_ptr<QRhiGraphicsPipeline> m_bboxPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_pointsPipeline;
    std::array<std::unique_ptr<QRhiBuffer>, 4> m_decoratorUbufs;
    std::array<std::unique_ptr<QRhiShaderResourceBindings>, 4> m_decoratorSrbs;
    std::unique_ptr<QRhiGraphicsPipeline> m_decoratorPipeline;
    std::unique_ptr<QRhiBuffer> m_selectionUbuf;
    std::unique_ptr<QRhiShaderResourceBindings> m_selectionSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_selectionFacesPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_selectionVerticesPipeline;
    std::unique_ptr<QRhiBuffer> m_decoratorFatUbuf;
    std::unique_ptr<QRhiShaderResourceBindings> m_decoratorFatSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_decoratorFatPipeline;
    std::unique_ptr<QRhiTexture> m_depthPickTexture;
    std::unique_ptr<QRhiRenderBuffer> m_depthPickDepth;
    std::unique_ptr<QRhiTextureRenderTarget> m_depthPickRt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_depthPickRp;
    QSize m_depthPickSize;
    std::unique_ptr<QRhiShaderResourceBindings> m_depthPickSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_depthPickFillPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_depthPickPointsPipeline;
    bool m_depthPickPending = false;
    bool m_depthPickInFlight = false;
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
    std::vector<bool> m_meshVisibility;
    std::unordered_map<std::uint64_t, MeshRenderMode> m_meshRenderModes;
};
