#include "undographwidget.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QMap>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QSet>
#include <QToolTip>
#include <algorithm>

// Single colour for all lanes – use the palette's highlight colour.
static QColor laneColor(int /*lane*/, const QPalette &pal)
{
    return pal.color(QPalette::Highlight);
}

// ─────────────────────────────────────────────────────────────────────────────
UndoGraphWidget::UndoGraphWidget(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    setMouseTracking(true);
    if (viewport())
        viewport()->setMouseTracking(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void UndoGraphWidget::setNodes(const QVector<Document::UndoTreeNodeInfo> &nodes,
                               int currentNodeId,
                               const QMap<int, QPixmap> &thumbnails)
{
    m_nodes         = nodes;
    m_currentNodeId = currentNodeId;
    m_thumbnails    = thumbnails;
    m_hoveredRow    = -1;
    rebuildRows();
    updateScrollBars();
    viewport()->update();
}

// ─────────────────────────────────────────────────────────────────────────────
// rebuildRows  – trivial: lane comes directly from UndoTreeNodeInfo.lane,
// assigned once at creation in Document::pushUndoStep.
// Display order: current-path nodes deepest-first (newest at top), then
// off-path nodes grouped by lane.
// ─────────────────────────────────────────────────────────────────────────────
void UndoGraphWidget::rebuildRows()
{
    m_rows.clear();
    m_laneCount = 1;

    if (m_nodes.isEmpty())
        return;

    QVector<Document::UndoTreeNodeInfo> sorted = m_nodes;
    // Sort by nodeId descending: nodeIds are assigned sequentially at creation,
    // so this always gives newest-at-top regardless of where the cursor is.
    // Same depth / same time → lower lane first.
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
        if (a.nodeId != b.nodeId) return a.nodeId > b.nodeId;
        return a.lane < b.lane;
    });

    for (const auto &info : sorted) {
        Row row;
        row.nodeId          = info.nodeId;
        row.parentId        = info.parentId;
        row.lane            = info.lane;
        row.isCurrent       = info.isCurrent;
        row.isOnCurrentPath = info.isOnCurrentPath;
        row.label           = info.label.isEmpty() ? tr("Initial state") : info.label;
        m_rows.append(row);
        m_laneCount = std::max(m_laneCount, info.lane + 1);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void UndoGraphWidget::updateScrollBars()
{
    const int contentH = m_rows.size() * kRowHeight;
    const int graphW   = m_laneCount * kLaneWidth + kLaneWidth / 2;
    // Text column gets the remaining width.
    const int textW    = std::max(200, viewport()->width() - graphW - kThumbW - kTextLeft * 2);
    const int contentW = graphW + kTextLeft + textW + kThumbW;

    verticalScrollBar()->setRange(0, std::max(0, contentH - viewport()->height()));
    verticalScrollBar()->setPageStep(viewport()->height());
    verticalScrollBar()->setSingleStep(kRowHeight);
    horizontalScrollBar()->setRange(0, std::max(0, contentW - viewport()->width()));
    horizontalScrollBar()->setPageStep(viewport()->width());
    horizontalScrollBar()->setSingleStep(20);
}

int UndoGraphWidget::laneX(int lane) const
{
    // Centre of the lane circle in content coordinates (before scroll offset).
    return kLaneWidth / 2 + lane * kLaneWidth;
}

int UndoGraphWidget::rowAt(int y) const
{
    const int cy = y + verticalScrollBar()->value();
    const int r  = cy / kRowHeight;
    return (r >= 0 && r < m_rows.size()) ? r : -1;
}

QRect UndoGraphWidget::rowRect(int row) const
{
    const int y = row * kRowHeight - verticalScrollBar()->value();
    return QRect(0, y, viewport()->width(), kRowHeight);
}

QSize UndoGraphWidget::sizeHint() const
{
    return QSize(280, 200);
}

// ─────────────────────────────────────────────────────────────────────────────
// paintEvent
// ─────────────────────────────────────────────────────────────────────────────
void UndoGraphWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing);

    const int scrollY = verticalScrollBar()->value();
    const int scrollX = horizontalScrollBar()->value();
    const int vw      = viewport()->width();
    const int vh      = viewport()->height();

    // Background
    p.fillRect(viewport()->rect(), palette().color(QPalette::Base));

    if (m_rows.isEmpty()) {
        p.setPen(palette().color(QPalette::Mid));
        p.drawText(viewport()->rect(), Qt::AlignCenter, tr("No undo history"));
        return;
    }

    const int graphAreaW = m_laneCount * kLaneWidth + kLaneWidth / 2;
    const int thumbColX  = vw - kThumbW - 4 + scrollX; // right-aligned

    // Build a map nodeId → row index for connector drawing.
    QMap<int, int> nodeRow;
    for (int i = 0; i < m_rows.size(); ++i)
        nodeRow[m_rows[i].nodeId] = i;

    // ── pass 1: row backgrounds (hover / current) ────────────────────────
    for (int ri = 0; ri < m_rows.size(); ++ri) {
        const Row &row = m_rows[ri];
        const int  top = ri * kRowHeight - scrollY;
        if (top > vh || top + kRowHeight < 0)
            continue;
        if (row.isCurrent)
            p.fillRect(QRect(0, top, vw, kRowHeight),
                       QColor(173, 216, 230)); // light blue
        else if (ri == m_hoveredRow)
            p.fillRect(QRect(0, top, vw, kRowHeight),
                       palette().color(QPalette::AlternateBase));
    }

    // ── pass 2: connector lines ───────────────────────────────────────────
    for (int ri = 0; ri < m_rows.size(); ++ri) {
        const Row &row = m_rows[ri];
        if (row.parentId < 0)
            continue;
        if (!nodeRow.contains(row.parentId))
            continue;

        const int parentRowIdx = nodeRow[row.parentId];
        const Row &parentRow   = m_rows[parentRowIdx];

        const int x1 = laneX(row.lane)       - scrollX;
        const int y1 = ri * kRowHeight + kRowHeight / 2 - scrollY;
        const int x2 = laneX(parentRow.lane) - scrollX;
        const int y2 = parentRowIdx * kRowHeight + kRowHeight / 2 - scrollY;

        // Skip entirely off-screen connectors.
        if ((y1 < -kRowHeight && y2 < -kRowHeight) || (y1 > vh + kRowHeight && y2 > vh + kRowHeight))
            continue;

        QPen linePen(laneColor(row.lane, palette()), 2.0f);
        linePen.setCapStyle(Qt::RoundCap);
        p.setPen(linePen);

        if (row.lane == parentRow.lane) {
            p.drawLine(x1, y1, x2, y2);
        } else {
            // Draw straight on the child's lane all the way down to just above
            // the parent's row, then a short curve into the parent.
            // This places the visible kink at the parent row (= the actual branch
            // point), not halfway between child and parent.
            const int cornerY = y2 - kRowHeight / 2;
            QPainterPath path;
            path.moveTo(x1, y1);
            path.lineTo(x1, cornerY);
            path.quadTo(x1, y2, x2, y2);
            p.strokePath(path, linePen);
        }
    }

    // ── pass 3: dots, thumbnails and labels ──────────────────────────────
    for (int ri = 0; ri < m_rows.size(); ++ri) {
        const Row  &row = m_rows[ri];
        const int   cy  = ri * kRowHeight + kRowHeight / 2 - scrollY;
        const int   top = ri * kRowHeight - scrollY;

        if (cy < -kRowHeight || cy > vh + kRowHeight)
            continue;

        const int cx = laneX(row.lane) - scrollX;
        const QColor col = laneColor(row.lane, palette());

        // Node dot
        if (row.isCurrent) {
            // Filled circle + outer ring
            p.setPen(QPen(col.lighter(150), 2));
            p.setBrush(col);
            p.drawEllipse(QPoint(cx, cy), kDotRadius, kDotRadius);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(col.lighter(180), 1.5));
            p.drawEllipse(QPoint(cx, cy), kDotRadius + 3, kDotRadius + 3);
        } else {
            p.setPen(QPen(col, 2));
            p.setBrush(palette().color(QPalette::Base));
            p.drawEllipse(QPoint(cx, cy), kDotRadius, kDotRadius);
        }

        // Thumbnail (right-aligned)
        const QPixmap thumb = m_thumbnails.value(row.nodeId);
        if (!thumb.isNull()) {
            const int tx = thumbColX - scrollX;
            const int ty = top + (kRowHeight - kThumbH) / 2;
            p.drawPixmap(tx, ty, kThumbW, kThumbH, thumb);
        }

        // Text
        const int textX = graphAreaW + kTextLeft - scrollX;
        const int textW = std::max(10, thumbColX - scrollX - graphAreaW - kTextLeft * 2);
        const QRect textRect(textX, top, textW, kRowHeight);

        QFont f = p.font();
        const bool bold = row.isCurrent;
        if (f.bold() != bold) { f.setBold(bold); p.setFont(f); }

        QColor textColor = palette().color(QPalette::Text);
        if (!row.isOnCurrentPath)
            textColor = palette().color(QPalette::Mid);
        p.setPen(textColor);
        p.drawText(textRect, Qt::AlignVCenter | Qt::TextSingleLine,
                   p.fontMetrics().elidedText(row.label, Qt::ElideRight, textW));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void UndoGraphWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    const int ri = rowAt(event->pos().y());
    if (ri >= 0) {
        const bool withCamera =
            event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier);
        emit nodeActivated(m_rows[ri].nodeId, withCamera);
    }
    QAbstractScrollArea::mouseDoubleClickEvent(event);
}

void UndoGraphWidget::contextMenuEvent(QContextMenuEvent *event)
{
    const int ri = rowAt(event->pos().y());
    if (ri < 0) {
        QAbstractScrollArea::contextMenuEvent(event);
        return;
    }
    const int nodeId = m_rows[ri].nodeId;

    QMenu menu(this);
    QAction *restoreAct      = menu.addAction(tr("Restore state"));
    QAction *restoreCamAct   = menu.addAction(tr("Restore state and camera"));
    menu.addSeparator();
    QAction *updateCamAct    = menu.addAction(tr("Update camera"));

    QAction *chosen = menu.exec(event->globalPos());
    if (chosen == restoreAct)
        emit nodeActivated(nodeId, false);
    else if (chosen == restoreCamAct)
        emit nodeActivated(nodeId, true);
    else if (chosen == updateCamAct)
        emit nodeUpdateCameraRequested(nodeId);
}

void UndoGraphWidget::mouseMoveEvent(QMouseEvent *event)
{
    const int ri = rowAt(event->pos().y());
    if (ri != m_hoveredRow) {
        m_hoveredRow = ri;
        viewport()->update();
    }
    if (ri >= 0)
        emit nodeHovered(m_rows[ri].nodeId, viewport()->mapToGlobal(event->pos()));
    else
        emit nodeUnhovered();
    QAbstractScrollArea::mouseMoveEvent(event);
}

void UndoGraphWidget::leaveEvent(QEvent *event)
{
    if (m_hoveredRow >= 0) {
        m_hoveredRow = -1;
        viewport()->update();
    }
    emit nodeUnhovered();
    QAbstractScrollArea::leaveEvent(event);
}

void UndoGraphWidget::resizeEvent(QResizeEvent *event)
{
    updateScrollBars();
    QAbstractScrollArea::resizeEvent(event);
}
