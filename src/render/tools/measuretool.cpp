#include "measuretool.h"

#include "document.h"
#include "renderwidget.h"

#include <vcg/space/distance3.h>

#include <QFileDialog>
#include <QFileInfo>
#include <QApplication>
#include <QKeyEvent>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QSaveFile>
#include <QTextStream>
#include <QVector4D>

#include <cmath>

QString MeasureTool::id() const
{
    return QStringLiteral("measure");
}

QString MeasureTool::name() const
{
    return QObject::tr("Measuring Tool");
}

QString MeasureTool::statusHint() const
{
    return QObject::tr(
        "Measure: click two surface points — C clear, Backspace undo last, P print, S save; Tab: camera, Esc: exit");
}

QString MeasureTool::badgeDetail() const
{
    return QObject::tr("%1 — select %2 point")
        .arg(m_measurements.size())
        .arg(m_pendingPoint >= 0 ? QObject::tr("second") : QObject::tr("first"));
}

QString MeasureTool::iconPath() const
{
    return QStringLiteral(":/img/icon_measure.png");
}

QCursor MeasureTool::cursor() const
{
    return QCursor(QPixmap(QStringLiteral(":/img/cur_measure.png")), 15, 15);
}

void MeasureTool::activate(RenderWidget &view)
{
    InteractiveTool::activate(view);
    m_points.clear();
    m_measurements.clear();
    m_pendingPoint = -1;
}

void MeasureTool::deactivate(bool commit)
{
    m_points.clear();
    m_measurements.clear();
    m_pendingPoint = -1;
    InteractiveTool::deactivate(commit);
}

bool MeasureTool::mousePress(QMouseEvent *e)
{
    if (!m_view || !e || e->button() != Qt::LeftButton
        || m_view->viewMode() != RenderWidget::ViewMode::Scene3D)
        return false;
    m_pressedPoint = pointAt(e->position());
    m_pressPos = e->position().toPoint();
    m_dragging = false;
    if (m_pressedPoint < 0) {
        m_pickAction = PickAction::AddPoint;
        m_view->requestSurfacePick(m_pressPos);
    }
    return true;
}

bool MeasureTool::mouseMove(QMouseEvent *e)
{
    if (!m_view || !e)
        return false;
    if (!(e->buttons() & Qt::LeftButton)) {
        m_hoverPoint = pointAt(e->position());
        return m_hoverPoint >= 0;
    }
    if (m_pressedPoint < 0)
        return false;

    if (!m_dragging
        && (e->position().toPoint() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
        m_dragging = true;
        m_dragPoint = m_pressedPoint;
    }
    if (m_dragging) {
        // GPU picking constrains every drag sample to the rendered mesh surface.
        m_pickAction = PickAction::DragPoint;
        m_view->requestSurfacePick(e->position().toPoint());
    }
    return true;
}

bool MeasureTool::mouseRelease(QMouseEvent *e)
{
    if (!e || e->button() != Qt::LeftButton || m_pressedPoint < 0)
        return false;
    if (m_dragging) {
        // Request the release position too, since the final move event may have
        // been coalesced by Qt.
        m_pickAction = PickAction::DragPoint;
        m_view->requestSurfacePick(e->position().toPoint());
    } else if (m_pendingPoint >= 0) {
        finishSegment(std::size_t(m_pressedPoint));
    } else {
        // A click on an existing endpoint starts a connected segment.  Because
        // segments share point indices, later dragging keeps the junction joined.
        m_pendingPoint = m_pressedPoint;
        m_pendingPointIsNew = false;
    }
    m_pressedPoint = -1;
    m_dragging = false;
    return true;
}

void MeasureTool::onSurfacePicked(const SurfacePick &result)
{
    if (!m_view || !result.hit)
        return;
    if (m_pickAction == PickAction::DragPoint) {
        if (m_dragPoint >= 0 && std::size_t(m_dragPoint) < m_points.size()) {
            m_points[std::size_t(m_dragPoint)] = result.worldPos;
            updateLengths(std::size_t(m_dragPoint));
        }
        m_pickAction = PickAction::None;
        return;
    }
    if (m_pickAction != PickAction::AddPoint)
        return;
    m_pickAction = PickAction::None;

    m_points.push_back(result.worldPos);
    const std::size_t point = m_points.size() - 1;
    if (m_pendingPoint < 0) {
        m_pendingPoint = int(point);
        m_pendingPointIsNew = true;
        return;
    }
    finishSegment(point);
}

void MeasureTool::finishSegment(std::size_t point)
{
    if (m_pendingPoint < 0 || std::size_t(m_pendingPoint) >= m_points.size()
        || point >= m_points.size() || std::size_t(m_pendingPoint) == point)
        return;
    const std::size_t first = std::size_t(m_pendingPoint);
    const QVector3D &a = m_points[first];
    const QVector3D &b = m_points[point];
    m_measurements.push_back({first, point, vcg::Distance(
        vcg::Point3d(a.x(), a.y(), a.z()), vcg::Point3d(b.x(), b.y(), b.z()))});
    m_pendingPoint = -1;
    m_pendingPointIsNew = false;
    if (Document *doc = m_view->document()) {
        const Measurement &m = m_measurements.back();
        doc->writeLog(
            QObject::tr("Distance M%1: %2").arg(m_measurements.size() - 1).arg(m.length, 0, 'g', 12),
            Document::LogSource::Application);
    }
}

void MeasureTool::updateLengths(std::size_t point)
{
    for (Measurement &m : m_measurements) {
        if (m.a != point && m.b != point)
            continue;
        const QVector3D &a = m_points[m.a];
        const QVector3D &b = m_points[m.b];
        m.length = vcg::Distance(
            vcg::Point3d(a.x(), a.y(), a.z()), vcg::Point3d(b.x(), b.y(), b.z()));
    }
}

void MeasureTool::clearPendingPoint()
{
    // A point created by a surface click is unreferenced until its segment is
    // completed, so it is safe to remove. Existing endpoints are merely deselected.
    if (m_pendingPointIsNew && m_pendingPoint == int(m_points.size()) - 1)
        m_points.pop_back();
    m_pendingPoint = -1;
    m_pendingPointIsNew = false;
}

void MeasureTool::cancelGesture()
{
    clearPendingPoint();
    m_pressedPoint = m_dragPoint = -1;
    m_pickAction = PickAction::None;
}

bool MeasureTool::keyPress(QKeyEvent *e)
{
    if (!e)
        return false;
    if (e->key() == Qt::Key_C) {
        m_points.clear();
        m_measurements.clear();
        m_pendingPoint = -1;
        m_pendingPointIsNew = false;
        return true;
    }
    if (e->key() == Qt::Key_Backspace || e->key() == Qt::Key_Delete) {
        if (m_pendingPoint >= 0)
            clearPendingPoint();
        else if (!m_measurements.empty())
            m_measurements.pop_back();
        return true;
    }
    if (e->key() == Qt::Key_P) {
        printMeasurements();
        return true;
    }
    if (e->key() == Qt::Key_S) {
        saveMeasurements();
        return true;
    }
    return false;
}

void MeasureTool::printMeasurements() const
{
    if (!m_view || !m_view->document())
        return;
    Document *doc = m_view->document();
    doc->writeLog(QObject::tr("Measurements: ID, distance, [point A], [point B]"),
                  Document::LogSource::Application);
    for (std::size_t i = 0; i < m_measurements.size(); ++i) {
        const Measurement &m = m_measurements[i];
        const QVector3D &a = m_points[m.a];
        const QVector3D &b = m_points[m.b];
        doc->writeLog(
            QStringLiteral("M%1: %2 [%3, %4, %5] [%6, %7, %8]")
                .arg(i).arg(m.length, 0, 'g', 12)
                .arg(a.x(), 0, 'g', 12).arg(a.y(), 0, 'g', 12).arg(a.z(), 0, 'g', 12)
                .arg(b.x(), 0, 'g', 12).arg(b.y(), 0, 'g', 12).arg(b.z(), 0, 'g', 12),
            Document::LogSource::Application);
    }
}

void MeasureTool::saveMeasurements() const
{
    if (!m_view || !m_view->document() || m_measurements.empty())
        return;
    Document *doc = m_view->document();
    QString base = QStringLiteral("measurements");
    const int meshIndex = doc->currentMeshIndex();
    if (meshIndex >= 0 && meshIndex < doc->meshCount()) {
        const Document::MeshEntry &entry = doc->mesh(meshIndex);
        base = QFileInfo(entry.sourcePath).completeBaseName();
        if (base.isEmpty())
            base = entry.name;
    }
    const QString path = QFileDialog::getSaveFileName(
        m_view,
        QObject::tr("Save Measurements"),
        QStringLiteral("%1_measurements.tsv").arg(base),
        QObject::tr("Tab-separated values (*.tsv);;Text files (*.txt)"));
    if (path.isEmpty())
        return;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        doc->writeLog(QObject::tr("Cannot save measurements to '%1'.").arg(path),
                      Document::LogSource::Error);
        return;
    }
    QTextStream out(&file);
    out.setRealNumberNotation(QTextStream::SmartNotation);
    out.setRealNumberPrecision(17);
    out << "id\tlength\tax\tay\taz\tbx\tby\tbz\n";
    for (std::size_t i = 0; i < m_measurements.size(); ++i) {
        const Measurement &m = m_measurements[i];
        const QVector3D &a = m_points[m.a];
        const QVector3D &b = m_points[m.b];
        out << 'M' << i << '\t' << m.length
            << '\t' << a.x() << '\t' << a.y() << '\t' << a.z()
            << '\t' << b.x() << '\t' << b.y() << '\t' << b.z() << '\n';
    }
    if (!file.commit()) {
        doc->writeLog(QObject::tr("Cannot finish writing measurements to '%1'.").arg(path),
                      Document::LogSource::Error);
        return;
    }
    doc->writeLog(QObject::tr("Measurements saved to '%1'.").arg(path),
                  Document::LogSource::Application);
}

void MeasureTool::paintOverlay(
    QPainter &painter,
    const QMatrix4x4 &worldToClip,
    const QSize &viewportSize)
{
    auto project = [&](const QVector3D &world, QPointF &screen) {
        const QVector4D clip = worldToClip * QVector4D(world, 1.0f);
        if (clip.w() <= 1e-6f)
            return false;
        const QVector3D ndc = clip.toVector3DAffine();
        screen = QPointF(
            (ndc.x() * 0.5f + 0.5f) * viewportSize.width(),
            (1.0f - (ndc.y() * 0.5f + 0.5f)) * viewportSize.height());
        return std::isfinite(screen.x()) && std::isfinite(screen.y());
    };

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(QColor(85, 170, 255));
    for (std::size_t i = 0; i < m_measurements.size(); ++i) {
        const Measurement &m = m_measurements[i];
        QPointF a, b;
        if (!project(m.a, a) || !project(m.b, b))
            continue;
        QPen pen(QColor(85, 170, 255), 2.0);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.drawEllipse(a, 4.0, 4.0);
        painter.drawEllipse(b, 4.0, 4.0);
        const QString label = QStringLiteral("M%1: %2").arg(i).arg(m.length, 0, 'g', 8);
        const QRectF textRect = painter.fontMetrics().boundingRect(label).adjusted(-5, -3, 5, 3);
        const QRectF placed = textRect.translated(b + QPointF(9, -9) - textRect.topLeft());
        painter.fillRect(placed, QColor(20, 20, 24, 190));
        painter.setPen(Qt::white);
        painter.drawText(placed, Qt::AlignCenter, label);
    }
    if (m_hasFirstPoint) {
        QPointF p;
        if (project(m_firstPoint, p)) {
            painter.setBrush(QColor(255, 170, 85));
            painter.setPen(QPen(QColor(255, 220, 160), 2.0));
            painter.drawEllipse(p, 5.0, 5.0);
        }
    }
}

std::vector<ToolLineSegment> MeasureTool::depthCuedLines() const
{
    std::vector<ToolLineSegment> lines;
    lines.reserve(m_measurements.size());
    for (const Measurement &m : m_measurements)
        lines.push_back({m.a, m.b});
    return lines;
}
