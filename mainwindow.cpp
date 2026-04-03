#include "mainwindow.h"
#include "document.h"
#include "renderwidget.h"
#include "layerwidget.h"
#include <QFileDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QDockWidget>
#include <QBrush>
#include <QColor>
#include <QListWidget>
#include <QListWidgetItem>
#include <QStringList>

namespace {
void appendLogItem(QListWidget *logWidget, const QString &message, Document::LogSource source, bool replaceLast)
{
    QListWidgetItem *item = nullptr;
    if (replaceLast && logWidget->count() > 0) {
        item = logWidget->item(logWidget->count() - 1);
    } else {
        item = new QListWidgetItem(logWidget);
    }

    if (source == Document::LogSource::VCG) {
        item->setText(QObject::tr("[vcg] %1").arg(message));
        item->setForeground(QBrush(QColor(80, 110, 150)));
    } else {
        item->setText(QObject::tr("[app] %1").arg(message));
        item->setForeground(QBrush(QColor(50, 50, 50)));
    }

    logWidget->scrollToBottom();
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("QMeshLab"));
    resize(800, 600);

    m_doc = new Document(this);

    m_renderWidget = new RenderWidget(m_doc, this);
    setCentralWidget(m_renderWidget);

    m_layerWidget = new LayerWidget(m_doc, this);
    auto *dock = new QDockWidget(tr("Layers"), this);
    dock->setWidget(m_layerWidget);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    auto *logWidget = new QListWidget(this);
    auto *logDock = new QDockWidget(tr("Log"), this);
    logDock->setWidget(logWidget);
    addDockWidget(Qt::BottomDockWidgetArea, logDock);

    for (const auto &entry : m_doc->logMessages())
        appendLogItem(logWidget, entry.message, entry.source, false);

    connect(m_doc, &Document::logCleared, logWidget, &QListWidget::clear);
    connect(m_doc, &Document::logMessageAdded, logWidget,
        [logWidget](const QString &message, Document::LogSource source, bool replaceLast) {
            appendLogItem(logWidget, message, source, replaceLast);
    });

    connect(m_renderWidget, &RenderWidget::frameRendered, this, [this](float ms) {
        statusBar()->showMessage(QString("Frame: %1 ms").arg(ms, 0, 'f', 3));
    });

    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Open..."), QKeySequence::Open, this, &MainWindow::openFile);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About"), this, &MainWindow::showAbout);
    helpMenu->addAction(tr("Loaded &Plugins"), this, &MainWindow::showLoadedPlugins);
}

void MainWindow::openFile()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open Mesh"),
        QString(), m_doc->openDialogFilter());
    if (fileName.isEmpty())
        return;
    int err = m_doc->loadMesh(fileName);
    if (err != 0)
        statusBar()->showMessage(tr("Failed to load %1").arg(fileName), 3000);
    else
        statusBar()->showMessage(tr("Loaded %1").arg(fileName), 3000);
}

void MainWindow::showAbout()
{
    QMessageBox::about(this,
        tr("About QMeshLab"),
        tr("QMeshLab\n"
           "Minimal SDI mesh viewer based on Qt 6, QRhi and vcglib.\n\n"
           "Features:\n"
           "- Document + multiple views (3D, Layers, Log)\n"
           "- Plugin-based mesh loading\n"
           "- Structured logging with timings\n\n"
           "License: GNU GPL v3"));
}

void MainWindow::showLoadedPlugins()
{
    const QStringList plugins = m_doc->loadedPluginSummaries();
    const QString text = plugins.isEmpty()
        ? tr("No plugins loaded.")
        : tr("Plugins loaded at startup:\n\n%1").arg(plugins.join(QStringLiteral("\n")));

    QMessageBox::information(this, tr("Loaded Plugins"), text);
}
