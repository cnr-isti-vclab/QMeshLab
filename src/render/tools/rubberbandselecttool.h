#pragma once

#include "interactivetool.h"

#include <QPoint>

class QRubberBand;

// Session tool: drag a rectangle to select the current mesh's faces (by centroid)
// or vertices. On release it commits exactly one select_by_screen_rectangle filter call
// (camera + normalized rect) → one undo node, fully scriptable. Modifiers:
// Shift = add, Ctrl = subtract, otherwise replace. Keys: F/V switch faces/vertices,
// B toggles visible-only, C toggles connected-component growth, Esc cancels the drag.
class RubberBandSelectTool final : public InteractiveTool
{
public:
    QString id() const override;
    QString name() const override;
    QString statusHint() const override;
    QString badgeDetail() const override;
    QString iconPath() const override;
    QCursor cursor() const override;
    bool supportsUvView() const override { return true; }

    void deactivate(bool commit) override;
    bool mousePress(QMouseEvent *e) override;
    bool mouseMove(QMouseEvent *e) override;
    bool mouseRelease(QMouseEvent *e) override;
    bool keyPress(QKeyEvent *e) override;
    void cancelGesture() override;

private:
    void endDrag();

    QRubberBand *m_band = nullptr;
    QPoint m_origin;
    bool m_dragging = false;
    bool m_selectFaces = true;
    bool m_visibleOnly = false; // faces: keep only faces not occluded from the viewpoint
    bool m_connectedComponents = false; // grow the hit set to whole connected components
};
