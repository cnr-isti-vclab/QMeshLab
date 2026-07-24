#pragma once

#include "document.h"
#include <QColor>
#include <QHash>
#include <QStringList>
#include <QVector3D>
#include <QWidget>
#include <functional>
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
class QTextBrowser;
class QToolButton;

class MeshFilterPanel : public QWidget
{
    Q_OBJECT
public:
    explicit MeshFilterPanel(Document *doc, QWidget *parent = nullptr);

    void reloadFilters();
    void reloadFilters(const std::vector<Document::FilterInfo> &filters);
    void showSearchResults();
    void focusSearch();
    void selectFilterByKey(const QString &filterKey, bool openParameters = true);

    struct ViewContext {
        QVector3D trackballCenter;
        QVector3D eyePosition;
        QVector3D viewDirection;
    };

    // Provide a function that returns the current camera view context.
    // When set, the Point3f editor exposes context-fill shortcuts.
    void setViewContextProvider(std::function<ViewContext()> fn);

    // Legacy name kept for compatibility.
    void setTrackballCenterProvider(std::function<QVector3D()> fn);

    // Provide current-view JSON snapshots for typed camera/render-state parameters.
    void setCameraStateProvider(std::function<QString()> fn);
    void setRenderStateProvider(std::function<QString()> fn);

signals:
    void runRequested(
        const QString &filterKey,
        const MeshFilterParameterValues &parameters,
        const QString &filterLabel);
    void copyToConsoleRequested(const QString &code);

private slots:
    void onSearchTextChanged(const QString &text);
    void onSearchReturnPressed();
    void onResultItemClicked(QListWidgetItem *item);
    void onResultItemActivated(QListWidgetItem *item);
    void onApplyClicked();
    void onResetParametersClicked();
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
    void refreshDependentParameterEditors();
    void refreshCurrentFilterApplicability();
    MeshFilterParameterValues collectCurrentParameterValues() const;
    void applyParameterValuesToEditors(const MeshFilterParameterValues &values);
    void cacheCurrentFilterParameters();
    QVariant parameterValue(const ParameterBinding &binding) const;
    const ParameterBinding *bindingById(const QString &parameterId) const;
    bool matchesSearch(const Document::FilterInfo &filterInfo, const QStringList &terms) const;
    bool titleMatchesAllTerms(const Document::FilterInfo &filterInfo, const QStringList &terms) const;
    void openSelectedResult(bool focusApplyButton);
    QColor colorFromVariant(const QVariant &value, const QColor &fallback) const;
    void updateColorButtonStyle(QWidget *button, const QColor &color) const;
    const Document::FilterInfo *filterByKey(const QString &filterKey) const;
    void showSearchResultsFromUi(bool focusSearch);
    bool eventFilter(QObject *watched, QEvent *event) override;

    Document *m_doc = nullptr;
    std::function<ViewContext()> m_viewContextProvider;
    std::function<QString()> m_cameraStateProvider;
    std::function<QString()> m_renderStateProvider;
    std::vector<Document::FilterInfo> m_filters;
    std::vector<int> m_visibleFilterIndices;
    std::vector<ParameterBinding> m_parameterBindings;
    QHash<QString, MeshFilterParameterValues> m_filterParameterCache;
    QString m_currentFilterKey;

    QToolButton *m_searchButton = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QStackedWidget *m_stack = nullptr;
    QWidget *m_resultsPage = nullptr;
    QListWidget *m_resultsList = nullptr;
    QWidget *m_parametersPage = nullptr;
    QLabel *m_filterTitleLabel = nullptr;
    QLabel *m_filterDescriptionLabel = nullptr;
    QLabel *m_filterModifiesLabel = nullptr;
    QToolButton *m_longDescriptionToggle = nullptr;
    QTextBrowser *m_longDescriptionView = nullptr;
    QCheckBox *m_showAdvancedCheck = nullptr;
    QCheckBox *m_applyToAllVisible = nullptr;
    QScrollArea *m_parametersScroll = nullptr;
    QWidget *m_parametersWidget = nullptr;
    QFormLayout *m_parametersLayout = nullptr;
    QLabel *m_noParametersLabel = nullptr;
    QPushButton *m_applyButton = nullptr;
    QToolButton *m_resetParametersButton = nullptr;
    QToolButton *m_copyToConsoleButton = nullptr;
    QString m_currentFilterUnavailableReason;
};
