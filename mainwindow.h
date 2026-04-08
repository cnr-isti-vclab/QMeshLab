#pragma once

#include <QMainWindow>
#include <QStringList>
#include <array>
#include <deque>

class Document;
class RenderWidget;
class LayerWidget;
class QMenu;
class QAction;
class QLabel;
class QProgressBar;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void newDocument();
    void newInstance();
    void openFile();
    void openLastMesh();
    void openRecentMesh();
    void showAbout();
    void showImportPlugins();
    void resetCamera();

private:
    bool loadMeshFromPath(const QString &filePath);
    void addRecentMesh(const QString &filePath);
    void sanitizeRecentMeshes();
    void refreshRecentMeshesMenu();
    void openRecentMeshByIndex(int index);
    void updateFrameTimeStats(float cpuMs, float gpuMs, bool gpuTimingSupported, bool gpuSampleValid);

    Document *m_doc;
    RenderWidget *m_renderWidget;
    LayerWidget *m_layerWidget;
    QMenu *m_recentMenu = nullptr;
    QAction *m_openLastAction = nullptr;
    std::array<QAction *, 4> m_recentActions = {};
    QStringList m_recentMeshes;
    QProgressBar *m_loadProgressBar = nullptr;
    QLabel *m_frameStatsLabel = nullptr;
    std::deque<float> m_lastCpuFrameTimes;
    std::deque<float> m_lastGpuFrameTimes;
};
