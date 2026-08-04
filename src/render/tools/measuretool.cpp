#include "measuretool.h"

#include "document.h"
#include "renderwidget.h"

#include <vcg/space/distance3.h>

#include <QFileDialog>
#include <QFileInfo>
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
        .arg(m_hasFirstPoint ? QObject::tr("second") : QObject::tr("first"));
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
    m_measurements.clear();
    m_hasFirstPoint = false;
}

void MeasureTool::deactivate(bool commit)
{
    m_measurements.clear();
    m_hasFirstPoint = false;
    InteractiveTool::deactivate(commit);
}

bool MeasureTool::mousePress(QMouseEvent *e)
{
    if (!m_view || !e || e->button() != Qt::LeftButton
        || m_view->viewMode() != RenderWidget::ViewMode::Scene3D)
        return false;
    m_view->requestSurfacePick(e->position().toPoint());
    return true;
}

void MeasureTool::onSurfacePicked(const SurfacePick &result)
{
    if (!m_view || !result.hit)
        return;
    if (!m_hasFirstPoint) {
        m_firstPoint = result.worldPos;
        m_hasFirstPoint = true;
        return;
    }

    const vcg::Point3d a(m_firstPoint.x(), m_firstPoint.y(), m_firstPoint.z());
    const vcg::Point3d b(result.worldPos.x(), result.worldPos.y(), result.worldPos.z());
    m_measurements.push_back({m_firstPoint, result.worldPos, vcg::Distance(a, b)});
    m_hasFirstPoint = false;

    if (Document *doc = m_view->document()) {
        const Measurement &m = m_measurements.back();
        doc->writeLog(
            QObject::tr("Distance M%1: %2").arg(m_measurements.size() - 1).arg(m.length, 0, 'g', 12),
            Document::LogSource::Application);
    }
}

void MeasureTool::cancelGesture()
{
    m_hasFirstPoint = false;
}

bool MeasureTool::keyPress(QKeyEvent *e)
{
    if (!e)
        return false;
    if (e->key() == Qt::Key_C) {
        m_measurements.clear();
        m_hasFirstPoint = false;
        return true;
    }
    if (e->key() == Qt::Key_Backspace || e->key() == Qt::Key_Delete) {
        if (m_hasFirstPoint)
            m_hasFirstPoint = false;
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
        doc->writeLog(
            QStringLiteral("M%1: %2 [%3, %4, %5] [%6, %7, %8]")
                .arg(i).arg(m.length, 0, 'g', 12)
                .arg(m.a.x(), 0, 'g', 12).arg(m.a.y(), 0, 'g', 12).arg(m.a.z(), 0, 'g', 12)
                .arg(m.b.x(), 0, 'g', 12).arg(m.b.y(), 0, 'g', 12).arg(m.b.z(), 0, 'g', 12),
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
        out << 'M' << i << '\t' << m.length
            << '\t' << m.a.x() << '\t' << m.a.y() << '\t' << m.a.z()
            << '\t' << m.b.x() << '\t' << m.b.y() << '\t' << m.b.z() << '\n';
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
    QPen pen(QColor(85, 170, 255), 2.0);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(QColor(85, 170, 255));
    for (std::size_t i = 0; i < m_measurements.size(); ++i) {
        const Measurement &m = m_measurements[i];
        QPointF a, b;
        if (!project(m.a, a) || !project(m.b, b))
            continue;
        painter.drawLine(a, b);
        painter.drawEllipse(a, 4.0, 4.0);
        painter.drawEllipse(b, 4.0, 4.0);
        const QString label = QStringLiteral("M%1: %2").arg(i).arg(m.length, 0, 'g', 8);
        const QRectF textRect = painter.fontMetrics().boundingRect(label).adjusted(-5, -3, 5, 3);
        const QRectF placed = textRect.translated(b + QPointF(9, -9) - textRect.topLeft());
        painter.fillRect(placed, QColor(20, 20, 24, 190));
        painter.setPen(Qt::white);
        painter.drawText(placed, Qt::AlignCenter, label);
        painter.setPen(pen);
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
