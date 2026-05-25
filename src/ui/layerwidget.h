#pragma once

#include <QTreeWidget>

class Document;

class LayerWidget : public QTreeWidget
{
    Q_OBJECT
public:
    explicit LayerWidget(Document *doc, QWidget *parent = nullptr);

signals:
    void filterActionRequested(const QString &filterKey);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void rebuild();
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

private slots:
    void onItemChanged(QTreeWidgetItem *item, int column);
    void onCurrentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);

private:
    Document *m_doc;
    bool m_rebuilding = false;
};
