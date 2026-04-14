#include "layerwidget.h"
#include "document.h"
#include <wrap/io_trimesh/io_mask.h>
#include <QFileInfo>
#include <QFontMetrics>
#include <QImageReader>
#include <QHeaderView>
#include <QLocale>
#include <QMetaObject>
#include <QSignalBlocker>
#include <algorithm>

namespace {
constexpr int kFirstColumnMinWidth = 56;
constexpr int kFirstColumnPadding = 10;
constexpr int kFirstColumnTreePadding = 28;

QString meshDataSummary(const Document::MeshEntry &entry)
{
    using Mask = vcg::tri::io::Mask;

    QStringList tokens;
    const int mask = entry.ioMask;

    if ((mask & Mask::IOM_VERTCOLOR) != 0)
        tokens << QObject::tr("VC");
    if ((mask & Mask::IOM_FACECOLOR) != 0)
        tokens << QObject::tr("FC");
    if ((mask & Mask::IOM_VERTNORMAL) != 0)
        tokens << QObject::tr("VN");
    if ((mask & Mask::IOM_FACENORMAL) != 0)
        tokens << QObject::tr("FN");
    if ((mask & Mask::IOM_WEDGNORMAL) != 0)
        tokens << QObject::tr("WN");
    if ((mask & Mask::IOM_VERTTEXCOORD) != 0)
        tokens << QObject::tr("VT");
    if ((mask & Mask::IOM_WEDGTEXCOORD) != 0)
        tokens << QObject::tr("WT");
    if ((mask & Mask::IOM_WEDGTEXMULTI) != 0)
        tokens << QObject::tr("MT");
    if ((mask & Mask::IOM_VERTQUALITY) != 0)
        tokens << QObject::tr("VQ");
    if ((mask & Mask::IOM_FACEQUALITY) != 0)
        tokens << QObject::tr("FQ");
    if ((mask & Mask::IOM_EDGEINDEX) != 0 || entry.mesh.EN() > 0)
        tokens << QObject::tr("EI");

    if (!entry.textureFilePaths.isEmpty()) {
        int foundCount = 0;
        for (const QString &path : entry.textureFilePaths) {
            if (QFileInfo::exists(path))
                ++foundCount;
        }
        tokens << QObject::tr("TX %1/%2").arg(foundCount).arg(entry.textureFilePaths.size());
    }

    if (tokens.isEmpty())
        return QObject::tr("none");
    return tokens.join(QLatin1Char(' '));
}

QString meshDataTooltip(const Document::MeshEntry &entry)
{
    QStringList lines;
    lines << QObject::tr("Data: %1").arg(meshDataSummary(entry));
    lines << QObject::tr("Counts: V=%1 F=%2 E=%3")
                 .arg(entry.mesh.VN())
                 .arg(entry.mesh.FN())
                 .arg(entry.mesh.EN());
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
    if (index >= 0 && index < entry.textureFileNames.size() && !entry.textureFileNames.at(index).isEmpty()) {
        const QString n = QFileInfo(entry.textureFileNames.at(index)).fileName();
        if (!n.isEmpty())
            return n;
        return entry.textureFileNames.at(index);
    }

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
    header()->setSectionResizeMode(0, QHeaderView::Fixed);
    header()->setSectionResizeMode(1, QHeaderView::Stretch);
    setColumnWidth(0, kFirstColumnMinWidth);

    connect(m_doc, &Document::meshAdded, this, &LayerWidget::rebuild);
    connect(m_doc, &Document::meshRemoved, this, &LayerWidget::rebuild);
    // Defer rebuild to avoid mutating tree items re-entrantly while an itemChanged
    // signal is still being processed.
    connect(m_doc, &Document::meshVisibilityChanged, this, [this](int, bool) {
        QMetaObject::invokeMethod(this, [this]() { rebuild(); }, Qt::QueuedConnection);
    });
    connect(m_doc, &Document::currentMeshChanged, this, [this](int) {
        QMetaObject::invokeMethod(this, [this]() { rebuild(); }, Qt::QueuedConnection);
    });
    connect(this, &QTreeWidget::itemChanged, this, &LayerWidget::onItemChanged);
    connect(this, &QTreeWidget::currentItemChanged, this, &LayerWidget::onCurrentItemChanged);

    rebuild();
}

void LayerWidget::rebuild()
{
    m_rebuilding = true;
    QSignalBlocker blocker(this);
    const QLocale locale = QLocale::system();
    const QFontMetrics fm(font());
    int maxCountTextWidth = 0;
    clear();
    for (int i = 0; i < m_doc->meshCount(); ++i) {
        const auto &entry = m_doc->mesh(i);
        auto *item = new QTreeWidgetItem(this, {entry.name, QString()});
        item->setData(0, Qt::UserRole, i);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        item->setCheckState(0, entry.visible ? Qt::Checked : Qt::Unchecked);
        item->setFirstColumnSpanned(true);

        const QString vertCountText = locale.toString(static_cast<qlonglong>(entry.mesh.VN()));
        const QString edgeCountText = locale.toString(static_cast<qlonglong>(entry.mesh.EN()));
        const QString faceCountText = locale.toString(static_cast<qlonglong>(entry.mesh.FN()));
        maxCountTextWidth = std::max(maxCountTextWidth, fm.horizontalAdvance(vertCountText));
        maxCountTextWidth = std::max(maxCountTextWidth, fm.horizontalAdvance(edgeCountText));
        maxCountTextWidth = std::max(maxCountTextWidth, fm.horizontalAdvance(faceCountText));

        auto *vItem = new QTreeWidgetItem({vertCountText, tr("Vert")});
        vItem->setFlags(vItem->flags() & ~Qt::ItemIsSelectable);
        vItem->setTextAlignment(0, Qt::AlignRight | Qt::AlignVCenter);
        item->addChild(vItem);
        auto *eItem = new QTreeWidgetItem({edgeCountText, tr("Edge")});
        eItem->setFlags(eItem->flags() & ~Qt::ItemIsSelectable);
        eItem->setTextAlignment(0, Qt::AlignRight | Qt::AlignVCenter);
        item->addChild(eItem);
        auto *fItem = new QTreeWidgetItem({faceCountText, tr("Face")});
        fItem->setFlags(fItem->flags() & ~Qt::ItemIsSelectable);
        fItem->setTextAlignment(0, Qt::AlignRight | Qt::AlignVCenter);
        item->addChild(fItem);
        auto *dItem = new QTreeWidgetItem({tr("Data"), meshDataSummary(entry)});
        dItem->setFlags(dItem->flags() & ~Qt::ItemIsSelectable);
        item->addChild(dItem);

        for (int texIdx = 0; texIdx < entry.textureFilePaths.size(); ++texIdx) {
            const QString texPath = entry.textureFilePaths.at(texIdx);
            auto *tItem = new QTreeWidgetItem(
                {tr("Tex %1").arg(texIdx), QStringLiteral("%1 (%2)")
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
    const int requiredWidth =
        maxCountTextWidth + kFirstColumnPadding + indentation() + kFirstColumnTreePadding;
    setColumnWidth(0, std::max(kFirstColumnMinWidth, requiredWidth));
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
