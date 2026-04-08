#pragma once

#include <QColor>
#include <QMetaType>

enum class RenderPass {
    CurrentMesh = 0,
    BoundingBox,
    Points,
    Wireframe,
    Fill,
    DecoratorNormals,
    DecoratorBoundary
};

enum class FillShading {
    Smooth = 0,
    Flat
};

enum class FillColorSource {
    Constant = 0,
    PerVertex,
    PerFace
};

enum class PointColorSource {
    Constant = 0,
    PerVertex
};

enum class CurrentMeshDebugView {
    Outline = 0,
    BaseMask,
    DilatedMask,
    ErodedMask
};

struct RenderSettings {
    bool highlightCurrentMesh = true;
    bool showBoundingBox = false;
    bool showPoints = false;
    bool showWire = true;
    bool showFill = true;
    QColor currentMeshOutlineColor = QColor(42, 160, 240);
    float currentMeshOutlineWidth = 1.0f;
    float currentMeshDilateRadius = 2.5f;
    float currentMeshErodeRadius = 1.5f;
    CurrentMeshDebugView currentMeshDebugView = CurrentMeshDebugView::Outline;
    bool pointLighting = false;
    bool wireLighting = false;
    bool wireBackfaceCulling = true;
    bool fillLighting = true;
    bool fillBackfaceCulling = true;
    bool settingsPanelVisible = false;
    RenderPass currentPass = RenderPass::Fill;
    bool decoratorVertexNormals = false;
    bool decoratorFaceNormals = false;
    bool decoratorBoundaryEdges = false;
    bool decoratorTextureSeams = false;
    QColor decoratorVertexNormalColor = QColor(70, 200, 255);
    QColor decoratorFaceNormalColor = QColor(70, 255, 120);
    QColor decoratorBoundaryEdgeColor = QColor(0, 255, 0);
    QColor decoratorTextureSeamColor = QColor(255, 80, 255);
    QColor bboxWireColor = QColor(245, 190, 60);
    QColor pointColor = QColor(255, 191, 51);
    float pointSize = 4.0f;
    PointColorSource pointColorSource = PointColorSource::Constant;
    QColor wireColor = QColor(15, 15, 20);
    float wireSize = 1.5f;
    QColor fillColor = QColor(153, 153, 179);
    FillShading fillShading = FillShading::Smooth;
    FillColorSource fillColorSource = FillColorSource::Constant;

    bool operator==(const RenderSettings &other) const
    {
        return highlightCurrentMesh == other.highlightCurrentMesh
            && showBoundingBox == other.showBoundingBox
            && showPoints == other.showPoints
            && showWire == other.showWire
            && showFill == other.showFill
            && currentMeshOutlineColor == other.currentMeshOutlineColor
            && currentMeshOutlineWidth == other.currentMeshOutlineWidth
            && currentMeshDilateRadius == other.currentMeshDilateRadius
            && currentMeshErodeRadius == other.currentMeshErodeRadius
            && currentMeshDebugView == other.currentMeshDebugView
            && pointLighting == other.pointLighting
            && wireLighting == other.wireLighting
            && wireBackfaceCulling == other.wireBackfaceCulling
            && fillLighting == other.fillLighting
            && fillBackfaceCulling == other.fillBackfaceCulling
            && settingsPanelVisible == other.settingsPanelVisible
            && currentPass == other.currentPass
            && decoratorVertexNormals == other.decoratorVertexNormals
            && decoratorFaceNormals == other.decoratorFaceNormals
            && decoratorBoundaryEdges == other.decoratorBoundaryEdges
            && decoratorTextureSeams == other.decoratorTextureSeams
            && decoratorVertexNormalColor == other.decoratorVertexNormalColor
            && decoratorFaceNormalColor == other.decoratorFaceNormalColor
            && decoratorBoundaryEdgeColor == other.decoratorBoundaryEdgeColor
            && decoratorTextureSeamColor == other.decoratorTextureSeamColor
            && bboxWireColor == other.bboxWireColor
            && pointColor == other.pointColor
            && pointSize == other.pointSize
            && pointColorSource == other.pointColorSource
            && wireColor == other.wireColor
            && wireSize == other.wireSize
            && fillColor == other.fillColor
            && fillShading == other.fillShading
            && fillColorSource == other.fillColorSource;
    }

    bool operator!=(const RenderSettings &other) const
    {
        return !(*this == other);
    }
};

Q_DECLARE_METATYPE(RenderSettings)
