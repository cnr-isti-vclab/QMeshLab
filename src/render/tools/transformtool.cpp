#include "transformtool.h"

#include "document.h"
#include "meshfilterplugin.h"
#include "renderwidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace {

constexpr const char *kTranslateKey = "qmeshlab.filter.meshing::compute_matrix_from_translation";
constexpr const char *kRotateKey    = "qmeshlab.filter.meshing::compute_matrix_from_rotation";
constexpr const char *kScaleKey     = "qmeshlab.filter.meshing::compute_matrix_from_scaling_or_normalization";

QVector3D axisVector(int axis)
{
    switch (axis) {
    case 0: return { 1, 0, 0 };
    case 1: return { 0, 1, 0 };
    default: return { 0, 0, 1 };
    }
}

QString axisName(int axis)
{
    switch (axis) {
    case 0: return QStringLiteral("X");
    case 1: return QStringLiteral("Y");
    default: return QStringLiteral("Z");
    }
}

} // namespace

QString TransformTool::id() const { return QStringLiteral("transform"); }

QString TransformTool::name() const { return QObject::tr("Transform Layer"); }

QString TransformTool::statusHint() const
{
    return QObject::tr("G move · R rotate · S scale — then X/Y/Z to constrain, type a number, "
                       "Enter or click to apply, Esc to cancel");
}

QString TransformTool::badgeDetail() const
{
    if (m_gesture == Gesture::None)
        return {};
    return readout();
}

QCursor TransformTool::cursor() const
{
    return QCursor(Qt::SizeAllCursor);
}

void TransformTool::activate(RenderWidget &view)
{
    InteractiveTool::activate(view);
    clearGesture();
}

void TransformTool::deactivate(bool commit)
{
    (void) commit;
    // Leaving the tool never commits a half-finished drag.
    abortGesture();
    InteractiveTool::deactivate(commit);
}

void TransformTool::armGesture(Gesture g)
{
    if (m_gesture != Gesture::None)
        abortGesture();
    beginGesture(g);
}

QVector3D TransformTool::pivotWorld() const
{
    if (!m_view || !m_view->document())
        return {};
    Document *doc = m_view->document();
    if (m_meshIndex < 0 || m_meshIndex >= doc->meshCount())
        return {};
    const Document::MeshEntry &entry = doc->mesh(m_meshIndex);
    const vcg::Point3f c = entry.mesh.bbox.Center();
    // The pivot must be in world space: the filter is handed the same centre via
    // rotCenter/scaleCenter = bbox_center, which it evaluates in mesh space, but the
    // on-screen mapping below happens after the layer transform.
    return m_originalTransform * QVector3D(c.X(), c.Y(), c.Z());
}

QPointF TransformTool::pivotScreen() const
{
    if (!m_view)
        return {};
    const QMatrix4x4 worldToClip = m_view->projectionMatrix() * m_view->viewMatrix();
    const QVector4D clip = worldToClip * QVector4D(m_pivot, 1.0f);
    if (std::abs(clip.w()) < 1e-9f)
        return {};
    const QSize sz = m_view->size();
    return { (clip.x() / clip.w() * 0.5f + 0.5f) * sz.width(),
             (1.0f - (clip.y() / clip.w() * 0.5f + 0.5f)) * sz.height() };
}

bool TransformTool::beginGesture(Gesture g)
{
    if (!m_view || !m_view->document())
        return false;
    Document *doc = m_view->document();
    const int idx = doc->currentMeshIndex();
    if (idx < 0 || idx >= doc->meshCount()) {
        doc->writeLog(QObject::tr("Transform: select a layer first."),
                      Document::LogSource::Application, Document::LogLevel::Warning);
        return false;
    }

    m_gesture = g;
    m_meshIndex = idx;
    m_originalTransform = doc->mesh(idx).transform;
    m_pivot = pivotWorld();
    m_axis = -1;
    m_planeConstraint = false;
    m_numeric.clear();
    m_startMouse = m_view->mapFromGlobal(QCursor::pos());
    m_currentMouse = m_startMouse;
    m_view->update();
    return true;
}

// World units spanned by one screen pixel at the pivot's depth. Derived by
// unprojecting a one-pixel offset, so it is correct for both perspective and
// orthographic projections.
float TransformTool::worldUnitsPerPixel() const
{
    if (!m_view)
        return 1.0f;
    const QSize sz = m_view->size();
    if (sz.width() <= 0)
        return 1.0f;
    const QMatrix4x4 worldToClip = m_view->projectionMatrix() * m_view->viewMatrix();
    bool invertible = false;
    const QMatrix4x4 clipToWorld = worldToClip.inverted(&invertible);
    if (!invertible)
        return 1.0f;
    const QVector4D clip = worldToClip * QVector4D(m_pivot, 1.0f);
    if (std::abs(clip.w()) < 1e-9f)
        return 1.0f;
    QVector4D shifted = clip;
    shifted.setX(clip.x() + (2.0f / float(sz.width())) * clip.w());
    const QVector4D a = clipToWorld * clip;
    const QVector4D b = clipToWorld * shifted;
    if (std::abs(a.w()) < 1e-9f || std::abs(b.w()) < 1e-9f)
        return 1.0f;
    return (b.toVector3D() / b.w() - a.toVector3D() / a.w()).length();
}

// Camera forward in world space: the view matrix maps world to view, and the
// camera looks down its own -Z.
QVector3D TransformTool::viewForward() const
{
    if (!m_view)
        return { 0, 0, -1 };
    const QMatrix4x4 v = m_view->viewMatrix();
    const QVector3D f(-v(2, 0), -v(2, 1), -v(2, 2));
    return f.isNull() ? QVector3D(0, 0, -1) : f.normalized();
}

QMatrix4x4 TransformTool::gestureMatrix() const
{
    QMatrix4x4 m;
    m.setToIdentity();
    if (m_gesture == Gesture::None || !m_view)
        return m;

    const bool typed = !m_numeric.isEmpty();
    bool ok = false;
    const double typedValue = typed ? m_numeric.toDouble(&ok) : 0.0;
    const QPointF delta = m_currentMouse - m_startMouse;

    switch (m_gesture) {
    case Gesture::Translate: {
        QVector3D t;
        if (typed && ok && m_axis >= 0) {
            t = axisVector(m_axis) * float(typedValue);
        } else {
            // Screen motion mapped to world units at the pivot's depth.
            const float scale = worldUnitsPerPixel();
            const QMatrix4x4 view = m_view->viewMatrix();
            const QVector3D right(view(0, 0), view(0, 1), view(0, 2));
            const QVector3D up(view(1, 0), view(1, 1), view(1, 2));
            t = (right * float(delta.x()) - up * float(delta.y())) * scale;
            if (m_axis >= 0) {
                const QVector3D a = axisVector(m_axis);
                t = m_planeConstraint ? (t - a * QVector3D::dotProduct(t, a))
                                      : a * QVector3D::dotProduct(t, a);
            }
        }
        m.translate(t);
        break;
    }
    case Gesture::Rotate: {
        const QPointF c = pivotScreen();
        double degrees = 0.0;
        if (typed && ok) {
            degrees = typedValue;
        } else {
            const double a0 = std::atan2(m_startMouse.y() - c.y(), m_startMouse.x() - c.x());
            const double a1 = std::atan2(m_currentMouse.y() - c.y(), m_currentMouse.x() - c.x());
            degrees = -qRadiansToDegrees(a1 - a0);
        }
        // The filter's angle parameter is bounded to +-360.
        degrees = std::clamp(degrees, -360.0, 360.0);
        const QVector3D a = (m_axis >= 0) ? axisVector(m_axis) : viewForward();
        m.translate(m_pivot);
        m.rotate(float(degrees), a);
        m.translate(-m_pivot);
        break;
    }
    case Gesture::Scale: {
        const QPointF c = pivotScreen();
        double factor = 1.0;
        if (typed && ok) {
            factor = typedValue;
        } else {
            const double d0 = std::hypot(m_startMouse.x() - c.x(), m_startMouse.y() - c.y());
            const double d1 = std::hypot(m_currentMouse.x() - c.x(), m_currentMouse.y() - c.y());
            factor = (d0 > 1e-6) ? d1 / d0 : 1.0;
        }
        QVector3D s{ float(factor), float(factor), float(factor) };
        if (m_axis >= 0 && !m_planeConstraint) {
            s = QVector3D(1, 1, 1);
            s[m_axis] = float(factor);
        } else if (m_axis >= 0) {
            s = QVector3D{ float(factor), float(factor), float(factor) };
            s[m_axis] = 1.0f;
        }
        m.translate(m_pivot);
        m.scale(s);
        m.translate(-m_pivot);
        break;
    }
    case Gesture::None:
        break;
    }
    return m;
}

void TransformTool::updatePreview()
{
    if (m_gesture == Gesture::None || !m_view || !m_view->document())
        return;
    Document *doc = m_view->document();
    if (m_meshIndex < 0 || m_meshIndex >= doc->meshCount())
        return;
    // Written straight through, deliberately: Document::setMeshTransform would open an
    // undo step and emit meshDataChanged (a full filter-menu rebuild) on every move.
    doc->mesh(m_meshIndex).transform = gestureMatrix() * m_originalTransform;
    m_view->update();
}

void TransformTool::commitGesture()
{
    if (m_gesture == Gesture::None || !m_view || !m_view->document())
        return;
    Document *doc = m_view->document();
    if (m_meshIndex < 0 || m_meshIndex >= doc->meshCount()) {
        clearGesture();
        return;
    }

    const Gesture gesture = m_gesture;
    const QMatrix4x4 delta = gestureMatrix();

    // The preview must be gone before runFilter captures its "before" snapshot,
    // or the undo step would restore a state that already includes the change.
    doc->mesh(m_meshIndex).transform = m_originalTransform;

    MeshFilterParameterValues params;
    QString key;
    switch (gesture) {
    case Gesture::Translate: {
        const QVector3D t = delta.column(3).toVector3D();
        key = QString::fromLatin1(kTranslateKey);
        params[QStringLiteral("traslMethod")] = QStringLiteral("xyz");
        params[QStringLiteral("axis")] = t;
        break;
    }
    case Gesture::Rotate: {
        const QVector3D a = (m_axis >= 0) ? axisVector(m_axis) : viewForward();
        key = QString::fromLatin1(kRotateKey);
        params[QStringLiteral("rotAxis")] = QStringLiteral("custom");
        params[QStringLiteral("customAxis")] = a;
        params[QStringLiteral("angle")] = rotationDegrees();
        // Explicit world-space centre rather than rotCenter=bbox_center: the filter
        // reads mesh.bbox, which is the *untransformed* local box, yet composes its
        // matrix in world space -- so the two only agree while the layer transform is
        // identity, and the preview would drift from the commit on a moved layer.
        params[QStringLiteral("rotCenter")] = QStringLiteral("custom");
        params[QStringLiteral("customCenter")] = m_pivot;
        break;
    }
    case Gesture::Scale: {
        const QVector3D s(delta(0, 0), delta(1, 1), delta(2, 2));
        key = QString::fromLatin1(kScaleKey);
        params[QStringLiteral("axisX")] = double(s.x());
        params[QStringLiteral("axisY")] = double(s.y());
        params[QStringLiteral("axisZ")] = double(s.z());
        params[QStringLiteral("uniformFlag")] = false;
        params[QStringLiteral("scaleCenter")] = QStringLiteral("custom");
        params[QStringLiteral("customCenter")] = m_pivot;
        break;
    }
    case Gesture::None:
        clearGesture();
        return;
    }
    // Keep it a layer matrix: baking every gesture would deep-copy the mesh.
    params[QStringLiteral("Freeze")] = false;

    const int previousCurrent = doc->currentMeshIndex();
    doc->setCurrentMeshIndex(m_meshIndex);
    const MeshFilterRunResult result = doc->runFilter(key, params);
    if (previousCurrent != m_meshIndex)
        doc->setCurrentMeshIndex(previousCurrent);

    if (!result.success) {
        doc->writeLog(QObject::tr("Transform failed: %1").arg(result.errorMessage),
                      Document::LogSource::Application, Document::LogLevel::Error);
    }
    clearGesture();
}

void TransformTool::abortGesture()
{
    if (m_gesture == Gesture::None)
        return;
    if (m_view && m_view->document()) {
        Document *doc = m_view->document();
        if (m_meshIndex >= 0 && m_meshIndex < doc->meshCount())
            doc->mesh(m_meshIndex).transform = m_originalTransform;
    }
    clearGesture();
}

void TransformTool::clearGesture()
{
    m_gesture = Gesture::None;
    m_meshIndex = -1;
    m_axis = -1;
    m_planeConstraint = false;
    m_numeric.clear();
    if (m_view) {
            m_view->update();
    }
}

void TransformTool::cancelGesture()
{
    abortGesture();
}

bool TransformTool::mousePress(QMouseEvent *e)
{
    if (!e || m_gesture == Gesture::None)
        return false;
    if (e->button() == Qt::LeftButton) {
        commitGesture();
        return true;
    }
    if (e->button() == Qt::RightButton) {
        abortGesture();
        return true;
    }
    return false;
}

bool TransformTool::mouseMove(QMouseEvent *e)
{
    if (!e || m_gesture == Gesture::None)
        return false;
    m_currentMouse = e->position();
    if (m_numeric.isEmpty())
        updatePreview();
    return true;
}

bool TransformTool::mouseRelease(QMouseEvent *e)
{
    (void) e;
    // The gesture is driven by motion, not by holding a button down.
    return m_gesture != Gesture::None;
}

bool TransformTool::keyPress(QKeyEvent *e)
{
    if (!e)
        return false;
    const int k = e->key();

    if (m_gesture == Gesture::None) {
        switch (k) {
        case Qt::Key_G: return beginGesture(Gesture::Translate);
        case Qt::Key_R: return beginGesture(Gesture::Rotate);
        case Qt::Key_S: return beginGesture(Gesture::Scale);
        default: return false;
        }
    }

    switch (k) {
    case Qt::Key_G: abortGesture(); return beginGesture(Gesture::Translate);
    case Qt::Key_R: abortGesture(); return beginGesture(Gesture::Rotate);
    case Qt::Key_S: abortGesture(); return beginGesture(Gesture::Scale);
    case Qt::Key_Escape:
        abortGesture();
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        commitGesture();
        return true;
    case Qt::Key_X:
    case Qt::Key_Y:
    case Qt::Key_Z: {
        const int axis = (k == Qt::Key_X) ? 0 : (k == Qt::Key_Y) ? 1 : 2;
        const bool plane = e->modifiers().testFlag(Qt::ShiftModifier);
        if (m_axis == axis && m_planeConstraint == plane) {
            m_axis = -1;             // pressing the same constraint again clears it
            m_planeConstraint = false;
        } else {
            m_axis = axis;
            m_planeConstraint = plane;
        }
        updatePreview();
        return true;
    }
    case Qt::Key_Backspace:
        if (!m_numeric.isEmpty()) {
            m_numeric.chop(1);
            updatePreview();
        }
        return true;
    default:
        break;
    }

    const QString text = e->text();
    if (text.size() == 1) {
        const QChar c = text.at(0);
        if (c.isDigit() || c == QLatin1Char('.') || c == QLatin1Char('-')) {
            // Typing a bare number is only meaningful once an axis is chosen --
            // except for scale, where it reads as a uniform factor.
            if (m_axis < 0 && m_gesture != Gesture::Scale) {
                if (Document *doc = m_view->document()) {
                    doc->writeLog(
                        QObject::tr("Transform: choose an axis with X, Y or Z before typing a value."),
                        Document::LogSource::Application, Document::LogLevel::Warning);
                }
                return true;
            }
            m_numeric.append(c);
            updatePreview();
            return true;
        }
    }
    return false;
}

double TransformTool::rotationDegrees() const
{
    const QPointF c = pivotScreen();
    bool ok = false;
    if (!m_numeric.isEmpty()) {
        const double v = m_numeric.toDouble(&ok);
        if (ok)
            return std::clamp(v, -360.0, 360.0);
    }
    const double a0 = std::atan2(m_startMouse.y() - c.y(), m_startMouse.x() - c.x());
    const double a1 = std::atan2(m_currentMouse.y() - c.y(), m_currentMouse.x() - c.x());
    return std::clamp(-qRadiansToDegrees(a1 - a0), -360.0, 360.0);
}

QString TransformTool::readout() const
{
    if (m_gesture == Gesture::None)
        return {};
    QString verb;
    switch (m_gesture) {
    case Gesture::Translate: verb = QObject::tr("Move"); break;
    case Gesture::Rotate: verb = QObject::tr("Rotate"); break;
    case Gesture::Scale: verb = QObject::tr("Scale"); break;
    default: break;
    }
    QString constraint;
    if (m_axis >= 0)
        constraint = m_planeConstraint ? QObject::tr(" · plane ⊥ %1").arg(axisName(m_axis))
                                       : QObject::tr(" · axis %1").arg(axisName(m_axis));
    QString value;
    if (!m_numeric.isEmpty()) {
        value = QObject::tr(" · %1").arg(m_numeric);
    } else if (m_gesture == Gesture::Rotate) {
        value = QObject::tr(" · %1°").arg(QString::number(rotationDegrees(), 'f', 1));
    }
    return verb + constraint + value;
}

void TransformTool::paintOverlay(QPainter &painter,
                                 const QMatrix4x4 &worldToClip,
                                 const QSize &viewportSize)
{
    (void) worldToClip;
    if (m_gesture == Gesture::None)
        return;

    const QPointF c = pivotScreen();
    painter.save();
    QPen pen(QColor(255, 200, 0));
    pen.setWidthF(1.5);
    painter.setPen(pen);

    // Anchor line from the pivot to the pointer, so the gesture reads at a glance.
    painter.drawLine(c, m_currentMouse);
    painter.drawEllipse(c, 4.0, 4.0);

    const QString text = readout();
    if (!text.isEmpty()) {
        const QRectF box(12, viewportSize.height() - 34, viewportSize.width() - 24, 22);
        painter.setPen(QColor(255, 255, 255));
        painter.drawText(box, Qt::AlignLeft | Qt::AlignVCenter, text);
    }
    painter.restore();
}
