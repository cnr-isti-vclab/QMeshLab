#pragma once

#include "interactivetool.h"

#include <QPoint>

class QRubberBand;

// Session tool: drag a rectangle to select the current mesh's faces (by centroid)
// or vertices. On release it commits exactly one select_by_rectangle filter call
// (camera + normalized rect) → one undo node, fully scriptable. Modifiers:
// Shift = add, Ctrl = subtract, otherwise replace. Keys: F/V switch faces/vertices,
// Esc cancels the in-progress drag.
class RubberBandSelectTool final : public InteractiveTool
{
public:
    QString id() const override;
    QString name() const override;
    QString statusHint() const override;
    QString badgeDetail() const override;
    QString iconPath() const override;
    QCursor cursor() const override;

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
};
