#include "mainwindow.h"
#include "document.h"
#include "renderwidget.h"
#include "meshtreewidget.h"
#include <QFileDialog>
#include <QMenuBar>
#include <QStatusBar>
#include <QDockWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("QMeshLab"));
    resize(800, 600);

    m_doc = new Document(this);

    m_renderWidget = new RenderWidget(this);
    setCentralWidget(m_renderWidget);

    m_meshTree = new MeshTreeWidget(m_doc, this);
    auto *dock = new QDockWidget(tr("Mesh List"), this);
    dock->setWidget(m_meshTree);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    connect(m_renderWidget, &RenderWidget::frameRendered, this, [this](float ms) {
        statusBar()->showMessage(QString("Frame: %1 ms").arg(ms, 0, 'f', 3));
    });

    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Open..."), QKeySequence::Open, this, &MainWindow::openFile);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close);
}

void MainWindow::openFile()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open Mesh"),
        QString(), tr("Mesh Files (*.ply *.obj *.stl *.off);;All Files (*)"));
    if (fileName.isEmpty())
        return;
    int err = m_doc->loadMesh(fileName);
    if (err != 0)
        statusBar()->showMessage(tr("Failed to load %1").arg(fileName), 3000);
    else
        statusBar()->showMessage(tr("Loaded %1").arg(fileName), 3000);
}
