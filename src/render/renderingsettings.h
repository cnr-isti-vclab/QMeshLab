#pragma once

#include <QColor>
#include <QMetaType>
#include <QString>

enum class RenderPass {
    CurrentMesh = 0,
    BoundingBox,
    Points,
    Edges,
    Wireframe,
    Fill,
    Selection,
    DecoratorNormals,
    DecoratorBoundary,
    QualityHistogram
};

enum class FillShading {
    Smooth = 0,
    Flat
};

enum class FillColorSource {
    Constant = 0,
    PerVertex,
    PerFace,
    PerVertexQuality,
    PerFaceQuality,
    Texture
};

enum class FillMaterial {
    Plain = 0,
    Pbr,
    RadianceScaling
};

enum class FillPbrTextureSource {
    None = 0,
    Constant,
    Texture
};

// Per-material parameter sub-structs (Suggestion C).
// These group together all settings specific to one fill material so that
// render code, sync code and equality checks stay local to each material.

struct PbrFillParams {
    FillShading          shading          = FillShading::Smooth;
    FillPbrTextureSource albedoSource    = FillPbrTextureSource::Texture;
    int                  albedoIndex     = -1;
    FillPbrTextureSource normalSource    = FillPbrTextureSource::Texture;
    int                  normalIndex     = -1;
    FillPbrTextureSource occlusionSource = FillPbrTextureSource::Texture;
    int                  occlusionIndex  = -1;
    FillPbrTextureSource roughnessSource = FillPbrTextureSource::Texture;
    int                  roughnessIndex  = -1;
    float                normalScale       = 1.0f;
    float                occlusionStrength = 1.0f;
    float                roughnessFactor   = 1.0f;

    bool operator==(const PbrFillParams &o) const
    {
        return shading         == o.shading
            && albedoSource    == o.albedoSource
            && albedoIndex     == o.albedoIndex
            && normalSource    == o.normalSource
            && normalIndex     == o.normalIndex
            && occlusionSource == o.occlusionSource
            && occlusionIndex  == o.occlusionIndex
            && roughnessSource == o.roughnessSource
            && roughnessIndex  == o.roughnessIndex
            && normalScale       == o.normalScale
            && occlusionStrength == o.occlusionStrength
            && roughnessFactor   == o.roughnessFactor;
    }
    bool operator!=(const PbrFillParams &o) const { return !(*this == o); }
};

struct PlainFillParams {
    FillShading     shading      = FillShading::Smooth;
    FillColorSource colorSource  = FillColorSource::Constant;
    int             textureIndex = -1;

    bool operator==(const PlainFillParams &o) const
    {
        return shading      == o.shading
            && colorSource  == o.colorSource
            && textureIndex == o.textureIndex;
    }
    bool operator!=(const PlainFillParams &o) const { return !(*this == o); }
};

struct RsFillParams {
    float enhancement = 0.5f;
    int   displayMode = 0;   // 0=Lambertian, 1=Colored Descriptor, 2=Grey Descriptor
    bool  invert      = false;

    bool operator==(const RsFillParams &o) const
    {
        return enhancement == o.enhancement
            && displayMode == o.displayMode
            && invert      == o.invert;
    }
    bool operator!=(const RsFillParams &o) const { return !(*this == o); }
};

enum class PointColorSource {
    Constant = 0,
    PerVertex,
    PerVertexQuality
};

enum class QualityHistogramSource {
    Auto = 0,
    VertexQuality,
    FaceQuality
};

enum class CurrentMeshDebugView {
    Outline = 0,
    FullMask,
    VisibleMask,
    OccludedMask,
    DilatedMask,
    ErodedMask
};

// Per-mesh rendering settings (one instance per mesh in the scene).
// Replaces the former private MeshRenderMode struct and is now a public,
// named type so that external code can manipulate per-mesh settings directly.
struct PerMeshRenderSettings {
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
    bool wireRespectFaux = true;
    bool fillLighting = true;
    bool fillBackfaceCulling = true;
    FillMaterial fillMaterial = FillMaterial::Plain;
    PbrFillParams fillPbr;
    RsFillParams  fillRs;
    PlainFillParams fillPlain;
    PointColorSource pointColorSource = PointColorSource::Constant;
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
    QColor fillColor = QColor(230, 230, 230);

    bool operator==(const PerMeshRenderSettings &o) const
    {
        return showBoundingBox == o.showBoundingBox
            && showPoints == o.showPoints
            && showEdges == o.showEdges
            && showWire == o.showWire
            && showFill == o.showFill
            && showSelection == o.showSelection
            && showSelectionVertices == o.showSelectionVertices
            && showSelectionFaces == o.showSelectionFaces
            && decoratorVertexNormals == o.decoratorVertexNormals
            && decoratorFaceNormals == o.decoratorFaceNormals
            && decoratorBoundaryEdges == o.decoratorBoundaryEdges
            && decoratorTextureSeams == o.decoratorTextureSeams
            && pointLighting == o.pointLighting
            && wireLighting == o.wireLighting
            && wireBackfaceCulling == o.wireBackfaceCulling
            && wireRespectFaux == o.wireRespectFaux
            && fillLighting == o.fillLighting
            && fillBackfaceCulling == o.fillBackfaceCulling
            && fillMaterial == o.fillMaterial
            && fillPbr == o.fillPbr
            && fillRs == o.fillRs
            && fillPlain == o.fillPlain
            && pointColorSource == o.pointColorSource
            && decoratorVertexNormalColor == o.decoratorVertexNormalColor
            && decoratorFaceNormalColor == o.decoratorFaceNormalColor
            && decoratorBoundaryEdgeColor == o.decoratorBoundaryEdgeColor
            && decoratorTextureSeamColor == o.decoratorTextureSeamColor
            && decoratorBoundaryWidth == o.decoratorBoundaryWidth
            && bboxWireColor == o.bboxWireColor
            && pointColor == o.pointColor
            && pointSize == o.pointSize
            && edgeColor == o.edgeColor
            && edgeSize == o.edgeSize
            && wireColor == o.wireColor
            && wireSize == o.wireSize
            && fillColor == o.fillColor;
    }
    bool operator!=(const PerMeshRenderSettings &o) const { return !(*this == o); }
};

Q_DECLARE_METATYPE(PerMeshRenderSettings)

// View-level (global) rendering settings shared across all meshes in the scene.
struct GlobalRenderSettings {
    bool highlightCurrentMesh = true;
    bool showTrackballGizmo = true;
    bool showBoundingBoxCorners = false;
    bool showBoundingBoxDimensions = false;
    QColor currentMeshOutlineColor = QColor(42, 160, 240);
    float currentMeshOutlineWidth = 1.0f;
    float currentMeshDilateRadius = 2.5f;
    float currentMeshErodeRadius = 1.5f;
    CurrentMeshDebugView currentMeshDebugView = CurrentMeshDebugView::Outline;
    bool settingsPanelVisible = false;
    RenderPass currentPass = RenderPass::Fill;
    bool showQualityHistogram = false;
    bool uvShowReferenceFrame = true;
    bool uvShowFullTexture = false;
    int uvTextureIndex = -1;
    bool uvTextureNearestSampling = false;
    QColor sceneBackgroundTopColor = QColor(0, 0, 0);
    QColor sceneBackgroundBottomColor = QColor(128, 128, 255);
    int qualityHistogramBins = 32;
    QualityHistogramSource qualityHistogramSource = QualityHistogramSource::Auto;
    bool qualityHistogramFixedRange = false;
    float qualityHistogramMin = 0.0f;
    float qualityHistogramMax = 1.0f;
    QString qualityHistogramColorMapId = QStringLiteral("rainbow");
    bool qualityHistogramInvertColorMap = false;
    bool qualityIsolinesEnabled = false;
    int qualityIsolineCount = 10;

    bool operator==(const GlobalRenderSettings &o) const
    {
        return highlightCurrentMesh == o.highlightCurrentMesh
            && showTrackballGizmo == o.showTrackballGizmo
            && showBoundingBoxCorners == o.showBoundingBoxCorners
            && showBoundingBoxDimensions == o.showBoundingBoxDimensions
            && currentMeshOutlineColor == o.currentMeshOutlineColor
            && currentMeshOutlineWidth == o.currentMeshOutlineWidth
            && currentMeshDilateRadius == o.currentMeshDilateRadius
            && currentMeshErodeRadius == o.currentMeshErodeRadius
            && currentMeshDebugView == o.currentMeshDebugView
            && settingsPanelVisible == o.settingsPanelVisible
            && currentPass == o.currentPass
            && showQualityHistogram == o.showQualityHistogram
            && uvShowReferenceFrame == o.uvShowReferenceFrame
            && uvShowFullTexture == o.uvShowFullTexture
            && uvTextureIndex == o.uvTextureIndex
            && uvTextureNearestSampling == o.uvTextureNearestSampling
            && sceneBackgroundTopColor == o.sceneBackgroundTopColor
            && sceneBackgroundBottomColor == o.sceneBackgroundBottomColor
            && qualityHistogramBins == o.qualityHistogramBins
            && qualityHistogramSource == o.qualityHistogramSource
            && qualityHistogramFixedRange == o.qualityHistogramFixedRange
            && qualityHistogramMin == o.qualityHistogramMin
            && qualityHistogramMax == o.qualityHistogramMax
            && qualityHistogramColorMapId == o.qualityHistogramColorMapId
            && qualityHistogramInvertColorMap == o.qualityHistogramInvertColorMap
            && qualityIsolinesEnabled == o.qualityIsolinesEnabled
            && qualityIsolineCount == o.qualityIsolineCount;
    }
    bool operator!=(const GlobalRenderSettings &o) const { return !(*this == o); }
};

// Backward-compatibility alias so callers that still use RenderSettings keep compiling.
using RenderSettings = GlobalRenderSettings;

Q_DECLARE_METATYPE(GlobalRenderSettings)
