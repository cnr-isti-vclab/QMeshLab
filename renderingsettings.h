#pragma once

#include <QColor>
#include <QMetaType>

enum class RenderPass {
    BoundingBox = 0,
    Points,
    Wireframe,
    Fill
};

struct RenderSettings {
    bool showBoundingBox = false;
    bool showPoints = false;
    bool showWire = true;
    bool showFill = true;
    bool settingsPanelVisible = false;
    RenderPass currentPass = RenderPass::Fill;
    QColor bboxWireColor = QColor(245, 190, 60);

    bool operator==(const RenderSettings &other) const
    {
        return showBoundingBox == other.showBoundingBox
            && showPoints == other.showPoints
            && showWire == other.showWire
            && showFill == other.showFill
            && settingsPanelVisible == other.settingsPanelVisible
            && currentPass == other.currentPass
            && bboxWireColor == other.bboxWireColor;
    }

    bool operator!=(const RenderSettings &other) const
    {
        return !(*this == other);
    }
};

Q_DECLARE_METATYPE(RenderSettings)
