#include "mainwindow.h"
#include "renderwidget.h"
#include <QFileDialog>
#include <QMenuBar>
#include <QStatusBar>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("QMeshLab"));
    resize(800, 600);

    auto *renderWidget = new RenderWidget(this);
    setCentralWidget(renderWidget);

    connect(renderWidget, &RenderWidget::frameRendered, this, [this](float ms) {
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
    statusBar()->showMessage(tr("Opened %1").arg(fileName), 3000);
}
