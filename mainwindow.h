#pragma once

#include <QMainWindow>
#include <QStringList>

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

private:
    bool loadMeshFromPath(const QString &filePath);
    void addRecentMesh(const QString &filePath);
    void refreshRecentMeshesMenu();

    Document *m_doc;
    RenderWidget *m_renderWidget;
    LayerWidget *m_layerWidget;
    QMenu *m_recentMenu = nullptr;
    QAction *m_openLastAction = nullptr;
    QStringList m_recentMeshes;
};
