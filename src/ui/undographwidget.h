#pragma once

#include "document.h"
#include <QAbstractScrollArea>
#include <QMap>
#include <QPixmap>
#include <QVector>

// UndoGraphWidget renders the undo history as a git-style lane graph.
//
// Layout:
//   - Each row = one node in the undo tree (newest at top, oldest at bottom).
//   - Each lane (column) carries one branch.  The main/current branch is always
//     in lane 0 (leftmost).  When a branch point is first encountered a new lane
//     is assigned and retained until the branch merges back (i.e. hits a node
//     that is shared with another lane).
//   - A filled circle is drawn on the node's lane.  A dot indicates current.
//   - Vertical lines connect parent↔child within the same lane.
//   - Diagonal lines connect a branch tip back to the row where the parent sits.
//
// Interaction:
//   - Double-click a row → jumpToNode(nodeId)
//   - Hover → show thumbnail popup (via signal)

class UndoGraphWidget : public QAbstractScrollArea
{
    Q_OBJECT
public:
    explicit UndoGraphWidget(QWidget *parent = nullptr);
    static QSize thumbnailSize() { return QSize(kThumbW, kThumbH); }

    void setNodes(const QVector<UndoTreeNodeInfo> &nodes,
                  int currentNodeId,
                  const QMap<int, QPixmap> &thumbnails);

signals:
    // Emitted when the user requests to jump to a node.
    // withCamera=true  → restore data AND camera (Ctrl/Cmd+double-click or context menu)
    // withCamera=false → restore data only (plain double-click or context menu)
    void nodeActivated(int nodeId, bool withCamera);
    // Emitted when the user requests to store the current view camera into a node.
    void nodeUpdateCameraRequested(int nodeId);
    // Emitted when the user requests to make a node the new history root.
    void nodeMakeRootRequested(int nodeId);
    // Emitted when the user requests to delete all descendants of a node.
    void nodePurgeBranchRequested(int nodeId);
    // Emitted when the user wants to vary the action that produced a node: return to the
    // state it was invoked from, with its filter reopened on the same parameters.
    void nodeReopenFilterRequested(int nodeId);
    // Emitted when the user requests to keep only the path root→current, removing all branches.
    void linearizeHistoryRequested();
    void generatePythonScriptRequested();
    void nodeHovered(int nodeId, const QPoint &globalPos); // emitted only when hovering a thumbnail
    void nodeUnhovered();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    QSize sizeHint() const override;

private:
    struct Row {
        int nodeId    = -1;
        int parentId  = -1;
        int lane      = 0;  // which column this node occupies
        bool isCurrent        = false;
        bool isOnCurrentPath  = false;
        QString label;
        QString filterKey; // non-empty when a filter produced this node

        // For each active lane at this row: which lane carries a continuous line
        // from the row above to the row below.  Used to draw pass-through verticals.
        QVector<int> activeParentLanes; // lanes still open below this row
    };

    void rebuildRows();
    int  rowAt(int y) const;
    QRect rowRect(int row) const;
    QRect thumbnailRect(int row) const;
    int  laneX(int lane) const;
    void updateScrollBars();

    // ---- data ----
    QVector<UndoTreeNodeInfo> m_nodes; // original tree info (DFS pre-order)
    int                                 m_currentNodeId = -1;
    QMap<int, QPixmap>                  m_thumbnails;

    // ---- layout ----
    QVector<Row>  m_rows;     // one per visible row, row 0 = newest (top)
    int           m_laneCount = 1;

    // ---- geometry constants ----
    static constexpr int kThumbW      = 80;  // thumbnail width  (2:1 aspect ratio)
    static constexpr int kThumbH      = 40;  // thumbnail height
    static constexpr int kRowHeight   = 40;  // keep rows visually compact
    static constexpr int kLaneWidth   = 20;
    static constexpr int kDotRadius   = 6;
    static constexpr int kTextLeft    = 8;   // gap between graph area and text

    int m_hoveredRow = -1;
    int m_hoveredThumbnailRow = -1;
};
