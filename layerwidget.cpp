#include "layerwidget.h"
#include "document.h"

LayerWidget::LayerWidget(Document *doc, QWidget *parent)
    : QTreeWidget(parent), m_doc(doc)
{
    setColumnCount(2);
    setHeaderHidden(true);

    connect(m_doc, &Document::meshAdded, this, &LayerWidget::rebuild);
    connect(m_doc, &Document::meshRemoved, this, &LayerWidget::rebuild);

    rebuild();
}

void LayerWidget::rebuild()
{
    clear();
    for (int i = 0; i < m_doc->meshCount(); ++i) {
        const auto &entry = m_doc->mesh(i);
        auto *item = new QTreeWidgetItem(this, {entry.name});
        item->addChild(new QTreeWidgetItem({tr("Vertices"), QString::number(entry.mesh.VN())}));
        item->addChild(new QTreeWidgetItem({tr("Faces"), QString::number(entry.mesh.FN())}));
        item->setExpanded(true);
    }
    resizeColumnToContents(0);
}
