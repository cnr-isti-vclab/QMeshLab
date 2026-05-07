#pragma once

#include <QTreeWidget>

class Document;

class LayerWidget : public QTreeWidget
{
    Q_OBJECT
public:
    explicit LayerWidget(Document *doc, QWidget *parent = nullptr);

private:
    void rebuild();
    int meshIndexForItem(QTreeWidgetItem *item) const;
    void updateCurrentItemVisuals();

private slots:
    void onItemChanged(QTreeWidgetItem *item, int column);
    void onCurrentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);

private:
    Document *m_doc;
    bool m_rebuilding = false;
};
