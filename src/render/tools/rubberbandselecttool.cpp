#include "rubberbandselecttool.h"

#include "document.h"
#include "renderwidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QRect>
#include <QRubberBand>
#include <QVariantMap>

#include <algorithm>

QString RubberBandSelectTool::id() const
{
    return QStringLiteral("rubberband_select");
}

QString RubberBandSelectTool::name() const
{
    return QObject::tr("Rubber-band Select");
}

QString RubberBandSelectTool::statusHint() const
{
    return QObject::tr("Rubber-band: drag to select — Shift add, Ctrl subtract, F/V faces/vertices, Esc cancel");
}

void RubberBandSelectTool::deactivate(bool commit)
{
    endDrag();
    delete m_band; // safe if null
    m_band = nullptr;
    InteractiveTool::deactivate(commit);
}

void RubberBandSelectTool::endDrag()
{
    m_dragging = false;
    if (m_band)
        m_band->hide();
}

void RubberBandSelectTool::cancelGesture()
{
    endDrag();
}

bool RubberBandSelectTool::mousePress(QMouseEvent *e)
{
    if (!m_view || !e || e->button() != Qt::LeftButton)
        return false;
    m_origin = e->position().toPoint();
    if (!m_band)
        m_band = new QRubberBand(QRubberBand::Rectangle, m_view);
    m_band->setGeometry(QRect(m_origin, QSize()));
    m_band->show();
    m_dragging = true;
    return true;
}

bool RubberBandSelectTool::mouseMove(QMouseEvent *e)
{
    if (!m_dragging || !e)
        return false;
    m_band->setGeometry(QRect(m_origin, e->position().toPoint()).normalized());
    return true;
}

bool RubberBandSelectTool::mouseRelease(QMouseEvent *e)
{
    if (!m_dragging || !e || e->button() != Qt::LeftButton)
        return false;

    const QRect rect = QRect(m_origin, e->position().toPoint()).normalized();
    endDrag(); // hide the band before committing so the change signal is a no-op here

    Document *doc = m_view->document();
    const int w = std::max(1, m_view->width());
    const int h = std::max(1, m_view->height());
    if (!doc || rect.width() < 2 || rect.height() < 2)
        return true; // ignore accidental clicks

    // Normalize to [0..1], y up (bottom-left origin) to match the filter's convention.
    const double xMin = double(rect.left()) / w;
    const double xMax = double(rect.right()) / w;
    const double yMin = 1.0 - double(rect.bottom()) / h;
    const double yMax = 1.0 - double(rect.top()) / h;

    const Qt::KeyboardModifiers mods = e->modifiers();
    const QString mode = (mods & Qt::ShiftModifier) ? QStringLiteral("add")
        : (mods & Qt::ControlModifier) ? QStringLiteral("subtract")
        : QStringLiteral("replace");

    QVariantMap params;
    params[QStringLiteral("camera_state")] = m_view->cameraStateJson();
    params[QStringLiteral("aspect")] = double(w) / double(h);
    params[QStringLiteral("rect_min_x")] = xMin;
    params[QStringLiteral("rect_min_y")] = yMin;
    params[QStringLiteral("rect_max_x")] = xMax;
    params[QStringLiteral("rect_max_y")] = yMax;
    params[QStringLiteral("element")] = m_selectFaces ? QStringLiteral("face") : QStringLiteral("vertex");
    params[QStringLiteral("mode")] = mode;

    doc->runFilter(QStringLiteral("qmeshlab.filter.select::select_by_rectangle"), params);
    return true;
}

bool RubberBandSelectTool::keyPress(QKeyEvent *e)
{
    if (!e)
        return false;
    if (e->key() == Qt::Key_Escape) {
        cancelGesture();
        return true;
    }
    if (e->key() == Qt::Key_F || e->key() == Qt::Key_V) {
        m_selectFaces = (e->key() == Qt::Key_F);
        if (m_view && m_view->document())
            m_view->document()->writeLog(
                m_selectFaces ? QObject::tr("Rubber-band: selecting faces")
                              : QObject::tr("Rubber-band: selecting vertices"),
                Document::LogSource::Application);
        return true;
    }
    return false;
}
