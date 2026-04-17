#include "meshfilterpanel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>
#include <QIcon>
#include <QRegularExpression>
#include <algorithm>
#include <limits>
#include <set>

namespace {
QString groupDisplayName(const QString &group)
{
    const QString trimmed = group.trimmed();
    if (trimmed.isEmpty())
        return QObject::tr("Main");
    QString name = trimmed;
    name.replace(QLatin1Char('_'), QLatin1Char(' '));
    name.replace(QLatin1Char('.'), QLatin1Char(' '));
    if (!name.isEmpty())
        name[0] = name[0].toUpper();
    return name;
}

QStringList tokenizeSearchTerms(const QString &text)
{
    static const QRegularExpression kSpaceRe(QStringLiteral("\\s+"));
    QStringList terms = text.trimmed().toLower().split(kSpaceRe, Qt::SkipEmptyParts);
    terms.removeDuplicates();
    return terms;
}
}

MeshFilterPanel::MeshFilterPanel(Document *doc, QWidget *parent)
    : QWidget(parent)
    , m_doc(doc)
{
    buildUi();
    reloadFilters();
}

void MeshFilterPanel::buildUi()
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(6, 6, 6, 6);
    rootLayout->setSpacing(6);

    auto *searchLayout = new QHBoxLayout();
    searchLayout->setContentsMargins(0, 0, 0, 0);
    searchLayout->setSpacing(4);
    m_searchButton = new QToolButton(this);
    m_searchButton->setAutoRaise(true);
    const QIcon searchIcon = QIcon::fromTheme(QIcon::ThemeIcon::SystemSearch);
    if (!searchIcon.isNull())
        m_searchButton->setIcon(searchIcon);
    else
        m_searchButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
    m_searchButton->setToolTip(tr("Search filters"));
    searchLayout->addWidget(m_searchButton, 0);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Search filters..."));
    m_searchEdit->installEventFilter(this);
    searchLayout->addWidget(m_searchEdit, 1);
    rootLayout->addLayout(searchLayout);

    m_stack = new QStackedWidget(this);
    rootLayout->addWidget(m_stack, 1);

    m_resultsPage = new QWidget(m_stack);
    auto *resultsLayout = new QVBoxLayout(m_resultsPage);
    resultsLayout->setContentsMargins(0, 0, 0, 0);
    resultsLayout->setSpacing(4);
    m_resultsList = new QListWidget(m_resultsPage);
    m_resultsList->setSelectionMode(QAbstractItemView::SingleSelection);
    resultsLayout->addWidget(m_resultsList, 1);
    m_stack->addWidget(m_resultsPage);

    m_parametersPage = new QWidget(m_stack);
    auto *paramsPageLayout = new QVBoxLayout(m_parametersPage);
    paramsPageLayout->setContentsMargins(0, 0, 0, 0);
    paramsPageLayout->setSpacing(6);

    auto *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(6);

    m_filterTitleLabel = new QLabel(m_parametersPage);
    QFont titleFont = m_filterTitleLabel->font();
    titleFont.setBold(true);
    m_filterTitleLabel->setFont(titleFont);
    m_filterTitleLabel->setWordWrap(true);
    headerLayout->addWidget(m_filterTitleLabel, 1);

    m_longDescriptionToggle = new QToolButton(m_parametersPage);
    m_longDescriptionToggle->setCheckable(true);
    m_longDescriptionToggle->setChecked(false);
    m_longDescriptionToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_longDescriptionToggle->setText(QStringLiteral("?"));
    m_longDescriptionToggle->setToolTip(tr("Show details"));
    m_longDescriptionToggle->hide();
    m_applyButton = new QPushButton(tr("Apply"), m_parametersPage);
    headerLayout->addWidget(m_longDescriptionToggle, 0, Qt::AlignTop);
    headerLayout->addWidget(m_applyButton, 0, Qt::AlignTop);
    paramsPageLayout->addLayout(headerLayout);

    m_filterDescriptionLabel = new QLabel(m_parametersPage);
    m_filterDescriptionLabel->setWordWrap(true);
    m_filterDescriptionLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
    paramsPageLayout->addWidget(m_filterDescriptionLabel);

    m_longDescriptionView = new QTextBrowser(m_parametersPage);
    m_longDescriptionView->setOpenExternalLinks(true);
    m_longDescriptionView->setVisible(false);
    m_longDescriptionView->setMaximumHeight(180);
    paramsPageLayout->addWidget(m_longDescriptionView);

    m_showAdvancedCheck = new QCheckBox(tr("Show advanced parameters"), m_parametersPage);
    m_showAdvancedCheck->setChecked(false);
    paramsPageLayout->addWidget(m_showAdvancedCheck);

    m_parametersScroll = new QScrollArea(m_parametersPage);
    m_parametersScroll->setWidgetResizable(true);
    m_parametersWidget = new QWidget(m_parametersScroll);
    m_parametersLayout = new QFormLayout(m_parametersWidget);
    m_parametersLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_parametersLayout->setContentsMargins(0, 0, 0, 0);
    m_noParametersLabel = new QLabel(tr("This filter has no parameters."), m_parametersWidget);
    m_noParametersLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
    m_parametersLayout->addRow(m_noParametersLabel);
    m_parametersScroll->setWidget(m_parametersWidget);
    paramsPageLayout->addWidget(m_parametersScroll, 1);

    m_stack->addWidget(m_parametersPage);
    m_stack->setCurrentWidget(m_resultsPage);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &MeshFilterPanel::onSearchTextChanged);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &MeshFilterPanel::onSearchReturnPressed);
    connect(m_resultsList, &QListWidget::itemClicked, this, &MeshFilterPanel::onResultItemClicked);
    connect(m_resultsList, &QListWidget::itemActivated, this, &MeshFilterPanel::onResultItemActivated);
    connect(m_searchButton, &QToolButton::clicked, this, [this]() { showSearchResultsFromUi(true); });
    connect(m_applyButton, &QPushButton::clicked, this, &MeshFilterPanel::onApplyClicked);
    connect(m_showAdvancedCheck, &QCheckBox::toggled, this, &MeshFilterPanel::onShowAdvancedToggled);
    connect(m_longDescriptionToggle, &QToolButton::toggled, this, [this](bool checked) {
        if (m_longDescriptionView)
            m_longDescriptionView->setVisible(checked);
    });
}

void MeshFilterPanel::reloadFilters()
{
    cacheCurrentFilterParameters();
    const QString previousKey = m_currentFilterKey;
    if (m_doc) {
        m_filters = m_doc->filterInfos();
        std::sort(
            m_filters.begin(),
            m_filters.end(),
            [](const Document::FilterInfo &a, const Document::FilterInfo &b) {
                const int menuCmp =
                    a.descriptor.menuPath.compare(b.descriptor.menuPath, Qt::CaseInsensitive);
                if (menuCmp != 0)
                    return menuCmp < 0;
                return a.descriptor.name.compare(b.descriptor.name, Qt::CaseInsensitive) < 0;
            });
    } else {
        m_filters.clear();
    }

    rebuildResultsList();
    if (!previousKey.isEmpty()) {
        for (int i = 0; i < static_cast<int>(m_filters.size()); ++i) {
            if (m_filters[static_cast<size_t>(i)].key == previousKey) {
                openFilterAtIndex(i);
                return;
            }
        }
    }

    m_currentFilterKey.clear();
    m_stack->setCurrentWidget(m_resultsPage);
}

void MeshFilterPanel::showSearchResults()
{
    m_stack->setCurrentWidget(m_resultsPage);
}

void MeshFilterPanel::focusSearch()
{
    if (!m_searchEdit)
        return;
    m_searchEdit->setFocus(Qt::OtherFocusReason);
    m_searchEdit->selectAll();
}

void MeshFilterPanel::selectFilterByKey(const QString &filterKey, bool openParameters)
{
    if (filterKey.trimmed().isEmpty())
        return;

    for (int i = 0; i < static_cast<int>(m_filters.size()); ++i) {
        if (m_filters[static_cast<size_t>(i)].key != filterKey)
            continue;

        for (int row = 0; row < m_resultsList->count(); ++row) {
            QListWidgetItem *item = m_resultsList->item(row);
            if (!item)
                continue;
            if (item->data(Qt::UserRole).toInt() == i) {
                m_resultsList->setCurrentRow(row);
                break;
            }
        }
        if (openParameters)
            openFilterAtIndex(i);
        return;
    }
}

void MeshFilterPanel::onSearchTextChanged(const QString &)
{
    showSearchResultsFromUi(false);
    rebuildResultsList();
}

void MeshFilterPanel::onSearchReturnPressed()
{
    if (m_stack->currentWidget() == m_parametersPage) {
        onApplyClicked();
        return;
    }
    openSelectedResult(true);
}

void MeshFilterPanel::onResultItemClicked(QListWidgetItem *item)
{
    if (!item)
        return;
    const int filterIndex = item->data(Qt::UserRole).toInt();
    openFilterAtIndex(filterIndex);
}

void MeshFilterPanel::onResultItemActivated(QListWidgetItem *)
{
    openSelectedResult(true);
}

void MeshFilterPanel::onApplyClicked()
{
    if (m_currentFilterKey.trimmed().isEmpty())
        return;
    const Document::FilterInfo *info = filterByKey(m_currentFilterKey);
    if (!info || !info->applicable)
        return;

    const MeshFilterParameterValues parameters = collectCurrentParameterValues();
    m_filterParameterCache.insert(m_currentFilterKey, parameters);
    emit runRequested(m_currentFilterKey, parameters, info->descriptor.name);
}

void MeshFilterPanel::onShowAdvancedToggled(bool checked)
{
    for (auto &binding : m_parameterBindings) {
        if (!binding.advanced)
            continue;
        if (binding.formLabel)
            binding.formLabel->setVisible(checked);
        if (binding.rowField)
            binding.rowField->setVisible(checked);
    }
}

void MeshFilterPanel::rebuildResultsList()
{
    m_resultsList->clear();
    m_visibleFilterIndices.clear();
    const QStringList terms = tokenizeSearchTerms(m_searchEdit->text());
    std::vector<int> titleFirst;
    std::vector<int> otherMatches;

    for (int i = 0; i < static_cast<int>(m_filters.size()); ++i) {
        const Document::FilterInfo &info = m_filters[static_cast<size_t>(i)];
        if (!matchesSearch(info, terms))
            continue;

        if (titleMatchesAllTerms(info, terms))
            titleFirst.push_back(i);
        else
            otherMatches.push_back(i);
    }

    auto appendResultItem = [this](int filterIndex) {
        const Document::FilterInfo &info = m_filters[static_cast<size_t>(filterIndex)];
        m_visibleFilterIndices.push_back(filterIndex);
        const QString text = info.descriptor.menuPath.trimmed().isEmpty()
            ? info.descriptor.name
            : QStringLiteral("%1 / %2").arg(info.descriptor.menuPath, info.descriptor.name);
        auto *item = new QListWidgetItem(text, m_resultsList);
        item->setData(Qt::UserRole, filterIndex);
        QString tip = info.descriptor.shortDescription.trimmed();
        if (!info.applicable && !info.applicabilityError.trimmed().isEmpty()) {
            if (!tip.isEmpty())
                tip += QStringLiteral("\n");
            tip += tr("Unavailable: %1").arg(info.applicabilityError);
            item->setForeground(palette().brush(QPalette::Mid));
        }
        if (!tip.isEmpty())
            item->setToolTip(tip);
    };

    for (int filterIndex : titleFirst)
        appendResultItem(filterIndex);
    for (int filterIndex : otherMatches)
        appendResultItem(filterIndex);

    if (m_resultsList->count() > 0 && m_resultsList->currentRow() < 0)
        m_resultsList->setCurrentRow(0);
}

void MeshFilterPanel::openSelectedResult(bool focusApplyButton)
{
    if (!m_resultsList)
        return;

    QListWidgetItem *item = m_resultsList->currentItem();
    if (!item && m_resultsList->count() > 0) {
        item = m_resultsList->item(0);
        m_resultsList->setCurrentItem(item);
    }
    if (!item)
        return;

    onResultItemClicked(item);
    if (focusApplyButton && m_applyButton && m_stack->currentWidget() == m_parametersPage)
        m_applyButton->setFocus(Qt::OtherFocusReason);
}

void MeshFilterPanel::openFilterAtIndex(int filterIndex)
{
    if (filterIndex < 0 || filterIndex >= static_cast<int>(m_filters.size()))
        return;

    cacheCurrentFilterParameters();

    const Document::FilterInfo &info = m_filters[static_cast<size_t>(filterIndex)];
    m_currentFilterKey = info.key;
    m_filterTitleLabel->setText(info.descriptor.name);
    m_filterDescriptionLabel->setText(info.descriptor.shortDescription);
    const QString longDescription = info.descriptor.longDescriptionMarkdown.trimmed();
    const bool hasLongDescription = !longDescription.isEmpty();
    m_longDescriptionToggle->setVisible(hasLongDescription);
    if (hasLongDescription) {
        m_longDescriptionView->setMarkdown(longDescription);
    } else {
        m_longDescriptionView->clear();
    }
    if (m_longDescriptionToggle->isChecked())
        m_longDescriptionToggle->setChecked(false);
    else
        m_longDescriptionView->setVisible(false);
    if (info.applicable) {
        m_filterDescriptionLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
        m_applyButton->setToolTip(QString());
    } else {
        const QString reason = info.applicabilityError.trimmed().isEmpty()
            ? tr("This filter is not available in the current context.")
            : info.applicabilityError.trimmed();
        m_filterDescriptionLabel->setStyleSheet(QStringLiteral("color: #a13a3a;"));
        m_applyButton->setToolTip(tr("Unavailable: %1").arg(reason));
    }
    buildParameterEditors(info);
    const auto cacheIt = m_filterParameterCache.constFind(info.key);
    if (cacheIt != m_filterParameterCache.constEnd())
        applyParameterValuesToEditors(cacheIt.value());
    m_applyButton->setEnabled(info.applicable);
    m_stack->setCurrentWidget(m_parametersPage);
}

bool MeshFilterPanel::matchesSearch(
    const Document::FilterInfo &filterInfo,
    const QStringList &terms) const
{
    if (terms.isEmpty())
        return true;

    const QString name = filterInfo.descriptor.name.toLower();
    const QString shortDesc = filterInfo.descriptor.shortDescription.toLower();
    const QString longDesc = filterInfo.descriptor.longDescriptionMarkdown.toLower();

    for (const QString &term : terms) {
        if (term.isEmpty())
            continue;
        const bool termMatched =
            name.contains(term)
            || shortDesc.contains(term)
            || longDesc.contains(term);
        if (!termMatched)
            return false;
    }
    return true;
}

bool MeshFilterPanel::titleMatchesAllTerms(
    const Document::FilterInfo &filterInfo,
    const QStringList &terms) const
{
    if (terms.isEmpty())
        return true;

    const QString name = filterInfo.descriptor.name.toLower();
    for (const QString &term : terms) {
        if (term.isEmpty())
            continue;
        if (!name.contains(term))
            return false;
    }
    return true;
}

void MeshFilterPanel::clearParameterEditors()
{
    m_parameterBindings.clear();
    while (m_parametersLayout->rowCount() > 0)
        m_parametersLayout->removeRow(0);
    m_noParametersLabel = new QLabel(tr("This filter has no parameters."), m_parametersWidget);
    m_noParametersLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
    m_parametersLayout->addRow(m_noParametersLabel);
}

void MeshFilterPanel::buildParameterEditors(const Document::FilterInfo &filterInfo)
{
    clearParameterEditors();
    if (filterInfo.descriptor.parameters.empty()) {
        m_noParametersLabel->show();
        if (m_showAdvancedCheck) {
            m_showAdvancedCheck->setChecked(false);
            m_showAdvancedCheck->hide();
        }
        return;
    }
    if (m_noParametersLabel)
        m_noParametersLabel->hide();

    std::set<QString> uniqueGroups;
    for (const auto &param : filterInfo.descriptor.parameters) {
        if (param.id.trimmed().isEmpty())
            continue;
        uniqueGroups.insert(param.group);
    }
    const bool showGroupHeaders = uniqueGroups.size() > 1;

    QString currentGroup;
    bool hasAdvanced = false;
    for (const auto &param : filterInfo.descriptor.parameters) {
        if (param.id.trimmed().isEmpty())
            continue;
        if (showGroupHeaders && currentGroup != param.group) {
            currentGroup = param.group;
            auto *groupLabel = new QLabel(groupDisplayName(currentGroup), m_parametersWidget);
            QFont f = groupLabel->font();
            f.setBold(true);
            groupLabel->setFont(f);
            groupLabel->setStyleSheet(QStringLiteral("color: palette(mid); padding-top: 6px;"));
            m_parametersLayout->addRow(groupLabel);
        } else {
            currentGroup = param.group;
        }

        ParameterBinding binding;
        binding.descriptor = param;
        binding.advanced = param.isAdvancedGroup();
        hasAdvanced = hasAdvanced || binding.advanced;

        QWidget *editor = nullptr;
        switch (param.type) {
        case MeshFilterParameterType::Bool: {
            auto *w = new QCheckBox(m_parametersWidget);
            w->setChecked(param.defaultValue.toBool());
            editor = w;
            break;
        }
        case MeshFilterParameterType::Int: {
            auto *w = new QSpinBox(m_parametersWidget);
            const int minV = param.minValue.isValid() ? param.minValue.toInt() : std::numeric_limits<int>::lowest();
            const int maxV = param.maxValue.isValid() ? param.maxValue.toInt() : std::numeric_limits<int>::max();
            w->setRange(minV, maxV);
            w->setValue(param.defaultValue.isValid() ? param.defaultValue.toInt() : 0);
            editor = w;
            break;
        }
        case MeshFilterParameterType::Double: {
            auto *w = new QDoubleSpinBox(m_parametersWidget);
            const double minV = param.minValue.isValid() ? param.minValue.toDouble() : -1e12;
            const double maxV = param.maxValue.isValid() ? param.maxValue.toDouble() : 1e12;
            w->setRange(minV, maxV);
            w->setDecimals(std::clamp(param.decimals, 0, 10));
            w->setValue(param.defaultValue.isValid() ? param.defaultValue.toDouble() : 0.0);
            editor = w;
            break;
        }
        case MeshFilterParameterType::String: {
            auto *w = new QLineEdit(m_parametersWidget);
            w->setText(param.defaultValue.toString());
            editor = w;
            break;
        }
        case MeshFilterParameterType::Enum: {
            auto *w = new QComboBox(m_parametersWidget);
            for (const auto &opt : param.enumOptions)
                w->addItem(opt.label, opt.id);
            const QString defaultId = param.defaultValue.toString();
            if (!defaultId.isEmpty()) {
                const int pos = w->findData(defaultId);
                if (pos >= 0)
                    w->setCurrentIndex(pos);
            }
            editor = w;
            break;
        }
        case MeshFilterParameterType::Color: {
            auto *w = new QPushButton(m_parametersWidget);
            const QColor c = colorFromVariant(param.defaultValue, QColor(Qt::white));
            w->setProperty("filterColor", c);
            updateColorButtonStyle(w, c);
            connect(w, &QPushButton::clicked, this, [this, w]() {
                const QColor current = colorFromVariant(w->property("filterColor"), QColor(Qt::white));
                const QColor chosen = QColorDialog::getColor(current, this, tr("Choose Color"));
                if (!chosen.isValid())
                    return;
                w->setProperty("filterColor", chosen);
                updateColorButtonStyle(w, chosen);
            });
            editor = w;
            break;
        }
        }

        if (!editor)
            continue;
        binding.editor = editor;

        auto *labelWidget = new QLabel(param.label, m_parametersWidget);
        binding.formLabel = labelWidget;
        binding.rowField = editor;
        if (!param.helpMarkdown.trimmed().isEmpty()) {
            labelWidget->setToolTip(param.helpMarkdown);
            editor->setToolTip(param.helpMarkdown);
        }
        m_parametersLayout->addRow(labelWidget, editor);

        if (binding.advanced && !m_showAdvancedCheck->isChecked()) {
            labelWidget->hide();
            editor->hide();
        }

        m_parameterBindings.push_back(std::move(binding));
    }

    if (m_showAdvancedCheck) {
        if (hasAdvanced) {
            m_showAdvancedCheck->show();
        } else {
            m_showAdvancedCheck->setChecked(false);
            m_showAdvancedCheck->hide();
        }
    }
}

MeshFilterParameterValues MeshFilterPanel::collectCurrentParameterValues() const
{
    MeshFilterParameterValues values;
    for (const ParameterBinding &binding : m_parameterBindings) {
        const QVariant value = parameterValue(binding);
        if (value.isValid())
            values.insert(binding.descriptor.id, value);
    }
    return values;
}

void MeshFilterPanel::applyParameterValuesToEditors(const MeshFilterParameterValues &values)
{
    for (const ParameterBinding &binding : m_parameterBindings) {
        QWidget *editor = binding.editor;
        if (!editor)
            continue;

        const auto it = values.constFind(binding.descriptor.id);
        if (it == values.constEnd())
            continue;

        const QVariant value = it.value();
        switch (binding.descriptor.type) {
        case MeshFilterParameterType::Bool: {
            if (auto *w = qobject_cast<QCheckBox *>(editor))
                w->setChecked(value.toBool());
            break;
        }
        case MeshFilterParameterType::Int: {
            if (auto *w = qobject_cast<QSpinBox *>(editor))
                w->setValue(value.toInt());
            break;
        }
        case MeshFilterParameterType::Double: {
            if (auto *w = qobject_cast<QDoubleSpinBox *>(editor))
                w->setValue(value.toDouble());
            break;
        }
        case MeshFilterParameterType::String: {
            if (auto *w = qobject_cast<QLineEdit *>(editor))
                w->setText(value.toString());
            break;
        }
        case MeshFilterParameterType::Enum: {
            if (auto *w = qobject_cast<QComboBox *>(editor)) {
                const QString enumId = value.toString();
                const int pos = w->findData(enumId);
                if (pos >= 0)
                    w->setCurrentIndex(pos);
            }
            break;
        }
        case MeshFilterParameterType::Color: {
            const QColor fallback = colorFromVariant(editor->property("filterColor"), QColor(Qt::white));
            const QColor c = colorFromVariant(value, fallback);
            editor->setProperty("filterColor", c);
            updateColorButtonStyle(editor, c);
            break;
        }
        }
    }
}

void MeshFilterPanel::cacheCurrentFilterParameters()
{
    if (m_currentFilterKey.trimmed().isEmpty())
        return;
    m_filterParameterCache.insert(m_currentFilterKey, collectCurrentParameterValues());
}

QVariant MeshFilterPanel::parameterValue(const ParameterBinding &binding) const
{
    QWidget *editor = binding.editor;
    if (!editor)
        return {};
    switch (binding.descriptor.type) {
    case MeshFilterParameterType::Bool:
        return qobject_cast<QCheckBox *>(editor)->isChecked();
    case MeshFilterParameterType::Int:
        return qobject_cast<QSpinBox *>(editor)->value();
    case MeshFilterParameterType::Double:
        return qobject_cast<QDoubleSpinBox *>(editor)->value();
    case MeshFilterParameterType::String:
        return qobject_cast<QLineEdit *>(editor)->text();
    case MeshFilterParameterType::Enum:
        return qobject_cast<QComboBox *>(editor)->currentData().toString();
    case MeshFilterParameterType::Color:
        return colorFromVariant(editor->property("filterColor"), QColor(Qt::white));
    }
    return {};
}

QColor MeshFilterPanel::colorFromVariant(const QVariant &value, const QColor &fallback) const
{
    if (value.userType() == QMetaType::QColor) {
        const QColor c = value.value<QColor>();
        return c.isValid() ? c : fallback;
    }
    const QString s = value.toString().trimmed();
    if (s.isEmpty())
        return fallback;
    const QColor c(s);
    return c.isValid() ? c : fallback;
}

void MeshFilterPanel::updateColorButtonStyle(QWidget *button, const QColor &color) const
{
    if (!button)
        return;
    button->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; border: 1px solid palette(mid); min-height: 24px; }")
        .arg(color.name(QColor::HexRgb)));
}

const Document::FilterInfo *MeshFilterPanel::filterByKey(const QString &filterKey) const
{
    if (filterKey.trimmed().isEmpty())
        return nullptr;
    for (const auto &info : m_filters) {
        if (info.key == filterKey)
            return &info;
    }
    return nullptr;
}

void MeshFilterPanel::showSearchResultsFromUi(bool focusSearch)
{
    if (m_stack && m_stack->currentWidget() == m_parametersPage)
        cacheCurrentFilterParameters();
    showSearchResults();
    if (focusSearch && m_searchEdit)
        m_searchEdit->setFocus(Qt::OtherFocusReason);
}

bool MeshFilterPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_searchEdit && event) {
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::FocusIn) {
            showSearchResultsFromUi(false);
        }
    }
    if (watched == m_searchEdit && event && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Down && keyEvent->modifiers() == Qt::NoModifier) {
            showSearchResultsFromUi(false);
            if (m_resultsList && m_resultsList->count() > 0) {
                if (m_resultsList->currentRow() < 0)
                    m_resultsList->setCurrentRow(0);
                m_resultsList->setFocus(Qt::OtherFocusReason);
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
