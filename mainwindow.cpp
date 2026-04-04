#include "mainwindow.h"
#include "document.h"
#include "renderwidget.h"
#include "layerwidget.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QDockWidget>
#include <QActionGroup>
#include <QAction>
#include <QBrush>
#include <QColor>
#include <QFontDatabase>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QSettings>
#include <QStringList>
#include <algorithm>
#include <array>
#include <numeric>

namespace {
constexpr std::size_t kFrameStatsWindow = 100;

QString normalizeRecentPath(const QString &path)
{
    const QFileInfo fi(path);
    const QString canonical = fi.canonicalFilePath();
    if (!canonical.isEmpty())
        return canonical;
    return fi.absoluteFilePath();
}

bool sameRecentPath(const QString &a, const QString &b)
{
#if defined(Q_OS_WIN) || defined(Q_OS_DARWIN)
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}

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

    m_frameStatsLabel = new QLabel(this);
    QFont statsFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    statsFont.setStyleHint(QFont::TypeWriter);
    m_frameStatsLabel->setFont(statsFont);
    statusBar()->addPermanentWidget(m_frameStatsLabel, 1);

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

    connect(m_renderWidget, &RenderWidget::frameRendered, this,
            [this](float cpuMs, float gpuMs, bool gpuTimingSupported, bool gpuSampleValid) {
        updateFrameTimeStats(cpuMs, gpuMs, gpuTimingSupported, gpuSampleValid);
    });

    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Open..."), QKeySequence::Open, this, &MainWindow::openFile);
    m_openLastAction = fileMenu->addAction(tr("Open &Last Mesh"), this, &MainWindow::openLastMesh);
    m_openLastAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+L")));
    m_recentMenu = fileMenu->addMenu(tr("Open &Recent"));
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    auto *modeGroup = new QActionGroup(this);
    modeGroup->setExclusive(true);

    QAction *smoothAction = viewMenu->addAction(tr("Smooth Shading"), this, &MainWindow::setSmoothShading);
    smoothAction->setCheckable(true);
    QAction *flatAction = viewMenu->addAction(tr("Flat Shading"), this, &MainWindow::setFlatShading);
    flatAction->setCheckable(true);
    QAction *wireframeAction = viewMenu->addAction(tr("Wireframe"), this, &MainWindow::setWireframeShading);
    wireframeAction->setCheckable(true);
    modeGroup->addAction(smoothAction);
    modeGroup->addAction(flatAction);
    modeGroup->addAction(wireframeAction);
    smoothAction->setChecked(true);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About"), this, &MainWindow::showAbout);
    helpMenu->addAction(tr("Loaded &Plugins"), this, &MainWindow::showLoadedPlugins);

    QSettings settings;
    m_recentMeshes = settings.value(QStringLiteral("recentMeshes")).toStringList();
    sanitizeRecentMeshes();
    refreshRecentMeshesMenu();
}

void MainWindow::openFile()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open Mesh"),
        QString(), m_doc->openDialogFilter());
    if (fileName.isEmpty())
        return;
    loadMeshFromPath(fileName);
}

void MainWindow::openLastMesh()
{
    sanitizeRecentMeshes();
    refreshRecentMeshesMenu();

    if (m_recentMeshes.isEmpty()) {
        statusBar()->showMessage(tr("No recent meshes"), 2000);
        return;
    }

    loadMeshFromPath(m_recentMeshes.constFirst());
}

void MainWindow::openRecentMesh()
{
    auto *action = qobject_cast<QAction *>(sender());
    if (!action)
        return;

    const QString filePath = action->data().toString();
    if (filePath.isEmpty())
        return;

    loadMeshFromPath(filePath);
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

void MainWindow::setSmoothShading()
{
    m_renderWidget->setShadingMode(RenderWidget::ShadingMode::Smooth);
}

void MainWindow::setFlatShading()
{
    m_renderWidget->setShadingMode(RenderWidget::ShadingMode::Flat);
}

void MainWindow::setWireframeShading()
{
    m_renderWidget->setShadingMode(RenderWidget::ShadingMode::Wireframe);
}

bool MainWindow::loadMeshFromPath(const QString &filePath)
{
    const int err = m_doc->loadMesh(filePath);
    if (err != 0) {
        statusBar()->showMessage(tr("Failed to load %1").arg(filePath), 3000);
        return false;
    }

    statusBar()->showMessage(tr("Loaded %1").arg(filePath), 3000);
    addRecentMesh(filePath);
    return true;
}

void MainWindow::addRecentMesh(const QString &filePath)
{
    const QString normalizedPath = normalizeRecentPath(filePath);
    if (!normalizedPath.isEmpty())
        m_recentMeshes.prepend(normalizedPath);

    sanitizeRecentMeshes();
    refreshRecentMeshesMenu();
}

void MainWindow::sanitizeRecentMeshes()
{
    QStringList cleaned;
    cleaned.reserve(4);
    for (const QString &path : std::as_const(m_recentMeshes)) {
        const QString normalizedPath = normalizeRecentPath(path);
        if (normalizedPath.isEmpty())
            continue;
        if (!QFileInfo::exists(normalizedPath))
            continue;

        bool alreadyInList = false;
        for (const QString &existingPath : std::as_const(cleaned)) {
            if (sameRecentPath(existingPath, normalizedPath)) {
                alreadyInList = true;
                break;
            }
        }
        if (alreadyInList)
            continue;

        cleaned.append(normalizedPath);
        if (cleaned.size() >= 4)
            break;
    }

    if (cleaned == m_recentMeshes)
        return;

    m_recentMeshes = cleaned;
    QSettings settings;
    settings.setValue(QStringLiteral("recentMeshes"), m_recentMeshes);
}

void MainWindow::refreshRecentMeshesMenu()
{
    m_recentMenu->clear();

    const std::array<QString, 4> shortcuts = {
        QStringLiteral("Ctrl+1"),
        QStringLiteral("Ctrl+2"),
        QStringLiteral("Ctrl+3"),
        QStringLiteral("Ctrl+4")
    };

    for (int i = 0; i < m_recentMeshes.size() && i < 4; ++i) {
        const QString &path = m_recentMeshes[i];
        if (!m_recentActions[i]) {
            m_recentActions[i] = new QAction(this);
            connect(m_recentActions[i], &QAction::triggered, this, [this, i]() {
                openRecentMeshByIndex(i);
            });
        }
        m_recentActions[i]->setText(QFileInfo(path).fileName());
        m_recentActions[i]->setShortcut(QKeySequence(shortcuts[i]));
        m_recentActions[i]->setToolTip(path);
        m_recentMenu->addAction(m_recentActions[i]);
    }

    m_recentMenu->setEnabled(!m_recentMeshes.isEmpty());
    m_openLastAction->setEnabled(!m_recentMeshes.isEmpty());
}

void MainWindow::openRecentMeshByIndex(int index)
{
    sanitizeRecentMeshes();
    refreshRecentMeshesMenu();

    if (index >= 0 && index < m_recentMeshes.size()) {
        loadMeshFromPath(m_recentMeshes[index]);
    }
}

void MainWindow::updateFrameTimeStats(float cpuMs, float gpuMs, bool gpuTimingSupported, bool gpuSampleValid)
{
    m_lastCpuFrameTimes.push_back(cpuMs);
    if (m_lastCpuFrameTimes.size() > kFrameStatsWindow)
        m_lastCpuFrameTimes.pop_front();

    const auto [cpuMinIt, cpuMaxIt] = std::minmax_element(m_lastCpuFrameTimes.begin(), m_lastCpuFrameTimes.end());
    const float cpuSum = std::accumulate(m_lastCpuFrameTimes.begin(), m_lastCpuFrameTimes.end(), 0.0f);
    const float cpuAvg = cpuSum / static_cast<float>(m_lastCpuFrameTimes.size());
    const float cpuMinMs = (cpuMinIt != m_lastCpuFrameTimes.end()) ? *cpuMinIt : cpuMs;
    const float cpuMaxMs = (cpuMaxIt != m_lastCpuFrameTimes.end()) ? *cpuMaxIt : cpuMs;

    QString gpuText;
    if (!gpuTimingSupported) {
        m_lastGpuFrameTimes.clear();
        gpuText = tr("GPU: n/a");
    } else {
        if (gpuSampleValid) {
            m_lastGpuFrameTimes.push_back(gpuMs);
            if (m_lastGpuFrameTimes.size() > kFrameStatsWindow)
                m_lastGpuFrameTimes.pop_front();
        }

        if (m_lastGpuFrameTimes.empty()) {
            gpuText = tr("GPU: waiting...");
        } else {
            const auto [gpuMinIt, gpuMaxIt] =
                std::minmax_element(m_lastGpuFrameTimes.begin(), m_lastGpuFrameTimes.end());
            const float gpuSum = std::accumulate(m_lastGpuFrameTimes.begin(), m_lastGpuFrameTimes.end(), 0.0f);
            const float gpuAvg = gpuSum / static_cast<float>(m_lastGpuFrameTimes.size());
            const float gpuMinMs = (gpuMinIt != m_lastGpuFrameTimes.end()) ? *gpuMinIt : m_lastGpuFrameTimes.back();
            const float gpuMaxMs = (gpuMaxIt != m_lastGpuFrameTimes.end()) ? *gpuMaxIt : m_lastGpuFrameTimes.back();
            const float gpuCurrentMs = gpuSampleValid ? gpuMs : m_lastGpuFrameTimes.back();

            gpuText = tr("GPU: %1 ms | Last %2 avg %3 ms (min %4, max %5)")
                .arg(gpuCurrentMs, 0, 'f', 3)
                .arg(m_lastGpuFrameTimes.size())
                .arg(gpuAvg, 0, 'f', 3)
                .arg(gpuMinMs, 0, 'f', 3)
                .arg(gpuMaxMs, 0, 'f', 3);
        }
    }

    const QString statsText = tr("CPU: %1 ms | Last %2 avg %3 ms (min %4, max %5) | %6")
        .arg(cpuMs, 0, 'f', 3)
        .arg(m_lastCpuFrameTimes.size())
        .arg(cpuAvg, 0, 'f', 3)
        .arg(cpuMinMs, 0, 'f', 3)
        .arg(cpuMaxMs, 0, 'f', 3)
        .arg(gpuText);

    if (m_frameStatsLabel)
        m_frameStatsLabel->setText(statsText);
    else
        statusBar()->showMessage(statsText);
}
