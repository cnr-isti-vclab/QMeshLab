#pragma once

#include <QMainWindow>

class Document;
class RenderWidget;
class MeshTreeWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void openFile();

private:
    Document *m_doc;
    RenderWidget *m_renderWidget;
    MeshTreeWidget *m_meshTree;
};
