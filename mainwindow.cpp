#include "mainwindow.h"
#include "document.h"
#include "renderwidget.h"
#include "layerwidget.h"
#include <QButtonGroup>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QGuiApplication>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QScreen>
#include <QDockWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QAction>
#include <QBrush>
#include <QColor>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QProcess>
#include <QProgressBar>
#include <QRadioButton>
#include <QSettings>
#include <QStringList>
#include <QTableWidget>
#include <QVector3D>
#include <QVBoxLayout>
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
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect avail = screen->availableGeometry();
        resize(avail.width() * 9 / 10, avail.height() * 9 / 10);
    } else {
        resize(800, 600);
    }

    m_doc = new Document(this);

    m_renderWidget = new RenderWidget(m_doc, this);
    setCentralWidget(m_renderWidget);

    m_loadProgressBar = new QProgressBar(this);
    m_loadProgressBar->setRange(0, 100);
    m_loadProgressBar->setTextVisible(false);
    m_loadProgressBar->setFixedWidth(160);
    m_loadProgressBar->setVisible(false);
    statusBar()->addPermanentWidget(m_loadProgressBar, 0);

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

    connect(m_doc, &Document::loadProgressStarted, this, [this](const QString &filePath) {
        if (!m_loadProgressBar)
            return;
        m_loadProgressBar->setValue(0);
        m_loadProgressBar->setVisible(true);
        m_loadProgressBar->setToolTip(filePath);
        statusBar()->showMessage(tr("Loading %1...").arg(QFileInfo(filePath).fileName()));
    });
    connect(m_doc, &Document::loadProgressUpdated, this, [this](int percent, const QString &message) {
        if (!m_loadProgressBar)
            return;
        m_loadProgressBar->setVisible(true);
        m_loadProgressBar->setValue(std::clamp(percent, 0, 100));
        if (!message.isEmpty())
            statusBar()->showMessage(tr("Loading: %1% - %2").arg(percent).arg(message));
        else
            statusBar()->showMessage(tr("Loading: %1%").arg(percent));
    });
    connect(m_doc, &Document::loadProgressFinished, this, [this](bool success, const QString &message) {
        if (m_loadProgressBar)
            m_loadProgressBar->setVisible(false);
        statusBar()->showMessage(message.isEmpty()
                ? (success ? tr("Loading completed") : tr("Loading failed"))
                : message,
            success ? 2500 : 4000);
    });

    connect(m_renderWidget, &RenderWidget::frameRendered, this,
            [this](float cpuMs, float gpuMs, bool gpuTimingSupported, bool gpuSampleValid) {
        updateFrameTimeStats(cpuMs, gpuMs, gpuTimingSupported, gpuSampleValid);
    });
    connect(m_renderWidget, &RenderWidget::trackballCenterPicked, this,
            [this](const QVector3D &worldPos) {
        const QString msg = tr("Trackball center: (%1, %2, %3)")
            .arg(worldPos.x(), 0, 'f', 6)
            .arg(worldPos.y(), 0, 'f', 6)
            .arg(worldPos.z(), 0, 'f', 6);
        statusBar()->showMessage(msg, 3500);
        m_doc->writeLog(msg, Document::LogSource::Application);
    });

    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&New"), QKeySequence::New, this, &MainWindow::newDocument);
    fileMenu->addAction(
        tr("New &Instance"),
        QKeySequence(QStringLiteral("Ctrl+Shift+N")),
        this,
        &MainWindow::newInstance);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Open..."), QKeySequence::Open, this, &MainWindow::openFile);
    m_openLastAction = fileMenu->addAction(tr("Open &Last Mesh"), this, &MainWindow::openLastMesh);
    m_openLastAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+L")));
    m_recentMenu = fileMenu->addMenu(tr("Open &Recent"));
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(tr("Reset Camera"), this, &MainWindow::resetCamera);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About"), this, &MainWindow::showAbout);
    helpMenu->addAction(tr("Import &Plugins..."), this, &MainWindow::showImportPlugins);

    QSettings settings;
    m_recentMeshes = settings.value(QStringLiteral("recentMeshes")).toStringList();
    sanitizeRecentMeshes();
    refreshRecentMeshesMenu();
}

void MainWindow::newDocument()
{
    // Remove from back to keep indices valid while emitting meshRemoved/currentMeshChanged.
    while (m_doc->meshCount() > 0)
        m_doc->removeMesh(m_doc->meshCount() - 1);
    m_doc->clearLog();
    statusBar()->showMessage(tr("New document"), 2000);
}

void MainWindow::newInstance()
{
    const QString program = QCoreApplication::applicationFilePath();
    const bool started = QProcess::startDetached(program, QStringList());
    statusBar()->showMessage(
        started ? tr("Started new QMeshLab instance")
                : tr("Failed to start a new QMeshLab instance"),
        started ? 2000 : 4000);
}

void MainWindow::openFile()
{
    const QStringList fileNames = QFileDialog::getOpenFileNames(
        this,
        tr("Open Mesh"),
        QString(),
        m_doc->openDialogFilter());
    if (fileNames.isEmpty())
        return;

    int loadedCount = 0;
    int failedCount = 0;
    for (const QString &fileName : fileNames) {
        if (loadMeshFromPath(fileName))
            ++loadedCount;
        else
            ++failedCount;
    }

    if (fileNames.size() > 1) {
        statusBar()->showMessage(
            tr("Open complete: %1 loaded, %2 failed")
                .arg(loadedCount)
                .arg(failedCount),
            3500);
    }
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

void MainWindow::showImportPlugins()
{
    const std::vector<Document::ImportPluginInfo> plugins = m_doc->importPluginInfos();
    const QStringList extensions = m_doc->importSupportedExtensions();
    if (plugins.empty() || extensions.isEmpty()) {
        QMessageBox::information(this, tr("Import Plugins"), tr("No import plugins are available."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Import Plugins"));

    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(
        tr("Choose the preferred plugin for each file type/extension."),
        &dialog));

    auto *table = new QTableWidget(static_cast<int>(plugins.size()), extensions.size(), &dialog);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setFocusPolicy(Qt::NoFocus);
    table->setHorizontalHeaderLabels(extensions);
    table->verticalHeader()->setVisible(true);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    QStringList rowLabels;
    rowLabels.reserve(static_cast<int>(plugins.size()));
    for (const auto &plugin : plugins)
        rowLabels << plugin.name;
    table->setVerticalHeaderLabels(rowLabels);

    std::vector<QButtonGroup *> groups(static_cast<size_t>(extensions.size()), nullptr);
    for (int col = 0; col < extensions.size(); ++col) {
        auto *group = new QButtonGroup(table);
        group->setExclusive(true);
        groups[static_cast<size_t>(col)] = group;
    }

    for (int col = 0; col < extensions.size(); ++col) {
        const QString extension = extensions[col];
        const QString preferredPluginId = m_doc->preferredImportPluginForExtension(extension);
        int preferredRow = -1;
        int firstSupportedRow = -1;

        for (int row = 0; row < static_cast<int>(plugins.size()); ++row) {
            const bool supports = plugins[static_cast<size_t>(row)].extensions.contains(extension);
            if (!supports)
                continue;

            if (firstSupportedRow < 0)
                firstSupportedRow = row;
            if (plugins[static_cast<size_t>(row)].id == preferredPluginId)
                preferredRow = row;

            auto *radio = new QRadioButton(table);
            radio->setToolTip(
                tr("Use \"%1\" for .%2 files")
                    .arg(plugins[static_cast<size_t>(row)].name, extension));
            groups[static_cast<size_t>(col)]->addButton(radio, row);

            auto *cell = new QWidget(table);
            auto *cellLayout = new QHBoxLayout(cell);
            cellLayout->setContentsMargins(0, 0, 0, 0);
            cellLayout->addWidget(radio, 0, Qt::AlignCenter);
            table->setCellWidget(row, col, cell);
        }

        const int rowToSelect = (preferredRow >= 0) ? preferredRow : firstSupportedRow;
        if (rowToSelect >= 0) {
            if (QAbstractButton *button = groups[static_cast<size_t>(col)]->button(rowToSelect))
                button->setChecked(true);
        }
    }

    table->resizeColumnsToContents();
    table->resizeRowsToContents();

    const int frame = table->frameWidth() * 2;
    const int verticalHeaderWidth = table->verticalHeader()->isVisible() ? table->verticalHeader()->width() : 0;
    const int horizontalHeaderHeight =
        table->horizontalHeader()->isVisible() ? table->horizontalHeader()->height() : 0;
    const int contentWidth = table->horizontalHeader()->length();
    const int contentHeight = table->verticalHeader()->length();
    const int slack = 8; // guard against style-dependent underestimation
    const int tableWidth = frame + verticalHeaderWidth + contentWidth + slack;
    const int tableHeight = frame + horizontalHeaderHeight + contentHeight + slack;

    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    table->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    table->setMinimumSize(tableWidth, tableHeight);
    table->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);

    layout->addWidget(table, 1);

    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.adjustSize();

    if (QScreen *screen = dialog.screen()) {
        const QSize maxDialogSize(
            screen->availableGeometry().width() * 9 / 10,
            screen->availableGeometry().height() * 9 / 10);

        const QSize preferredDialogSize = dialog.sizeHint();
        if (preferredDialogSize.width() > maxDialogSize.width()
            || preferredDialogSize.height() > maxDialogSize.height()) {
            const int chromeWidth = preferredDialogSize.width() - tableWidth;
            const int chromeHeight = preferredDialogSize.height() - tableHeight;
            const QSize maxTableSize(
                std::max(320, maxDialogSize.width() - std::max(0, chromeWidth)),
                std::max(220, maxDialogSize.height() - std::max(0, chromeHeight)));
            table->setMinimumSize(0, 0);
            table->setMaximumSize(maxTableSize);
            dialog.adjustSize();
        }

        dialog.resize(dialog.sizeHint().boundedTo(maxDialogSize));
    }

    if (dialog.exec() != QDialog::Accepted)
        return;

    for (int col = 0; col < extensions.size(); ++col) {
        QButtonGroup *group = groups[static_cast<size_t>(col)];
        if (!group)
            continue;
        const int selectedRow = group->checkedId();
        if (selectedRow < 0 || selectedRow >= static_cast<int>(plugins.size()))
            continue;

        m_doc->setPreferredImportPluginForExtension(
            extensions[col],
            plugins[static_cast<size_t>(selectedRow)].id);
    }

    statusBar()->showMessage(tr("Import plugin preferences updated"), 2000);
}

void MainWindow::resetCamera()
{
    m_renderWidget->resetCameraToScene();
    statusBar()->showMessage(tr("Camera reset"), 1500);
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

    if (cleaned != m_recentMeshes)
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
