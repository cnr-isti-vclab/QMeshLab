#pragma once

#include <QCursor>
#include <QString>
#include <QVector3D>

#include <memory>
#include <vector>

class RenderWidget;
class QMouseEvent;
class QKeyEvent;

// Result of an asynchronous GPU surface pick (see RenderWidget::requestSurfacePick).
struct SurfacePick
{
    bool hit = false;
    int meshIndex = -1;
    QVector3D worldPos;
};

// Base class for interactive editing tools (selection, transform, measure, …).
//
// A tool is a thin interactive front-end: it captures mouse/keyboard input in a
// RenderWidget, gives live visual feedback, and — when it produces a lasting
// document change — commits that change by calling Document::runFilter() exactly
// once. runFilter() already wraps itself in one undo step and records the Python
// call, so a whole tool session collapses into a single, replayable, scriptable
// undo node, indistinguishable from a menu-invoked filter. Tools therefore never
// touch the undo stack themselves.
//
// A tool is active in at most one view at a time. activate()/deactivate() bracket
// the session; deactivate(false) means the session was cancelled and any pending
// (uncommitted) change must be discarded.
class InteractiveTool
{
public:
    virtual ~InteractiveTool() = default;

    virtual QString id() const = 0;
    virtual QString name() const = 0;
    // Short one-line usage hint shown when the tool becomes active. Empty = none.
    virtual QString statusHint() const { return {}; }
    // Toolbar/menu icon resource path (e.g. ":/img/tool_select_rect.png"). Empty = none.
    virtual QString iconPath() const { return {}; }
    // Cursor shown while the tool owns the mouse (not suspended).
    virtual QCursor cursor() const { return QCursor(Qt::CrossCursor); }

    virtual void activate(RenderWidget &view) { m_view = &view; }
    virtual void deactivate(bool commit) { (void)commit; m_view = nullptr; }

    // Return true when the event is consumed; the view then skips camera
    // navigation for that event. Returning false lets the trackball handle it.
    virtual bool mousePress(QMouseEvent *e) { (void)e; return false; }
    virtual bool mouseMove(QMouseEvent *e) { (void)e; return false; }
    virtual bool mouseRelease(QMouseEvent *e) { (void)e; return false; }
    virtual bool keyPress(QKeyEvent *e) { (void)e; return false; }

    // Delivered (asynchronously) after a RenderWidget::requestSurfacePick issued
    // by this tool completes. result.hit is false when the click missed all meshes.
    virtual void onSurfacePicked(const SurfacePick &result) { (void)result; }

    // Called when an external document change happens mid-gesture (load, filter,
    // undo/redo). Per the lifecycle contract the tool discards any uncommitted
    // in-progress gesture but stays engaged. No-op for tools without one.
    virtual void cancelGesture() {}

protected:
    RenderWidget *m_view = nullptr;
};

// Builds the set of built-in interactive tools, mirroring
// registerBuiltinMeshFilterPlugins() for the filter side.
std::vector<std::unique_ptr<InteractiveTool>> createBuiltinInteractiveTools();
