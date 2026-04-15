#pragma once

#include "document.h"
#include <QColor>
#include <QWidget>
#include <vector>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QFormLayout;

class MeshFilterPanel : public QWidget
{
    Q_OBJECT
public:
    explicit MeshFilterPanel(Document *doc, QWidget *parent = nullptr);

    void reloadFilters();
    void showSearchResults();
    void focusSearch();
    void selectFilterByKey(const QString &filterKey, bool openParameters = true);

signals:
    void runRequested(
        const QString &filterKey,
        const MeshFilterParameterValues &parameters,
        const QString &filterLabel);

private slots:
    void onSearchTextChanged(const QString &text);
    void onResultItemClicked(QListWidgetItem *item);
    void onBackClicked();
    void onApplyClicked();
    void onShowAdvancedToggled(bool checked);

private:
    struct ParameterBinding {
        MeshFilterParameterDescriptor descriptor;
        QWidget *editor = nullptr;
        QWidget *formLabel = nullptr;
        QWidget *rowField = nullptr;
        bool advanced = false;
    };

    void buildUi();
    void rebuildResultsList();
    void openFilterAtIndex(int filterIndex);
    void clearParameterEditors();
    void buildParameterEditors(const Document::FilterInfo &filterInfo);
    QVariant parameterValue(const ParameterBinding &binding) const;
    bool matchesSearch(const Document::FilterInfo &filterInfo, const QString &needle) const;
    QColor colorFromVariant(const QVariant &value, const QColor &fallback) const;
    void updateColorButtonStyle(QWidget *button, const QColor &color) const;
    const Document::FilterInfo *filterByKey(const QString &filterKey) const;

    Document *m_doc = nullptr;
    std::vector<Document::FilterInfo> m_filters;
    std::vector<int> m_visibleFilterIndices;
    std::vector<ParameterBinding> m_parameterBindings;
    QString m_currentFilterKey;

    QLineEdit *m_searchEdit = nullptr;
    QStackedWidget *m_stack = nullptr;
    QWidget *m_resultsPage = nullptr;
    QListWidget *m_resultsList = nullptr;
    QWidget *m_parametersPage = nullptr;
    QLabel *m_filterTitleLabel = nullptr;
    QLabel *m_filterDescriptionLabel = nullptr;
    QCheckBox *m_showAdvancedCheck = nullptr;
    QScrollArea *m_parametersScroll = nullptr;
    QWidget *m_parametersWidget = nullptr;
    QFormLayout *m_parametersLayout = nullptr;
    QLabel *m_noParametersLabel = nullptr;
    QPushButton *m_backButton = nullptr;
    QPushButton *m_applyButton = nullptr;
};
