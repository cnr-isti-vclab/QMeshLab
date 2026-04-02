#include "meshtreewidget.h"
#include "document.h"

MeshTreeWidget::MeshTreeWidget(Document *doc, QWidget *parent)
    : QTreeWidget(parent), m_doc(doc)
{
    setHeaderLabels({tr("Property"), tr("Value")});
    setColumnCount(2);

    connect(m_doc, &Document::meshAdded, this, &MeshTreeWidget::rebuild);
    connect(m_doc, &Document::meshRemoved, this, &MeshTreeWidget::rebuild);

    rebuild();
}

void MeshTreeWidget::rebuild()
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
