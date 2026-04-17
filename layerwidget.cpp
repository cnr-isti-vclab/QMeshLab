#include "layerwidget.h"
#include "document.h"
#include <wrap/io_trimesh/io_mask.h>
#include <vcg/complex/allocate.h>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHash>
#include <QHBoxLayout>
#include <QImageReader>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMetaObject>
#include <QPainter>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kFirstColumnMinWidth = 56;
constexpr int kFirstColumnPadding = 10;
constexpr int kFirstColumnTreePadding = 28;
constexpr int kRoleMeshIndex = Qt::UserRole;
constexpr int kRoleMeshId = Qt::UserRole + 1;

struct MeshCustomAttributeInfo {
    QStringList vertexScalars;
    QStringList vertexColors;
    QStringList vertexPoints;
    QStringList faceScalars;
    QStringList faceColors;
    QStringList facePoints;

    int vertexCount() const
    {
        return vertexScalars.size() + vertexColors.size() + vertexPoints.size();
    }
    int faceCount() const
    {
        return faceScalars.size() + faceColors.size() + facePoints.size();
    }
};

template<typename T>
QStringList collectPerVertexAttributeNames(VCGMesh &mesh)
{
    std::vector<std::string> names;
    vcg::tri::Allocator<VCGMesh>::GetAllPerVertexAttribute<T>(mesh, names);
    QStringList out;
    out.reserve(int(names.size()));
    for (const std::string &name : names) {
        if (name.empty())
            continue;
        out.push_back(QString::fromStdString(name));
    }
    out.removeDuplicates();
    out.sort(Qt::CaseInsensitive);
    return out;
}

template<typename T>
QStringList collectPerFaceAttributeNames(VCGMesh &mesh)
{
    std::vector<std::string> names;
    vcg::tri::Allocator<VCGMesh>::GetAllPerFaceAttribute<T>(mesh, names);
    QStringList out;
    out.reserve(int(names.size()));
    for (const std::string &name : names) {
        if (name.empty())
            continue;
        out.push_back(QString::fromStdString(name));
    }
    out.removeDuplicates();
    out.sort(Qt::CaseInsensitive);
    return out;
}

MeshCustomAttributeInfo collectCustomAttributes(const VCGMesh &mesh)
{
    VCGMesh &mutableMesh = const_cast<VCGMesh &>(mesh);
    MeshCustomAttributeInfo attrs;
    attrs.vertexScalars = collectPerVertexAttributeNames<float>(mutableMesh);
    attrs.vertexColors = collectPerVertexAttributeNames<vcg::Color4b>(mutableMesh);
    attrs.vertexPoints = collectPerVertexAttributeNames<vcg::Point3f>(mutableMesh);
    attrs.faceScalars = collectPerFaceAttributeNames<float>(mutableMesh);
    attrs.faceColors = collectPerFaceAttributeNames<vcg::Color4b>(mutableMesh);
    attrs.facePoints = collectPerFaceAttributeNames<vcg::Point3f>(mutableMesh);
    return attrs;
}

QString ownerAttributeSummary(
    const QStringList &scalars,
    const QStringList &colors,
    const QStringList &points)
{
    QStringList parts;
    if (!scalars.isEmpty())
        parts << QObject::tr("S[%1]").arg(scalars.join(QStringLiteral(", ")));
    if (!colors.isEmpty())
        parts << QObject::tr("C[%1]").arg(colors.join(QStringLiteral(", ")));
    if (!points.isEmpty())
        parts << QObject::tr("P[%1]").arg(points.join(QStringLiteral(", ")));
    return parts.join(QStringLiteral("  "));
}

QString ownerAttributeTooltip(
    const QString &ownerLabel,
    const QStringList &scalars,
    const QStringList &colors,
    const QStringList &points)
{
    QStringList lines;
    lines << QObject::tr("%1 custom attributes:").arg(ownerLabel);
    if (!scalars.isEmpty())
        lines << QObject::tr("  Scalar: %1").arg(scalars.join(QStringLiteral(", ")));
    if (!colors.isEmpty())
        lines << QObject::tr("  Color: %1").arg(colors.join(QStringLiteral(", ")));
    if (!points.isEmpty())
        lines << QObject::tr("  Point3: %1").arg(points.join(QStringLiteral(", ")));
    return lines.join(QLatin1Char('\n'));
}

int selectedVertexCount(const VCGMesh &mesh)
{
    int count = 0;
    for (const auto &v : mesh.vert) {
        if (!v.IsD() && v.IsS())
            ++count;
    }
    return count;
}

int selectedEdgeCount(const VCGMesh &mesh)
{
    int count = 0;
    for (const auto &e : mesh.edge) {
        if (!e.IsD() && e.IsS())
            ++count;
    }
    return count;
}

int selectedFaceCount(const VCGMesh &mesh)
{
    int count = 0;
    for (const auto &f : mesh.face) {
        if (!f.IsD() && f.IsS())
            ++count;
    }
    return count;
}

bool isIdentityTransform(const QMatrix4x4 &m, float eps = 1e-6f)
{
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            const float expected = (r == c) ? 1.0f : 0.0f;
            if (std::abs(m(r, c) - expected) > eps)
                return false;
        }
    }
    return true;
}

QString meshTransformSummary(const QMatrix4x4 &m)
{
    const QVector3D t(m(0, 3), m(1, 3), m(2, 3));
    const QVector3D cx(m(0, 0), m(1, 0), m(2, 0));
    const QVector3D cy(m(0, 1), m(1, 1), m(2, 1));
    const QVector3D cz(m(0, 2), m(1, 2), m(2, 2));
    const float sx = cx.length();
    const float sy = cy.length();
    const float sz = cz.length();
    return QObject::tr("T(%1, %2, %3)  S(%4, %5, %6)")
        .arg(t.x(), 0, 'g', 4)
        .arg(t.y(), 0, 'g', 4)
        .arg(t.z(), 0, 'g', 4)
        .arg(sx, 0, 'g', 4)
        .arg(sy, 0, 'g', 4)
        .arg(sz, 0, 'g', 4);
}

QString meshTransformTooltip(const QMatrix4x4 &m)
{
    QStringList lines;
    lines << QObject::tr("Mesh transform (4x4):");
    for (int r = 0; r < 4; ++r) {
        lines << QStringLiteral("[ %1  %2  %3  %4 ]")
                     .arg(m(r, 0), 0, 'g', 8)
                     .arg(m(r, 1), 0, 'g', 8)
                     .arg(m(r, 2), 0, 'g', 8)
                     .arg(m(r, 3), 0, 'g', 8);
    }
    return lines.join(QLatin1Char('\n'));
}

QString meshDataSummary(const Document::MeshEntry &entry)
{
    using Mask = vcg::tri::io::Mask;

    QStringList tokens;
    const int mask = entry.ioMask;
    const MeshCustomAttributeInfo attrs = collectCustomAttributes(entry.mesh);

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
    if (attrs.vertexCount() > 0)
        tokens << QObject::tr("VA %1").arg(attrs.vertexCount());
    if (attrs.faceCount() > 0)
        tokens << QObject::tr("FA %1").arg(attrs.faceCount());

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
    const MeshCustomAttributeInfo attrs = collectCustomAttributes(entry.mesh);
    QStringList lines;
    lines << QObject::tr("Data: %1").arg(meshDataSummary(entry));
    lines << QObject::tr("Counts: V=%1 F=%2 E=%3")
                 .arg(entry.mesh.VN())
                 .arg(entry.mesh.FN())
                 .arg(entry.mesh.EN());
    lines << QObject::tr("Selected: V=%1 F=%2 E=%3")
                 .arg(selectedVertexCount(entry.mesh))
                 .arg(selectedFaceCount(entry.mesh))
                 .arg(selectedEdgeCount(entry.mesh));
    if (!isIdentityTransform(entry.renderTransform))
        lines << QObject::tr("Transform: %1").arg(meshTransformSummary(entry.renderTransform));
    if (attrs.vertexCount() > 0) {
        lines << QObject::tr("Vertex custom attributes: %1")
                     .arg(ownerAttributeSummary(attrs.vertexScalars, attrs.vertexColors, attrs.vertexPoints));
    }
    if (attrs.faceCount() > 0) {
        lines << QObject::tr("Face custom attributes: %1")
                     .arg(ownerAttributeSummary(attrs.faceScalars, attrs.faceColors, attrs.facePoints));
    }
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

QPixmap textureThumbnail(const QString &path, int side)
{
    side = std::max(8, side);
    const QString cacheKey = QStringLiteral("%1|%2").arg(path).arg(side);
    static QHash<QString, QPixmap> cache;
    const auto it = cache.constFind(cacheKey);
    if (it != cache.constEnd())
        return it.value();

    QPixmap out(side, side);
    out.fill(QColor(30, 30, 30));
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);

    bool ok = false;
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QSize native = reader.size();
        if (native.isValid())
            reader.setScaledSize(native.scaled(side, side, Qt::KeepAspectRatio));
        const QImage img = reader.read();
        if (!img.isNull()) {
            const QPixmap tex = QPixmap::fromImage(
                img.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            const int x = (side - tex.width()) / 2;
            const int y = (side - tex.height()) / 2;
            p.drawPixmap(x, y, tex);
            ok = true;
        }
    }

    if (!ok) {
        p.fillRect(out.rect(), QColor(45, 45, 48));
        p.setPen(QColor(110, 110, 115));
        p.drawLine(2, 2, side - 3, side - 3);
        p.drawLine(2, side - 3, side - 3, 2);
    }

    p.setPen(QColor(95, 95, 100));
    p.drawRect(out.rect().adjusted(0, 0, -1, -1));

    cache.insert(cacheKey, out);
    return out;
}

QWidget *textureInfoWidget(
    QTreeWidget *owner,
    const Document::MeshEntry &entry,
    int index,
    const QFontMetrics &fm)
{
    const QString path =
        (index >= 0 && index < entry.textureFilePaths.size()) ? entry.textureFilePaths.at(index) : QString();
    const QString name = textureDisplayName(entry, index);
    const QString size = textureDisplaySize(path);
    const int lineH = std::max(8, fm.lineSpacing());
    const int thumbSide = std::max(14, lineH * 2);

    auto *container = new QWidget(owner);
    auto *h = new QHBoxLayout(container);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);

    auto *thumb = new QLabel(container);
    thumb->setFixedSize(thumbSide, thumbSide);
    thumb->setPixmap(textureThumbnail(path, thumbSide));

    auto *textCol = new QWidget(container);
    auto *v = new QVBoxLayout(textCol);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto *nameLabel = new QLabel(name, textCol);
    nameLabel->setTextFormat(Qt::PlainText);
    nameLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    nameLabel->setFixedHeight(lineH);

    auto *sizeLabel = new QLabel(size, textCol);
    sizeLabel->setTextFormat(Qt::PlainText);
    sizeLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    sizeLabel->setFixedHeight(lineH);
    sizeLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));

    v->addWidget(nameLabel);
    v->addWidget(sizeLabel);

    h->addWidget(thumb, 0, Qt::AlignVCenter);
    h->addWidget(textCol, 1);

    if (!path.isEmpty()) {
        container->setToolTip(path);
        thumb->setToolTip(path);
        nameLabel->setToolTip(path);
        sizeLabel->setToolTip(path);
    }

    return container;
}
}

LayerWidget::LayerWidget(Document *doc, QWidget *parent)
    : QTreeWidget(parent), m_doc(doc)
{
    setColumnCount(3);
    setHeaderHidden(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    header()->setSectionResizeMode(0, QHeaderView::Fixed);
    header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(2, QHeaderView::Stretch);
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
    connect(m_doc, &Document::meshDataChanged, this, [this](int) {
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

    QHash<qulonglong, bool> expandedStateByMeshId;
    expandedStateByMeshId.reserve(topLevelItemCount());
    for (int row = 0; row < topLevelItemCount(); ++row) {
        QTreeWidgetItem *existing = topLevelItem(row);
        if (!existing)
            continue;
        bool ok = false;
        const qulonglong meshId = existing->data(0, kRoleMeshId).toULongLong(&ok);
        if (!ok)
            continue;
        expandedStateByMeshId.insert(meshId, existing->isExpanded());
    }

    clear();
    for (int i = 0; i < m_doc->meshCount(); ++i) {
        const auto &entry = m_doc->mesh(i);
        const MeshCustomAttributeInfo attrs = collectCustomAttributes(entry.mesh);
        auto *item = new QTreeWidgetItem(this, {entry.name, QString()});
        item->setData(0, kRoleMeshIndex, i);
        item->setData(0, kRoleMeshId, qulonglong(entry.meshId));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        item->setCheckState(0, entry.visible ? Qt::Checked : Qt::Unchecked);
        item->setFirstColumnSpanned(true);

        const QString vertCountText = locale.toString(static_cast<qlonglong>(entry.mesh.VN()));
        const QString edgeCountText = locale.toString(static_cast<qlonglong>(entry.mesh.EN()));
        const QString faceCountText = locale.toString(static_cast<qlonglong>(entry.mesh.FN()));
        const int selectedVertCount = selectedVertexCount(entry.mesh);
        const int selectedEdgeCountValue = selectedEdgeCount(entry.mesh);
        const int selectedFaceCountValue = selectedFaceCount(entry.mesh);
        const QString selectedVertText =
            selectedVertCount > 0 ? locale.toString(static_cast<qlonglong>(selectedVertCount)) : QString();
        const QString selectedEdgeText =
            selectedEdgeCountValue > 0
                ? locale.toString(static_cast<qlonglong>(selectedEdgeCountValue))
                : QString();
        const QString selectedFaceText =
            selectedFaceCountValue > 0
                ? locale.toString(static_cast<qlonglong>(selectedFaceCountValue))
                : QString();
        maxCountTextWidth = std::max(maxCountTextWidth, fm.horizontalAdvance(vertCountText));
        maxCountTextWidth = std::max(maxCountTextWidth, fm.horizontalAdvance(edgeCountText));
        maxCountTextWidth = std::max(maxCountTextWidth, fm.horizontalAdvance(faceCountText));

        auto *vItem = new QTreeWidgetItem({vertCountText, tr("Vert"), selectedVertText});
        vItem->setFlags(vItem->flags() & ~Qt::ItemIsSelectable);
        vItem->setTextAlignment(0, Qt::AlignRight | Qt::AlignVCenter);
        vItem->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        item->addChild(vItem);
        auto *eItem = new QTreeWidgetItem({edgeCountText, tr("Edge"), selectedEdgeText});
        eItem->setFlags(eItem->flags() & ~Qt::ItemIsSelectable);
        eItem->setTextAlignment(0, Qt::AlignRight | Qt::AlignVCenter);
        eItem->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        item->addChild(eItem);
        auto *fItem = new QTreeWidgetItem({faceCountText, tr("Face"), selectedFaceText});
        fItem->setFlags(fItem->flags() & ~Qt::ItemIsSelectable);
        fItem->setTextAlignment(0, Qt::AlignRight | Qt::AlignVCenter);
        fItem->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        item->addChild(fItem);
        auto *dItem = new QTreeWidgetItem({QString(), tr("Data"), meshDataSummary(entry)});
        dItem->setFlags(dItem->flags() & ~Qt::ItemIsSelectable);
        item->addChild(dItem);

        if (attrs.vertexCount() > 0) {
            auto *vAttrItem = new QTreeWidgetItem(
                {QString(), tr("VAttr"), ownerAttributeSummary(attrs.vertexScalars, attrs.vertexColors, attrs.vertexPoints)});
            vAttrItem->setFlags(vAttrItem->flags() & ~Qt::ItemIsSelectable);
            const QString tip = ownerAttributeTooltip(
                tr("Vertex"),
                attrs.vertexScalars,
                attrs.vertexColors,
                attrs.vertexPoints);
            vAttrItem->setToolTip(0, tip);
            vAttrItem->setToolTip(1, tip);
            vAttrItem->setToolTip(2, tip);
            item->addChild(vAttrItem);
        }

        if (attrs.faceCount() > 0) {
            auto *fAttrItem = new QTreeWidgetItem(
                {QString(), tr("FAttr"), ownerAttributeSummary(attrs.faceScalars, attrs.faceColors, attrs.facePoints)});
            fAttrItem->setFlags(fAttrItem->flags() & ~Qt::ItemIsSelectable);
            const QString tip = ownerAttributeTooltip(
                tr("Face"),
                attrs.faceScalars,
                attrs.faceColors,
                attrs.facePoints);
            fAttrItem->setToolTip(0, tip);
            fAttrItem->setToolTip(1, tip);
            fAttrItem->setToolTip(2, tip);
            item->addChild(fAttrItem);
        }

        if (!isIdentityTransform(entry.renderTransform)) {
            auto *xItem = new QTreeWidgetItem(
                {QString(), tr("Xf"), meshTransformSummary(entry.renderTransform)});
            xItem->setFlags(xItem->flags() & ~Qt::ItemIsSelectable);
            const QString xfTip = meshTransformTooltip(entry.renderTransform);
            xItem->setToolTip(0, xfTip);
            xItem->setToolTip(1, xfTip);
            xItem->setToolTip(2, xfTip);
            item->addChild(xItem);
        }

        for (int texIdx = 0; texIdx < entry.textureFilePaths.size(); ++texIdx) {
            const QString texPath = entry.textureFilePaths.at(texIdx);
            auto *tItem = new QTreeWidgetItem({QString(), tr("Tex %1").arg(texIdx), QString()});
            tItem->setFlags(tItem->flags() & ~Qt::ItemIsSelectable);
            if (!texPath.isEmpty()) {
                tItem->setToolTip(0, texPath);
                tItem->setToolTip(1, texPath);
                tItem->setToolTip(2, texPath);
            }
            item->addChild(tItem);
            setItemWidget(tItem, 2, textureInfoWidget(this, entry, texIdx, fm));
        }

        const QString dataTip = meshDataTooltip(entry);
        item->setToolTip(0, dataTip);
        item->setToolTip(1, dataTip);
        item->setToolTip(2, dataTip);
        dItem->setToolTip(0, dataTip);
        dItem->setToolTip(1, dataTip);
        dItem->setToolTip(2, dataTip);

        const auto it = expandedStateByMeshId.constFind(qulonglong(entry.meshId));
        item->setExpanded(it != expandedStateByMeshId.constEnd() ? it.value() : true);

        if (i == m_doc->currentMeshIndex())
            setCurrentItem(item);
    }
    updateCurrentItemVisuals();
    const int requiredWidth =
        maxCountTextWidth + kFirstColumnPadding + indentation() + kFirstColumnTreePadding;
    setColumnWidth(0, std::max(kFirstColumnMinWidth, requiredWidth));
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
    const int idx = top->data(0, kRoleMeshIndex).toInt(&ok);
    return ok ? idx : -1;
}

void LayerWidget::updateCurrentItemVisuals()
{
    const int currentIdx = m_doc->currentMeshIndex();
    for (int i = 0; i < topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = topLevelItem(i);
        const bool isCurrent = (item->data(0, kRoleMeshIndex).toInt() == currentIdx);
        QFont f0 = item->font(0);
        QFont f1 = item->font(1);
        QFont f2 = item->font(2);
        f0.setBold(isCurrent);
        f1.setBold(isCurrent);
        f2.setBold(isCurrent);
        item->setFont(0, f0);
        item->setFont(1, f1);
        item->setFont(2, f2);
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
