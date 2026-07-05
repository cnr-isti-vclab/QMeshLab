#pragma once

#include <QMainWindow>
#include <QImage>
#include <QStringList>
#include <QList>
#include <QMap>
#include <QPixmap>
#include <QVector>
#include <QVariantMap>
#include <array>
#include <deque>
#include <memory>
#include <vector>

class Document;
class InteractiveTool;
class RenderWidget;
class LayerWidget;
class MeshFilterPanel;
struct MeshFilterRunResult;
class PythonConsoleWidget;
class QMenu;
class QAction;
class QLabel;
class QProgressBar;
class QSplitter;
class QDockWidget;
class QToolButton;
class QListWidget;
class QTimer;
class UndoGraphWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void newDocument();
    void newInstance();
    void openFile();
    void openRasterImage();
    void reloadCurrentMesh();
    void reloadAllMeshes();
    void undo();
    void redo();
    void saveCurrentMesh();
    void saveProjectAs();
    void saveSnapshotPng();
    void addSnapshotRaster();
    void openFilterBrowser();
    void runFilterAction();
    void openLastMesh();
    void openRecentMesh();
    void showAbout();
    void showImportPlugins();
    void showFilterPlugins();
    void showMemoryInfo();
    void resetCamera();
    void copyCameraState();
    void pasteCameraState();
    void setCurrentViewSceneMode();
    void setCurrentViewParametrizationMode();
    void setCurrentViewRasterMode();
    void splitViewHorizontally();
    void splitViewVertically();

private:
    RenderWidget *currentRenderWidget() const;
    RenderWidget *createRenderWidget(QSplitter *parentSplitter);
    void setCurrentRenderWidget(RenderWidget *view);
    void updateCurrentViewBorder();
    void splitCurrentView(Qt::Orientation orientation);
    bool closeRenderWidget(RenderWidget *view);
    void closeCurrentView();
    void syncDocumentVisibilityFromCurrentView();
    bool loadMeshFromPath(const QString &filePath);
    bool loadRasterFromPath(const QString &filePath);
    bool handleDragEnterOrMove(QDropEvent *event);
    void handleDroppedUrls(const QList<QUrl> &urls);
    void addRecentMesh(const QString &filePath);
    void sanitizeRecentMeshes();
    void refreshRecentMeshesMenu();
    void openRecentMeshByIndex(int index);
    void syncCameraViewsFrom(RenderWidget *sourceView);
    void refreshFiltersMenu();
    void setupToolsMenu(QMenu *toolsMenu);
    void setActiveToolIndex(int index);   // -1 clears the active tool
    void applyActiveToolToCurrentView();
    void exitActiveTool();                // Esc from a view: uncheck + clear
    void executeFilter(
        const QString &filterKey,
        const QString &fallbackLabel,
        const QVariantMap &parameters = {});
    void applyFilterVisualizationHints(const MeshFilterRunResult &result);
    void updateFrameTimeStats(float cpuMs, float gpuMs, bool gpuTimingSupported, bool gpuSampleValid);
    void refreshUndoHistoryPanel();
    void jumpToUndoNode(int nodeId, bool withCamera = true);
    QPixmap captureUndoHistoryThumbnail() const;

    Document *m_doc;
    QSplitter *m_viewSplitter = nullptr;
    QList<RenderWidget *> m_renderWidgets;
    RenderWidget *m_currentRenderWidget = nullptr;
    bool m_cameraSyncEnabled = false;
    bool m_syncingCameraViews = false;
    bool m_syncingVisibilityProxy = false;
    LayerWidget *m_layerWidget;
    MeshFilterPanel *m_filterPanel = nullptr;
    QDockWidget *m_layerDock = nullptr;
    QDockWidget *m_filterDock = nullptr;
    QDockWidget *m_logDock = nullptr;
    QDockWidget *m_pythonConsoleDock = nullptr;
    PythonConsoleWidget *m_pythonConsole = nullptr;
    QToolButton *m_terminalButton = nullptr;
    QMenu *m_recentMenu = nullptr;
    QMenu *m_filtersMenu = nullptr;
    std::vector<std::unique_ptr<InteractiveTool>> m_interactiveTools;
    QList<QAction *> m_toolActions;
    int m_activeToolIndex = -1;
    QAction *m_openLastAction = nullptr;
    std::array<QAction *, 8> m_recentActions = {};
    QStringList m_recentMeshes;
    QProgressBar *m_loadProgressBar = nullptr;
    QProgressBar *m_filterProgressBar = nullptr;
    QToolButton *m_filterCancelButton = nullptr;
    QLabel *m_frameStatsLabel = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    QListWidget *m_logListWidget = nullptr;
    UndoGraphWidget *m_undoHistoryLaneWidget = nullptr;
    QLabel *m_undoHistoryPreviewPopup = nullptr;
    QTimer *m_undoHistoryPreviewTimer = nullptr;
    int m_pendingUndoHistoryPreviewNodeId = -1;
    QPoint m_pendingUndoHistoryPreviewGlobalPos;
    QMap<int, QPixmap> m_undoNodeThumbnails; // keyed by nodeId — 2:1 row icon
    QMap<int, QPixmap> m_undoNodeSnapshots;  // keyed by nodeId — 50% size hover image
    std::deque<float> m_lastCpuFrameTimes;
    std::deque<float> m_lastGpuFrameTimes;
};
