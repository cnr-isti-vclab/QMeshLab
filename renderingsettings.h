#pragma once

#include <QColor>
#include <QMetaType>

enum class RenderPass {
    BoundingBox = 0,
    Points,
    Wireframe,
    Fill
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

struct RenderSettings {
    bool showBoundingBox = false;
    bool showPoints = false;
    bool showWire = true;
    bool showFill = true;
    bool settingsPanelVisible = false;
    RenderPass currentPass = RenderPass::Fill;
    QColor bboxWireColor = QColor(245, 190, 60);
    QColor pointColor = QColor(255, 191, 51);
    float pointSize = 4.0f;
    QColor wireColor = QColor(15, 15, 20);
    float wireSize = 1.5f;
    QColor fillColor = QColor(153, 153, 179);
    FillShading fillShading = FillShading::Smooth;
    FillColorSource fillColorSource = FillColorSource::Constant;

    bool operator==(const RenderSettings &other) const
    {
        return showBoundingBox == other.showBoundingBox
            && showPoints == other.showPoints
            && showWire == other.showWire
            && showFill == other.showFill
            && settingsPanelVisible == other.settingsPanelVisible
            && currentPass == other.currentPass
            && bboxWireColor == other.bboxWireColor
            && pointColor == other.pointColor
            && pointSize == other.pointSize
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
