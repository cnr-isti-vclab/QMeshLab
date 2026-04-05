#include "layerwidget.h"
#include "document.h"
#include <QSignalBlocker>

LayerWidget::LayerWidget(Document *doc, QWidget *parent)
    : QTreeWidget(parent), m_doc(doc)
{
    setColumnCount(2);
    setHeaderHidden(true);
    setSelectionMode(QAbstractItemView::SingleSelection);

    connect(m_doc, &Document::meshAdded, this, &LayerWidget::rebuild);
    connect(m_doc, &Document::meshRemoved, this, &LayerWidget::rebuild);
    connect(m_doc, &Document::meshVisibilityChanged, this, &LayerWidget::rebuild);
    connect(m_doc, &Document::currentMeshChanged, this, &LayerWidget::rebuild);
    connect(this, &QTreeWidget::itemChanged, this, &LayerWidget::onItemChanged);
    connect(this, &QTreeWidget::currentItemChanged, this, &LayerWidget::onCurrentItemChanged);

    rebuild();
}

void LayerWidget::rebuild()
{
    m_rebuilding = true;
    QSignalBlocker blocker(this);
    clear();
    for (int i = 0; i < m_doc->meshCount(); ++i) {
        const auto &entry = m_doc->mesh(i);
        auto *item = new QTreeWidgetItem(this, {entry.name, QString()});
        item->setData(0, Qt::UserRole, i);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        item->setCheckState(0, entry.visible ? Qt::Checked : Qt::Unchecked);

        auto *vItem = new QTreeWidgetItem({tr("Vertices"), QString::number(entry.mesh.VN())});
        vItem->setFlags(vItem->flags() & ~Qt::ItemIsSelectable);
        item->addChild(vItem);
        auto *fItem = new QTreeWidgetItem({tr("Faces"), QString::number(entry.mesh.FN())});
        fItem->setFlags(fItem->flags() & ~Qt::ItemIsSelectable);
        item->addChild(fItem);

        item->setExpanded(true);

        if (i == m_doc->currentMeshIndex())
            setCurrentItem(item);
    }
    updateCurrentItemVisuals();
    resizeColumnToContents(0);
    resizeColumnToContents(1);
    m_rebuilding = false;
}

int LayerWidget::meshIndexForItem(QTreeWidgetItem *item) const
{
    if (!item)
        return -1;

    QTreeWidgetItem *top = item;
    while (top->parent())
        top = top->parent();

    bool ok = false;
    const int idx = top->data(0, Qt::UserRole).toInt(&ok);
    return ok ? idx : -1;
}

void LayerWidget::updateCurrentItemVisuals()
{
    const int currentIdx = m_doc->currentMeshIndex();
    for (int i = 0; i < topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = topLevelItem(i);
        const bool isCurrent = (item->data(0, Qt::UserRole).toInt() == currentIdx);
        QFont f0 = item->font(0);
        QFont f1 = item->font(1);
        f0.setBold(isCurrent);
        f1.setBold(isCurrent);
        item->setFont(0, f0);
        item->setFont(1, f1);
    }
}

void LayerWidget::onItemChanged(QTreeWidgetItem *item, int column)
{
    if (m_rebuilding || !item || column != 0)
        return;
    if (item->parent())
        return;

    const int idx = meshIndexForItem(item);
    if (idx < 0)
        return;

    const bool visible = (item->checkState(0) == Qt::Checked);
    m_doc->setMeshVisible(idx, visible);
}

void LayerWidget::onCurrentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *)
{
    if (m_rebuilding)
        return;
    const int idx = meshIndexForItem(current);
    if (idx >= 0)
        m_doc->setCurrentMeshIndex(idx);

    updateCurrentItemVisuals();
}
