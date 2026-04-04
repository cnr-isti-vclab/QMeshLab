#pragma once

#include <QMainWindow>
#include <QStringList>
#include <array>

class Document;
class RenderWidget;
class LayerWidget;
class QMenu;
class QAction;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void openFile();
    void openLastMesh();
    void openRecentMesh();
    void showAbout();
    void showLoadedPlugins();
    void setSmoothShading();
    void setFlatShading();
    void setWireframeShading();

private:
    bool loadMeshFromPath(const QString &filePath);
    void addRecentMesh(const QString &filePath);
    void refreshRecentMeshesMenu();
    void openRecentMeshByIndex(int index);

    Document *m_doc;
    RenderWidget *m_renderWidget;
    LayerWidget *m_layerWidget;
    QMenu *m_recentMenu = nullptr;
    QAction *m_openLastAction = nullptr;
    std::array<QAction *, 4> m_recentActions = {};
    QStringList m_recentMeshes;
};
