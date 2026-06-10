#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <QTableWidget>

class Document;
class QStackedWidget;
class QSplitter;

class LayerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit LayerWidget(Document *doc, QWidget *parent = nullptr);

    enum class ViewMode {
        Tree,
        Table
    };

    ViewMode viewMode() const { return m_viewMode; }
    void setViewMode(ViewMode mode);
    void toggleViewMode();

signals:
    void filterActionRequested(const QString &filterKey);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void rebuild();
    void rebuildTree();
    void rebuildTable();
    int kvRoleForColumn(int col) const { return Qt::UserRole + 100 + col; }

    enum class LayerItemKind {
        None,
        Mesh,
        Raster
    };
    struct LayerItemRef {
        LayerItemKind kind = LayerItemKind::None;
        int index = -1;
    };
    LayerItemRef layerRefForItem(QTreeWidgetItem *item) const;
    void updateCurrentItemVisuals();
    void savePlaneImage(int rasterIndex, int planeIndex);

private slots:
    void onTreeItemChanged(QTreeWidgetItem *item, int column);
    void onTreeCurrentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);
    void onMeshTableCellChanged(QTableWidgetItem *item);
    void onRasterTableCellChanged(QTableWidgetItem *item);
    void onMeshTableCurrentItemChanged(QTableWidgetItem *current, QTableWidgetItem *previous);
    void onRasterTableCurrentItemChanged(QTableWidgetItem *current, QTableWidgetItem *previous);

private:
    Document *m_doc;
    bool m_rebuilding = false;
    ViewMode m_viewMode = ViewMode::Tree;

    QStackedWidget *m_stack = nullptr;
    QTreeWidget *m_meshTree = nullptr;
    QTreeWidget *m_rasterTree = nullptr;
    QTableWidget *m_meshTable = nullptr;
    QTableWidget *m_rasterTable = nullptr;
};
