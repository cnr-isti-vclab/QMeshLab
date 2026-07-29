#include "filterpluginsinfodialog.h"

#include "filtercategories.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QScreen>
#include <QSplitter>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace {

// Roles on tree items: what kind of node it is and what it points at.
constexpr int kRoleKind = Qt::UserRole;      // "filter" | "plugin" | "category"
constexpr int kRoleValue = Qt::UserRole + 1; // filter key / plugin id / category path

QString esc(const QString &s)
{
    return s.toHtmlEscaped();
}

QString row(const QString &label, const QString &value)
{
    if (value.trimmed().isEmpty())
        return {};
    return QStringLiteral("<tr><td style=\"padding-right:10px; color:palette(mid);"
                          " vertical-align:top; white-space:nowrap;\">%1</td>"
                          "<td style=\"vertical-align:top;\">%2</td></tr>")
        .arg(esc(label), value);
}

QString heading(const QString &title, const QString &sub = QString())
{
    QString h = QStringLiteral("<div style=\"font-size:13px; font-weight:bold;\">%1</div>")
                    .arg(esc(title));
    if (!sub.trimmed().isEmpty())
        h += QStringLiteral("<div style=\"color:palette(mid); margin-bottom:6px;\">%1</div>")
                 .arg(esc(sub));
    return h;
}

QString linkHtml(const QString &url, const QString &text)
{
    if (url.trimmed().isEmpty())
        return {};
    return QStringLiteral("<a href=\"%1\">%2</a>").arg(esc(url), esc(text));
}

// Full bibliographic citation: every field the descriptor carries, in the order a
// paper's bibliography would use, with DOI and web links live.
// MeshFilterReference::markdownCitation() is deliberately not reused here — it is
// markdown, and it drops volume, issue, pages and publisher.
QString citationHtml(const MeshFilterReference &r)
{
    QStringList names;
    for (const MeshFilterReferenceAuthor &a : r.authors) {
        if (a.given.isEmpty())
            names << a.family;
        else if (a.family.isEmpty())
            names << a.given;
        else
            names << a.given + QLatin1Char(' ') + a.family;
    }

    QString out;
    if (!names.isEmpty())
        out += esc(names.join(QStringLiteral(", "))) + QStringLiteral(". ");
    if (!r.title.trimmed().isEmpty())
        out += QStringLiteral("<b>%1</b>. ").arg(esc(r.title));
    if (!r.containerTitle.trimmed().isEmpty())
        out += QStringLiteral("<i>%1</i>").arg(esc(r.containerTitle));

    // Volume(issue), pages — journal style.
    QString locator;
    if (!r.volume.trimmed().isEmpty()) {
        locator = esc(r.volume);
        if (!r.issue.trimmed().isEmpty())
            locator += QStringLiteral("(%1)").arg(esc(r.issue));
    }
    if (!r.page.trimmed().isEmpty())
        locator += (locator.isEmpty() ? QString() : QStringLiteral(":")) + esc(r.page);
    if (!locator.isEmpty())
        out += QStringLiteral(", %1").arg(locator);
    if (!r.publisher.trimmed().isEmpty())
        out += QStringLiteral(". %1").arg(esc(r.publisher));
    if (r.year > 0)
        out += QStringLiteral(" (%1)").arg(r.year);
    if (!out.endsWith(QLatin1Char('.')))
        out += QLatin1Char('.');

    QStringList links;
    if (!r.doiUrl().isEmpty())
        links << linkHtml(r.doiUrl(), QStringLiteral("doi:%1").arg(r.doi));
    // webUrl() already suppresses a URL that merely repeats the DOI.
    if (!r.webUrl().isEmpty())
        links << linkHtml(r.webUrl(), QObject::tr("web"));
    if (!links.isEmpty())
        out += QStringLiteral(" %1").arg(links.join(QStringLiteral(" · ")));
    return out;
}

// Readable list of the declared input requirements.
QStringList requirementLabels(const MeshFilterMeshRequirements &r)
{
    QStringList out;
    if (r.requireVertices)             out << QObject::tr("vertices");
    if (r.requireEdges)                out << QObject::tr("edges");
    if (r.requireFaces)                out << QObject::tr("faces");
    if (r.requireVertexColor)          out << QObject::tr("vertex color");
    if (r.requireFaceColor)            out << QObject::tr("face color");
    if (r.requireTextureCoordinates)   out << QObject::tr("texture coordinates");
    if (r.requirePerVertexTexCoords)   out << QObject::tr("per-vertex texcoords");
    if (r.requirePerWedgeTexCoords)    out << QObject::tr("per-wedge texcoords");
    if (r.requireTextures)             out << QObject::tr("textures");
    if (r.requireVertexQuality)        out << QObject::tr("vertex scalar");
    if (r.requireFaceQuality)          out << QObject::tr("face scalar");
    return out;
}

QString filterParameterTypeLabel(MeshFilterParameterType type)
{
    switch (type) {
    case MeshFilterParameterType::Bool:
        return QObject::tr("Boolean");
    case MeshFilterParameterType::Int:
        return QObject::tr("Integer");
    case MeshFilterParameterType::Mesh:
        return QObject::tr("Mesh Layer");
    case MeshFilterParameterType::Double:
        return QObject::tr("Double");
    case MeshFilterParameterType::AbsPerc:
        return QObject::tr("Abs / %");
    case MeshFilterParameterType::String:
        return QObject::tr("String");
    case MeshFilterParameterType::FileOpen:
        return QObject::tr("File Open");
    case MeshFilterParameterType::FileSave:
        return QObject::tr("File Save");
    case MeshFilterParameterType::TextureRef:
        return QObject::tr("Texture Choice");
    case MeshFilterParameterType::TextureOutputRef:
        return QObject::tr("Texture Output");
    case MeshFilterParameterType::Enum:
        return QObject::tr("Enum");
    case MeshFilterParameterType::Color:
        return QObject::tr("Color");
    case MeshFilterParameterType::Point3f:
        return QObject::tr("3D Point");
    case MeshFilterParameterType::CameraState:
        return QObject::tr("Camera State JSON");
    case MeshFilterParameterType::RenderState:
        return QObject::tr("Render State JSON");
    }
    return QObject::tr("Unknown");
}

QString filterParameterValueText(const QVariant &value, MeshFilterParameterType type, int decimals)
{
    if (!value.isValid() || value.isNull())
        return QObject::tr("none");

    switch (type) {
    case MeshFilterParameterType::Bool:
        return value.toBool() ? QObject::tr("true") : QObject::tr("false");
    case MeshFilterParameterType::Int:
        return QString::number(value.toInt());
    case MeshFilterParameterType::Mesh:
        return QString::number(value.toInt());
    case MeshFilterParameterType::Double:
    case MeshFilterParameterType::AbsPerc:
        return QLocale().toString(value.toDouble(), 'f', std::max(0, decimals));
    case MeshFilterParameterType::String:
    case MeshFilterParameterType::FileOpen:
    case MeshFilterParameterType::FileSave:
    case MeshFilterParameterType::TextureRef:
    case MeshFilterParameterType::TextureOutputRef:
    case MeshFilterParameterType::Enum:
    case MeshFilterParameterType::Color:
    case MeshFilterParameterType::CameraState:
    case MeshFilterParameterType::RenderState:
        if (type == MeshFilterParameterType::TextureOutputRef && value.userType() == QMetaType::QVariantMap) {
            const QVariantMap map = value.toMap();
            const QString mode = map.value(QStringLiteral("mode")).toString().trimmed().toLower();
            if (mode == QStringLiteral("existing")) {
                const int slot = map.value(QStringLiteral("slot")).toInt();
                if (slot > 0)
                    return QObject::tr("overwrite texture %1").arg(slot);
            }
            const QString path = map.value(QStringLiteral("path")).toString().trimmed();
            if (!path.isEmpty())
                return QObject::tr("create new: %1").arg(path);
            return QObject::tr("create new");
        }
        return value.toString();
    case MeshFilterParameterType::Point3f:
        if (value.userType() == QMetaType::QVector3D) {
            const QVector3D v = value.value<QVector3D>();
            return QStringLiteral("%1, %2, %3").arg(v.x()).arg(v.y()).arg(v.z());
        }
        return value.toString();
    }
    return value.toString();
}

QString formatFilterParameterDetails(const MeshFilterDescriptor &descriptor)
{
    QString html = QStringLiteral(
        "<html><body style=\"margin:0; font-size:11px; line-height:1.3;\">");

    if (!descriptor.outputModifies.isEmpty()) {
        // Build a human-readable sentence from the two-letter codes.
        static const QHash<QString, QString> codeLabels = {
            { QStringLiteral("VG"), QObject::tr("vertex geometry") },
            { QStringLiteral("VN"), QObject::tr("vertex normals") },
            { QStringLiteral("VC"), QObject::tr("vertex color") },
            { QStringLiteral("VQ"), QObject::tr("vertex quality") },
            { QStringLiteral("VT"), QObject::tr("vertex texcoords") },
            { QStringLiteral("VA"), QObject::tr("vertex attributes") },
            { QStringLiteral("VS"), QObject::tr("vertex selection") },
            { QStringLiteral("FV"), QObject::tr("face-vertex connectivity") },
            { QStringLiteral("FN"), QObject::tr("face normals") },
            { QStringLiteral("FC"), QObject::tr("face color") },
            { QStringLiteral("FQ"), QObject::tr("face quality") },
            { QStringLiteral("FA"), QObject::tr("face attributes") },
            { QStringLiteral("FS"), QObject::tr("face selection") },
            { QStringLiteral("FP"), QObject::tr("face polygon bits") },
            { QStringLiteral("WT"), QObject::tr("wedge texcoords") },
            { QStringLiteral("TX"), QObject::tr("texture images") },
            { QStringLiteral("TM"), QObject::tr("transform matrix") },
        };
        QStringList labels;
        for (const QString &code : descriptor.outputModifies) {
            const QString label = codeLabels.value(code);
            labels.push_back(
                label.isEmpty()
                    ? code.toHtmlEscaped()
                    : QStringLiteral("%1 (%2)").arg(label.toHtmlEscaped(), code.toHtmlEscaped()));
        }
        html += QStringLiteral(
                    "<div style=\"margin-bottom:6px; padding:4px 6px;"
                    " background:#f0f4f8; border-left:3px solid #5a8fc8;\">")
                + QStringLiteral("<span style=\"font-size:11px; color:#335;\">")
                + QObject::tr("<b>Modifies:</b> %1").arg(labels.join(QStringLiteral(", ")))
                + QStringLiteral("</span></div>");
    }

    if (descriptor.parameters.empty()) {
        html += QObject::tr(
            "<p style=\"margin:0; color: palette(mid);\">This filter declares no parameters.</p>");
        html += QStringLiteral("</body></html>");
        return html;
    }
    for (size_t i = 0; i < descriptor.parameters.size(); ++i) {
        const MeshFilterParameterDescriptor &param = descriptor.parameters[i];
        const QString group = param.group.trimmed();
        const bool hasGroup = !group.isEmpty()
            && group.compare(QStringLiteral("main"), Qt::CaseInsensitive) != 0;
        QStringList summaryParts;
        summaryParts.push_back(
            QObject::tr("Type: %1").arg(filterParameterTypeLabel(param.type)).toHtmlEscaped());
        summaryParts.push_back(
            QObject::tr("Default: %1")
                .arg(filterParameterValueText(param.defaultValue, param.type, param.decimals))
                .toHtmlEscaped());

        if (hasGroup) {
            summaryParts.push_back(QObject::tr("Group: %1").arg(group).toHtmlEscaped());
        }

        if ((param.type == MeshFilterParameterType::Int
                || param.type == MeshFilterParameterType::Double
                || param.type == MeshFilterParameterType::AbsPerc)
            && param.minValue.isValid() && param.maxValue.isValid()) {
            summaryParts.push_back(QObject::tr("Range: %1-%2")
                                       .arg(filterParameterValueText(
                                           param.minValue, param.type, param.decimals))
                                       .arg(filterParameterValueText(
                                           param.maxValue, param.type, param.decimals))
                                       .toHtmlEscaped());
        }

        html += QStringLiteral("<div style=\"margin-bottom:5px;\">");
        html += QStringLiteral(
                    "<div style=\"font-size:12px;\"><b>%1</b> <span style=\"color:#666;\">(%2)</span></div>")
                    .arg(param.label.toHtmlEscaped(), param.id.toHtmlEscaped());
        html += QStringLiteral("<div style=\"margin-top:1px; color:#444;\">%1</div>")
                    .arg(summaryParts.join(QStringLiteral(" &nbsp;&middot;&nbsp; ")));

        if (param.type == MeshFilterParameterType::Enum && !param.enumOptions.empty()) {
            QStringList options;
            options.reserve(static_cast<int>(param.enumOptions.size()));
            for (const MeshFilterEnumOption &option : param.enumOptions) {
                options.push_back(option.label.trimmed().isEmpty() ? option.id : option.label);
            }
            html += QStringLiteral("<div style=\"margin-top:2px; color:#444;\">%1</div>")
                        .arg(QObject::tr("Options: %1").arg(options.join(QStringLiteral(", "))).toHtmlEscaped());
        }

        if (!param.helpMarkdown.trimmed().isEmpty()) {
            html += QStringLiteral(
                        "<div style=\"margin-top:2px; font-size:10px; color:#555; white-space:pre-wrap;\">%1</div>")
                        .arg(param.helpMarkdown.toHtmlEscaped());
        }

        html += QStringLiteral("</div>");
        if (i + 1 < descriptor.parameters.size())
            html += QStringLiteral("<div style=\"height:2px;\"></div>");
    }
    html += QStringLiteral("</body></html>");
    return html;
}

QString filterInputDomainLabel(MeshFilterInputDomain domain)
{
    switch (domain) {
    case MeshFilterInputDomain::None:
        return QObject::tr("None");
    case MeshFilterInputDomain::SingleMesh:
        return QObject::tr("Single Mesh");
    case MeshFilterInputDomain::WholeDocument:
        return QObject::tr("Whole Document");
    }
    return QObject::tr("Unknown");
}

QString filterOutputDomainLabel(MeshFilterOutputDomain domain)
{
    switch (domain) {
    case MeshFilterOutputDomain::Information:
        return QObject::tr("Information");
    case MeshFilterOutputDomain::ModifyCurrentMesh:
        return QObject::tr("Modify Current Mesh");
    case MeshFilterOutputDomain::NewMeshes:
        return QObject::tr("Create New Meshes");
    }
    return QObject::tr("Unknown");
}
} // namespace

FilterPluginsInfoDialog::FilterPluginsInfoDialog(std::vector<Document::FilterInfo> filters,
                                                 QWidget *parent)
    : QDialog(parent)
    , m_filters(std::move(filters))
{
    setWindowTitle(tr("Filter Plugins Info"));
    buildUi();
    rebuildTree();

    // Open at a generous but bounded size; the tree/detail splitter does the rest.
    if (QScreen *s = screen()) {
        const QSize avail = s->availableGeometry().size();
        resize(std::min(avail.width() * 3 / 4, 1100), std::min(avail.height() * 3 / 4, 720));
    }
}

void FilterPluginsInfoDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    auto *controls = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search name, Python name, category, plugin…"));
    m_search->setClearButtonEnabled(true);
    controls->addWidget(m_search, 1);

    controls->addWidget(new QLabel(tr("Group by:"), this));
    m_groupBy = new QComboBox(this);
    m_groupBy->addItem(tr("Category"), QStringLiteral("category"));
    m_groupBy->addItem(tr("Plugin"), QStringLiteral("plugin"));
    controls->addWidget(m_groupBy);

    m_onlyUnavailable = new QCheckBox(tr("Only unavailable"), this);
    controls->addWidget(m_onlyUnavailable);
    root->addLayout(controls);

    auto *splitter = new QSplitter(Qt::Horizontal, this);

    m_tree = new QTreeWidget(splitter);
    m_tree->setHeaderLabels({ tr("Filter / Group"), tr("N") });
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    // Fixed, sized for three digits. ResizeToContents made the column track the
    // widest visible count, so it jumped about while typing in the search box.
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->resizeSection(
        1, m_tree->fontMetrics().horizontalAdvance(QStringLiteral("888")) + 16);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    splitter->addWidget(m_tree);

    m_detail = new QTextBrowser(splitter);
    // Route anchors explicitly rather than letting QTextBrowser navigate: it would
    // try to load the URL as a document and blank the panel.
    m_detail->setOpenLinks(false);
    m_detail->setOpenExternalLinks(false);
    connect(m_detail, &QTextBrowser::anchorClicked, this, [](const QUrl &url) {
        if (!url.isEmpty())
            QDesktopServices::openUrl(url);
    });
    splitter->addWidget(m_detail);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 4);
    root->addWidget(splitter, 1);

    m_summary = new QLabel(this);
    m_summary->setStyleSheet(QStringLiteral("color: palette(mid);"));
    root->addWidget(m_summary);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, Qt::Horizontal, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    root->addWidget(buttons);

    connect(m_search, &QLineEdit::textChanged, this, [this] { rebuildTree(); });
    connect(m_groupBy, &QComboBox::currentIndexChanged, this, [this] { rebuildTree(); });
    connect(m_onlyUnavailable, &QCheckBox::toggled, this, [this] { rebuildTree(); });
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this] { updateDetail(); });
}

bool FilterPluginsInfoDialog::matches(const Document::FilterInfo &info) const
{
    if (m_onlyUnavailable->isChecked() && info.applicable)
        return false;

    const QString needle = m_search->text().trimmed().toLower();
    if (needle.isEmpty())
        return true;
    // Same fields the filter panel searches, plus plugin and categories, so the two
    // surfaces agree on what "matching" means.
    return info.descriptor.name.toLower().contains(needle)
        || info.descriptor.effectivePythonName().toLower().contains(needle)
        || info.descriptor.categories.join(QLatin1Char(' ')).toLower().contains(needle)
        || info.pluginName.toLower().contains(needle)
        || info.descriptor.shortDescription.toLower().contains(needle)
        || info.descriptor.provenance.project.toLower().contains(needle);
}

void FilterPluginsInfoDialog::rebuildTree()
{
    m_tree->clear();
    const bool byCategory = m_groupBy->currentData().toString() == QStringLiteral("category");

    auto makeFilterItem = [](QTreeWidgetItem *parent, const Document::FilterInfo &info) {
        auto *item = new QTreeWidgetItem(parent);
        item->setText(0, info.descriptor.name);
        item->setData(0, kRoleKind, QStringLiteral("filter"));
        item->setData(0, kRoleValue, info.key);
        if (!info.applicable) {
            item->setForeground(0, QBrush(QColor(160, 90, 90)));
            item->setToolTip(0, QObject::tr("Unavailable: %1").arg(info.applicabilityError));
        }
        return item;
    };

    int shown = 0;
    if (byCategory) {
        // Mirror the Filters menu: root -> subcategory -> filter, with a filter
        // appearing under every category it declares. Declared-but-empty categories
        // are shown greyed so gaps in the ontology are visible.
        QHash<QString, QTreeWidgetItem *> groups;
        std::function<QTreeWidgetItem *(const QString &)> groupFor;
        groupFor = [&](const QString &path) -> QTreeWidgetItem * {
            if (groups.contains(path))
                return groups.value(path);
            const QString rootName = path.section(QLatin1Char('/'), 0, 0);
            const QString sub = path.section(QLatin1Char('/'), 1);
            QTreeWidgetItem *parent = nullptr;
            if (sub.isEmpty()) {
                parent = new QTreeWidgetItem(m_tree);
                parent->setText(0, rootName);
            } else {
                QTreeWidgetItem *rootItem = groupFor(rootName);
                parent = new QTreeWidgetItem(rootItem);
                parent->setText(0, sub);
            }
            QFont f = parent->font(0);
            f.setBold(true);
            parent->setFont(0, f);
            parent->setData(0, kRoleKind, QStringLiteral("category"));
            parent->setData(0, kRoleValue, path);
            groups.insert(path, parent);
            return parent;
        };

        for (const QString &path : FilterCategories::allPaths())
            groupFor(path);

        QHash<QString, int> counts;
        for (const auto &info : m_filters) {
            if (!matches(info))
                continue;
            ++shown;
            for (const QString &c : info.descriptor.categories) {
                makeFilterItem(groupFor(c), info);
                ++counts[c];
                ++counts[c.section(QLatin1Char('/'), 0, 0)];
            }
        }
        for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
            const int n = counts.value(it.key());
            it.value()->setText(1, n > 0 ? QString::number(n) : QString());
            it.value()->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            if (n == 0)
                it.value()->setForeground(0, palette().brush(QPalette::Mid));
        }
    } else {
        QHash<QString, QTreeWidgetItem *> byPlugin;
        QHash<QString, int> counts;
        for (const auto &info : m_filters) {
            if (!matches(info))
                continue;
            ++shown;
            QTreeWidgetItem *parent = byPlugin.value(info.pluginId);
            if (!parent) {
                parent = new QTreeWidgetItem(m_tree);
                parent->setText(0, info.pluginName);
                QFont f = parent->font(0);
                f.setBold(true);
                parent->setFont(0, f);
                parent->setData(0, kRoleKind, QStringLiteral("plugin"));
                parent->setData(0, kRoleValue, info.pluginId);
                byPlugin.insert(info.pluginId, parent);
            }
            makeFilterItem(parent, info);
            ++counts[info.pluginId];
        }
        for (auto it = byPlugin.cbegin(); it != byPlugin.cend(); ++it) {
            it.value()->setText(1, QString::number(counts.value(it.key())));
            it.value()->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        }
        m_tree->sortItems(0, Qt::AscendingOrder);
    }

    // Expand only when the result set is small enough that expanding helps.
    if (shown <= 40)
        m_tree->expandAll();
    updateSummary(shown);
    updateDetail();
}

void FilterPluginsInfoDialog::updateSummary(int shownFilters)
{
    QSet<QString> plugins;
    int applicable = 0;
    for (const auto &info : m_filters) {
        plugins.insert(info.pluginId);
        if (info.applicable)
            ++applicable;
    }
    const int total = int(m_filters.size());
    QString text = tr("%1 filters in %2 plugins · %3 applicable · %4 categories in the ontology")
                       .arg(total)
                       .arg(plugins.size())
                       .arg(applicable)
                       .arg(FilterCategories::allPaths().size());
    if (shownFilters != total)
        text = tr("Showing %1 of %2 · ").arg(shownFilters).arg(total) + text;
    m_summary->setText(text);
}

void FilterPluginsInfoDialog::updateDetail()
{
    QTreeWidgetItem *item = m_tree->currentItem();
    if (!item) {
        m_detail->setHtml(QStringLiteral("<p style=\"color:palette(mid);\">%1</p>")
                              .arg(tr("Select a plugin, category or filter.")));
        return;
    }
    const QString kind = item->data(0, kRoleKind).toString();
    const QString value = item->data(0, kRoleValue).toString();

    if (kind == QStringLiteral("filter")) {
        const auto it = std::find_if(m_filters.cbegin(), m_filters.cend(),
                                     [&](const Document::FilterInfo &i) { return i.key == value; });
        if (it != m_filters.cend()) {
            m_detail->setHtml(filterDetailHtml(*it));
            return;
        }
    } else if (kind == QStringLiteral("plugin")) {
        m_detail->setHtml(pluginDetailHtml(value));
        return;
    } else if (kind == QStringLiteral("category")) {
        m_detail->setHtml(categoryDetailHtml(value));
        return;
    }
    m_detail->clear();
}

QString FilterPluginsInfoDialog::filterDetailHtml(const Document::FilterInfo &info) const
{
    const MeshFilterDescriptor &d = info.descriptor;
    QString html = QStringLiteral("<html><body style=\"font-size:11px; line-height:1.35;\">");
    html += heading(d.name, d.shortDescription);

    // Availability first: it is the question this dialog most often answers.
    if (info.applicable) {
        html += QStringLiteral("<p style=\"margin:2px 0; color:#2e7d32;\">%1</p>")
                    .arg(tr("Available"));
    } else {
        html += QStringLiteral("<p style=\"margin:2px 0;\"><b style=\"color:#b71c1c;\">%1</b> %2</p>")
                    .arg(tr("Unavailable —"), esc(info.applicabilityError));
    }

    QString cats;
    for (const QString &c : d.categories) {
        const bool primary = (c == d.primaryCategory());
        cats += QStringLiteral("<div>%1%2</div>")
                    .arg(esc(c),
                         primary && d.categories.size() > 1
                             ? QStringLiteral(" <span style=\"color:palette(mid);\">(%1)</span>")
                                   .arg(tr("primary"))
                             : QString());
    }

    html += QStringLiteral("<table style=\"margin-top:6px;\">");
    html += row(tr("Python"), QStringLiteral("<code>%1</code>").arg(esc(d.effectivePythonName())));
    html += row(tr("Plugin"), esc(info.pluginName));
    html += row(tr("Categories"), cats);
    html += row(tr("Input"), esc(filterInputDomainLabel(d.inputDomain)));
    html += row(tr("Output"), esc(filterOutputDomainLabel(d.outputDomain)));
    const QStringList reqs = requirementLabels(d.inputRequirements);
    html += row(tr("Requires"), esc(reqs.join(QStringLiteral(", "))));
    if (d.incrementalSelection)
        html += row(tr("Selection"), tr("supports incremental selection"));
    if (!d.provenance.project.trimmed().isEmpty())
        html += row(tr("Upstream"), esc(d.provenance.project));
    html += QStringLiteral("</table>");

    // formatFilterParameterDetails renders the expanded outputModifies sentence and
    // the parameter table, so the declared contract is readable rather than codes.
    html += formatFilterParameterDetails(d);

    if (!d.references.empty()) {
        html += QStringLiteral("<p style=\"margin:8px 0 2px; font-weight:bold;\">%1</p>")
                    .arg(tr("References"));
        // Hanging indent, as a bibliography would set it.
        for (const MeshFilterReference &r : d.references)
            html += QStringLiteral("<div style=\"margin:0 0 5px 14px; text-indent:-14px;\">%1</div>")
                        .arg(citationHtml(r));
    }
    html += QStringLiteral("</body></html>");
    return html;
}

QString FilterPluginsInfoDialog::pluginDetailHtml(const QString &pluginId) const
{
    QString name;
    int count = 0, applicable = 0;
    QStringList roots;
    MeshFilterProvenance prov;
    QStringList unavailable;
    for (const auto &info : m_filters) {
        if (info.pluginId != pluginId)
            continue;
        name = info.pluginName;
        ++count;
        if (info.applicable)
            ++applicable;
        else
            unavailable << info.descriptor.name;
        for (const QString &r : info.descriptor.categoryRoots())
            if (!roots.contains(r))
                roots << r;
        if (prov.isEmpty())
            prov = info.descriptor.provenance;
    }
    roots.sort();

    QString html = QStringLiteral("<html><body style=\"font-size:11px; line-height:1.35;\">");
    html += heading(name, pluginId);
    html += QStringLiteral("<table style=\"margin-top:6px;\">");
    html += row(tr("Filters"), QString::number(count));
    html += row(tr("Available"), QStringLiteral("%1 / %2").arg(applicable).arg(count));
    // One category per line: a plugin may span many, and joining them into a cell is
    // what made the old table unreadable.
    QString rootHtml;
    for (const QString &r : roots)
        rootHtml += QStringLiteral("<div>%1</div>").arg(esc(r));
    html += row(tr("Categories"), rootHtml);
    html += row(tr("Upstream"), esc(prov.project));
    html += row(tr("Repository"), linkHtml(prov.repository, prov.repository));
    html += row(tr("License"), esc(prov.license));
    html += row(tr("Integration"), esc(prov.integration));
    html += QStringLiteral("</table>");

    if (!unavailable.isEmpty()) {
        html += QStringLiteral("<p style=\"margin:8px 0 2px; font-weight:bold;\">%1</p>")
                    .arg(tr("Unavailable here (%1)").arg(unavailable.size()));
        for (const QString &n : unavailable)
            html += QStringLiteral("<div style=\"color:palette(mid);\">%1</div>").arg(esc(n));
    }
    html += QStringLiteral("</body></html>");
    return html;
}

QString FilterPluginsInfoDialog::categoryDetailHtml(const QString &category) const
{
    QStringList members;
    QStringList plugins;
    for (const auto &info : m_filters) {
        const bool hit = std::any_of(
            info.descriptor.categories.cbegin(), info.descriptor.categories.cend(),
            [&](const QString &c) {
                return c == category || c.startsWith(category + QLatin1Char('/'));
            });
        if (!hit)
            continue;
        members << info.descriptor.name;
        if (!plugins.contains(info.pluginName))
            plugins << info.pluginName;
    }
    plugins.sort();

    QString html = QStringLiteral("<html><body style=\"font-size:11px; line-height:1.35;\">");
    const QStringList subs = FilterCategories::subcategories(category);
    html += heading(category,
                    subs.isEmpty() ? QString() : tr("subcategories: %1").arg(subs.join(QStringLiteral(", "))));
    html += QStringLiteral("<table style=\"margin-top:6px;\">");
    html += row(tr("Filters"), QString::number(members.size()));
    QString pluginHtml;
    for (const QString &p : plugins)
        pluginHtml += QStringLiteral("<div>%1</div>").arg(esc(p));
    html += row(tr("Provided by"), pluginHtml);
    html += QStringLiteral("</table>");
    if (members.isEmpty())
        html += QStringLiteral("<p style=\"color:palette(mid);\">%1</p>")
                    .arg(tr("Declared in the ontology but no filter uses it yet."));
    html += QStringLiteral("</body></html>");
    return html;
}
