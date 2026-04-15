#pragma once

#include <QMainWindow>
#include <QImage>
#include <QStringList>
#include <QList>
#include <array>
#include <deque>

class Document;
class RenderWidget;
class LayerWidget;
class QMenu;
class QAction;
class QLabel;
class QProgressBar;
class QSplitter;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void newDocument();
    void newInstance();
    void openFile();
    void undo();
    void redo();
    void saveCurrentMesh();
    void saveSnapshotPng();
    void openLastMesh();
    void openRecentMesh();
    void showAbout();
    void showImportPlugins();
    void resetCamera();
    void copyCameraState();
    void pasteCameraState();
    void setCurrentViewSceneMode();
    void setCurrentViewParametrizationMode();
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
    void addRecentMesh(const QString &filePath);
    void sanitizeRecentMeshes();
    void refreshRecentMeshesMenu();
    void openRecentMeshByIndex(int index);
    void updateFrameTimeStats(float cpuMs, float gpuMs, bool gpuTimingSupported, bool gpuSampleValid);
    QImage renderSnapshotOffscreen(
        RenderWidget *sourceView,
        const QSize &pixelSize,
        QString *errorMessage = nullptr);

    Document *m_doc;
    QSplitter *m_viewSplitter = nullptr;
    QList<RenderWidget *> m_renderWidgets;
    RenderWidget *m_currentRenderWidget = nullptr;
    bool m_syncingVisibilityProxy = false;
    LayerWidget *m_layerWidget;
    QMenu *m_recentMenu = nullptr;
    QAction *m_openLastAction = nullptr;
    std::array<QAction *, 8> m_recentActions = {};
    QStringList m_recentMeshes;
    QProgressBar *m_loadProgressBar = nullptr;
    QLabel *m_frameStatsLabel = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    std::deque<float> m_lastCpuFrameTimes;
    std::deque<float> m_lastGpuFrameTimes;
};
