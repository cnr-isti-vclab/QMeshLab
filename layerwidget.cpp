#include "layerwidget.h"
#include "document.h"
#include <wrap/io_trimesh/io_mask.h>
#include <QFileInfo>
#include <QImageReader>
#include <QSignalBlocker>
#include <algorithm>

namespace {
constexpr int kFirstColumnMaxWidth = 110;

QString meshDataSummary(const Document::MeshEntry &entry)
{
    using Mask = vcg::tri::io::Mask;

    QStringList tokens;
    const int mask = entry.ioMask;

    if ((mask & Mask::IOM_VERTCOLOR) != 0)
        tokens << QObject::tr("VColor");
    if ((mask & Mask::IOM_FACECOLOR) != 0)
        tokens << QObject::tr("FColor");
    if ((mask & Mask::IOM_VERTNORMAL) != 0)
        tokens << QObject::tr("VNormal");
    if ((mask & Mask::IOM_FACENORMAL) != 0)
        tokens << QObject::tr("FNormal");
    if ((mask & Mask::IOM_WEDGNORMAL) != 0)
        tokens << QObject::tr("WNormal");
    if ((mask & Mask::IOM_VERTTEXCOORD) != 0)
        tokens << QObject::tr("VTex");
    if ((mask & Mask::IOM_WEDGTEXCOORD) != 0)
        tokens << QObject::tr("WTex");
    if ((mask & Mask::IOM_WEDGTEXMULTI) != 0)
        tokens << QObject::tr("MultiTex");
    if ((mask & Mask::IOM_VERTQUALITY) != 0)
        tokens << QObject::tr("VQuality");
    if ((mask & Mask::IOM_FACEQUALITY) != 0)
        tokens << QObject::tr("FQuality");

    if (!entry.textureFilePaths.isEmpty()) {
        int foundCount = 0;
        for (const QString &path : entry.textureFilePaths) {
            if (QFileInfo::exists(path))
                ++foundCount;
        }
        tokens << QObject::tr("Textures %1/%2").arg(foundCount).arg(entry.textureFilePaths.size());
    }

    if (tokens.isEmpty())
        return QObject::tr("none");
    return tokens.join(QStringLiteral(", "));
}

QString meshDataTooltip(const Document::MeshEntry &entry)
{
    QStringList lines;
    lines << QObject::tr("Data: %1").arg(meshDataSummary(entry));
    if (!entry.textureFileNames.isEmpty()) {
        lines << QObject::tr("Texture slots:");
        for (int i = 0; i < entry.textureFileNames.size(); ++i) {
            const QString name = entry.textureFileNames.at(i);
            const QString path = (i < entry.textureFilePaths.size()) ? entry.textureFilePaths.at(i) : QString();
            const bool exists = !path.isEmpty() && QFileInfo::exists(path);
            lines << QObject::tr("  [%1] %2 (%3)")
                        .arg(i)
                        .arg(name)
                        .arg(exists ? QObject::tr("found") : QObject::tr("missing"));
        }
    }
    return lines.join(QLatin1Char('\n'));
}

QString textureDisplayName(const Document::MeshEntry &entry, int index)
{
    if (index >= 0 && index < entry.textureFileNames.size() && !entry.textureFileNames.at(index).isEmpty())
        return entry.textureFileNames.at(index);

    if (index >= 0 && index < entry.textureFilePaths.size()) {
        const QString fileName = QFileInfo(entry.textureFilePaths.at(index)).fileName();
        if (!fileName.isEmpty())
            return fileName;
    }
    return QObject::tr("texture_%1").arg(index);
}

QString textureDisplaySize(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path))
        return QObject::tr("missing");

    QImageReader reader(path);
    const QSize sz = reader.size();
    if (!sz.isValid())
        return QObject::tr("unknown");

    return QStringLiteral("%1x%2").arg(sz.width()).arg(sz.height());
}
}

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
        item->setFirstColumnSpanned(true);

        auto *vItem = new QTreeWidgetItem({tr("Vertices"), QString::number(entry.mesh.VN())});
        vItem->setFlags(vItem->flags() & ~Qt::ItemIsSelectable);
        item->addChild(vItem);
        auto *fItem = new QTreeWidgetItem({tr("Faces"), QString::number(entry.mesh.FN())});
        fItem->setFlags(fItem->flags() & ~Qt::ItemIsSelectable);
        item->addChild(fItem);
        auto *dItem = new QTreeWidgetItem({tr("Data"), meshDataSummary(entry)});
        dItem->setFlags(dItem->flags() & ~Qt::ItemIsSelectable);
        item->addChild(dItem);

        for (int texIdx = 0; texIdx < entry.textureFilePaths.size(); ++texIdx) {
            const QString texPath = entry.textureFilePaths.at(texIdx);
            auto *tItem = new QTreeWidgetItem(
                {tr("Texture %1").arg(texIdx), QStringLiteral("%1 (%2)")
                    .arg(textureDisplayName(entry, texIdx))
                    .arg(textureDisplaySize(texPath))});
            tItem->setFlags(tItem->flags() & ~Qt::ItemIsSelectable);
            if (!texPath.isEmpty()) {
                tItem->setToolTip(0, texPath);
                tItem->setToolTip(1, texPath);
            }
            item->addChild(tItem);
        }

        const QString dataTip = meshDataTooltip(entry);
        item->setToolTip(0, dataTip);
        item->setToolTip(1, dataTip);
        dItem->setToolTip(0, dataTip);
        dItem->setToolTip(1, dataTip);

        item->setExpanded(true);

        if (i == m_doc->currentMeshIndex())
            setCurrentItem(item);
    }
    updateCurrentItemVisuals();
    resizeColumnToContents(0);
    setColumnWidth(0, std::min(columnWidth(0), kFirstColumnMaxWidth));
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
