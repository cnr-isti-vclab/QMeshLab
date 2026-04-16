#include "mainwindow.h"
#include "document.h"
#include "meshfilterpanel.h"
#include "meshsaveoptionsdialog.h"
#include "renderwidget.h"
#include "layerwidget.h"
#include <wrap/io_trimesh/io_mask.h>
#include <QButtonGroup>
#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QImageWriter>
#include <QGuiApplication>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QScreen>
#include <QSplitter>
#include <QDockWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QAction>
#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QProcess>
#include <QProgressBar>
#include <QRadioButton>
#include <QSettings>
#include <QSet>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget>
#include <QTimer>
#include <QVector3D>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace {
constexpr std::size_t kFrameStatsWindow = 100;
constexpr int kRecentMeshesLimit = 8;

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

QString fileExtensionLower(const QString &path)
{
    return QFileInfo(path).suffix().toLower();
}

QString extensionFromNameFilter(const QString &nameFilter)
{
    const int wildcardPos = nameFilter.indexOf(QStringLiteral("*."));
    if (wildcardPos < 0)
        return QString();
    int start = wildcardPos + 2;
    int end = start;
    while (end < nameFilter.size()) {
        const QChar c = nameFilter[end];
        if (!c.isLetterOrNumber() && c != QLatin1Char('_') && c != QLatin1Char('-'))
            break;
        ++end;
    }
    if (end <= start)
        return QString();
    return nameFilter.mid(start, end - start).toLower();
}

QString appendSaveExtensionIfMissing(const QString &path, const QString &selectedFilter)
{
    if (!QFileInfo(path).suffix().isEmpty())
        return path;

    QString ext = extensionFromNameFilter(selectedFilter);
    if (ext.isEmpty())
        ext = QStringLiteral("ply");
    return QStringLiteral("%1.%2").arg(path, ext);
}

bool saveFormatSupportsBinary(const QString &extension)
{
    return extension == QLatin1String("ply") || extension == QLatin1String("stl");
}

bool saveFormatSupportsEmbeddedTextures(const QString &extension)
{
    return extension == QLatin1String("gltf") || extension == QLatin1String("glb");
}

bool saveFormatSupportsDracoCompression(const QString &extension)
{
    return extension == QLatin1String("gltf") || extension == QLatin1String("glb");
}

int availableSaveMaskForMesh(const Document::MeshEntry &entry)
{
    int mask = entry.ioMask;
    mask |= vcg::tri::io::Mask::IOM_VERTCOORD;
    if (entry.mesh.FN() > 0)
        mask |= vcg::tri::io::Mask::IOM_FACEINDEX;
    if (entry.mesh.EN() > 0)
        mask |= vcg::tri::io::Mask::IOM_EDGEINDEX;
    return mask;
}

int requiredSaveMaskForMesh(const Document::MeshEntry &entry, int capabilityMask)
{
    int requiredMask = 0;
    if ((capabilityMask & vcg::tri::io::Mask::IOM_VERTCOORD) != 0 && entry.mesh.VN() > 0)
        requiredMask |= vcg::tri::io::Mask::IOM_VERTCOORD;
    if ((capabilityMask & vcg::tri::io::Mask::IOM_FACEINDEX) != 0 && entry.mesh.FN() > 0)
        requiredMask |= vcg::tri::io::Mask::IOM_FACEINDEX;
    if ((capabilityMask & vcg::tri::io::Mask::IOM_EDGEINDEX) != 0 && entry.mesh.EN() > 0)
        requiredMask |= vcg::tri::io::Mask::IOM_EDGEINDEX;
    return requiredMask;
}

int defaultSaveMaskForMesh(const Document::MeshEntry &entry, int capabilityMask)
{
    const int availableMask = availableSaveMaskForMesh(entry);
    int mask = availableMask & capabilityMask;
    mask |= requiredSaveMaskForMesh(entry, capabilityMask);
    // Prefer wedge attributes over per-vertex ones when both are available.
    if ((mask & vcg::tri::io::Mask::IOM_WEDGTEXCOORD) != 0)
        mask &= ~vcg::tri::io::Mask::IOM_VERTTEXCOORD;
    if ((mask & vcg::tri::io::Mask::IOM_WEDGNORMAL) != 0)
        mask &= ~vcg::tri::io::Mask::IOM_VERTNORMAL;
    return mask;
}

QString filterInputDomainLabel(MeshFilterInputDomain domain)
{
    switch (domain) {
    case MeshFilterInputDomain::None:
        return QObject::tr("None");
    case MeshFilterInputDomain::SingleMesh:
        return QObject::tr("Single Mesh");
    case MeshFilterInputDomain::WholeDocument:
        return QObject::tr("Whole Document");
    }
    return QObject::tr("Unknown");
}

QString filterOutputDomainLabel(MeshFilterOutputDomain domain)
{
    switch (domain) {
    case MeshFilterOutputDomain::Information:
        return QObject::tr("Information");
    case MeshFilterOutputDomain::ModifyCurrentMesh:
        return QObject::tr("Modify Current Mesh");
    case MeshFilterOutputDomain::NewMeshes:
        return QObject::tr("Create New Meshes");
    }
    return QObject::tr("Unknown");
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

    m_viewSplitter = new QSplitter(Qt::Horizontal, this);
    m_viewSplitter->setChildrenCollapsible(false);
    setCentralWidget(m_viewSplitter);

    RenderWidget *initialView = createRenderWidget(m_viewSplitter);
    setCurrentRenderWidget(initialView);

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
    m_layerDock = new QDockWidget(tr("Layers"), this);
    m_layerDock->setWidget(m_layerWidget);
    m_layerDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    addDockWidget(Qt::RightDockWidgetArea, m_layerDock);

    m_filterPanel = new MeshFilterPanel(m_doc, this);
    m_filterDock = new QDockWidget(tr("Filters"), this);
    m_filterDock->setWidget(m_filterPanel);
    m_filterDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    addDockWidget(Qt::RightDockWidgetArea, m_filterDock);
    splitDockWidget(m_layerDock, m_filterDock, Qt::Vertical);
    const int rightColumnWidth = std::max(260, width() / 5);
    m_layerDock->setMinimumWidth(rightColumnWidth);
    m_filterDock->setMinimumWidth(rightColumnWidth);
    resizeDocks({ m_layerDock }, { rightColumnWidth }, Qt::Horizontal);
    resizeDocks({ m_layerDock, m_filterDock }, { 1, 1 }, Qt::Vertical);

    auto *logWidget = new QListWidget(this);
    logWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    logWidget->setContextMenuPolicy(Qt::ActionsContextMenu);

    auto copyLogSelectionToClipboard = [this, logWidget]() {
        QStringList lines;
        const QList<QListWidgetItem *> selected = logWidget->selectedItems();
        lines.reserve(selected.size());
        for (QListWidgetItem *item : selected) {
            if (item)
                lines.push_back(item->text());
        }
        if (lines.isEmpty()) {
            if (QListWidgetItem *current = logWidget->currentItem())
                lines.push_back(current->text());
        }
        if (lines.isEmpty())
            return;

        const QString text = lines.join(QLatin1Char('\n'));
        if (QClipboard *clipboard = QGuiApplication::clipboard()) {
            clipboard->setText(text, QClipboard::Clipboard);
            if (clipboard->supportsSelection())
                clipboard->setText(text, QClipboard::Selection);
        }
        statusBar()->showMessage(tr("Copied %1 log line(s)").arg(lines.size()), 1500);
    };

    auto *copyLogAction = new QAction(tr("Copy"), logWidget);
    copyLogAction->setShortcut(QKeySequence::Copy);
    copyLogAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(copyLogAction, &QAction::triggered, this, copyLogSelectionToClipboard);
    logWidget->addAction(copyLogAction);

    auto *selectAllLogAction = new QAction(tr("Select All"), logWidget);
    selectAllLogAction->setShortcut(QKeySequence::SelectAll);
    selectAllLogAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(selectAllLogAction, &QAction::triggered, logWidget, &QListWidget::selectAll);
    logWidget->addAction(selectAllLogAction);

    auto *copyAllLogAction = new QAction(tr("Copy All"), logWidget);
    connect(copyAllLogAction, &QAction::triggered, this, [this, logWidget]() {
        logWidget->selectAll();
        QStringList lines;
        lines.reserve(logWidget->count());
        for (int i = 0; i < logWidget->count(); ++i) {
            if (QListWidgetItem *item = logWidget->item(i))
                lines.push_back(item->text());
        }
        if (QClipboard *clipboard = QGuiApplication::clipboard()) {
            const QString text = lines.join(QLatin1Char('\n'));
            clipboard->setText(text, QClipboard::Clipboard);
            if (clipboard->supportsSelection())
                clipboard->setText(text, QClipboard::Selection);
        }
        statusBar()->showMessage(tr("Copied all log lines"), 1500);
    });
    logWidget->addAction(copyAllLogAction);

    auto *logDock = new QDockWidget(tr("Log"), this);
    logDock->setWidget(logWidget);
    addDockWidget(Qt::BottomDockWidgetArea, logDock);
    // Keep the right column (Layers + Filters) spanning full height.
    // This ensures the bottom Log dock does not extend under the right column.
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

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
    connect(m_doc, &Document::meshVisibilityChanged, this, [this](int index, bool visible) {
        if (m_syncingVisibilityProxy)
            return;
        RenderWidget *view = currentRenderWidget();
        if (!view)
            return;
        view->setMeshVisible(index, visible);
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
    fileMenu->addAction(tr("Reload &Current Mesh"), this, &MainWindow::reloadCurrentMesh);
    fileMenu->addAction(tr("Reload &All Meshes"), this, &MainWindow::reloadAllMeshes);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Save Mesh..."), QKeySequence::Save, this, &MainWindow::saveCurrentMesh);
    fileMenu->addAction(
        tr("S&napshot PNG..."),
        QKeySequence(QStringLiteral("Ctrl+Shift+S")),
        this,
        &MainWindow::saveSnapshotPng);
    m_openLastAction = fileMenu->addAction(tr("Open &Last Mesh"), this, &MainWindow::openLastMesh);
    m_openLastAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+L")));
    m_recentMenu = fileMenu->addMenu(tr("Open &Recent"));
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    m_undoAction = editMenu->addAction(tr("&Undo"), this, &MainWindow::undo);
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_redoAction = editMenu->addAction(tr("&Redo"), this, &MainWindow::redo);
    m_redoAction->setShortcut(QKeySequence::Redo);
    connect(m_doc, &Document::undoRedoStateChanged, this,
            [this](bool canUndo, bool canRedo, const QString &undoText, const QString &redoText) {
        if (m_undoAction) {
            m_undoAction->setEnabled(canUndo);
            m_undoAction->setText(
                canUndo ? tr("&Undo %1").arg(undoText) : tr("&Undo"));
        }
        if (m_redoAction) {
            m_redoAction->setEnabled(canRedo);
            m_redoAction->setText(
                canRedo ? tr("&Redo %1").arg(redoText) : tr("&Redo"));
        }
    });
    if (m_undoAction) {
        m_undoAction->setEnabled(m_doc->canUndo());
        m_undoAction->setText(
            m_doc->canUndo() ? tr("&Undo %1").arg(m_doc->undoText()) : tr("&Undo"));
    }
    if (m_redoAction) {
        m_redoAction->setEnabled(m_doc->canRedo());
        m_redoAction->setText(
            m_doc->canRedo() ? tr("&Redo %1").arg(m_doc->redoText()) : tr("&Redo"));
    }

    m_filtersMenu = menuBar()->addMenu(tr("&Filters"));
    refreshFiltersMenu();
    if (m_filterPanel) {
        connect(m_filterPanel, &MeshFilterPanel::runRequested, this,
                [this](const QString &filterKey, const MeshFilterParameterValues &params, const QString &label) {
            executeFilter(filterKey, label, params);
        });
    }
    connect(m_doc, &Document::meshAdded, this, [this](int) {
        refreshFiltersMenu();
        if (m_filterPanel)
            m_filterPanel->reloadFilters();
    });
    connect(m_doc, &Document::meshRemoved, this, [this](int) {
        refreshFiltersMenu();
        if (m_filterPanel)
            m_filterPanel->reloadFilters();
    });
    connect(m_doc, &Document::currentMeshChanged, this, [this](int) {
        refreshFiltersMenu();
        if (m_filterPanel)
            m_filterPanel->reloadFilters();
    });
    connect(m_doc, &Document::meshDataChanged, this, [this](int) {
        refreshFiltersMenu();
        if (m_filterPanel)
            m_filterPanel->reloadFilters();
    });

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(tr("3D Scene Mode"), this, &MainWindow::setCurrentViewSceneMode);
    viewMenu->addAction(
        tr("Parametrization (UV) Mode"),
        this,
        &MainWindow::setCurrentViewParametrizationMode);
    viewMenu->addSeparator();
    viewMenu->addAction(tr("Split Horizontally"), this, &MainWindow::splitViewHorizontally);
    viewMenu->addAction(tr("Split Vertically"), this, &MainWindow::splitViewVertically);
    viewMenu->addSeparator();
    viewMenu->addAction(tr("Reset Camera"), this, &MainWindow::resetCamera);
    viewMenu->addSeparator();
    viewMenu->addAction(
        tr("Copy Camera/Trackball JSON"),
        QKeySequence::Copy,
        this,
        &MainWindow::copyCameraState);
    viewMenu->addAction(
        tr("Paste Camera/Trackball JSON"),
        QKeySequence::Paste,
        this,
        &MainWindow::pasteCameraState);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About"), this, &MainWindow::showAbout);
    helpMenu->addAction(tr("I/O &Plugins..."), this, &MainWindow::showImportPlugins);
    helpMenu->addAction(tr("&Filter Plugins..."), this, &MainWindow::showFilterPlugins);

    QSettings settings;
    m_recentMeshes = settings.value(QStringLiteral("recentMeshes")).toStringList();
    sanitizeRecentMeshes();
    refreshRecentMeshesMenu();
}

RenderWidget *MainWindow::currentRenderWidget() const
{
    if (m_currentRenderWidget)
        return m_currentRenderWidget;
    return m_renderWidgets.isEmpty() ? nullptr : m_renderWidgets.first();
}

RenderWidget *MainWindow::createRenderWidget(QSplitter *parentSplitter)
{
    if (!parentSplitter)
        return nullptr;

    auto *view = new RenderWidget(m_doc, parentSplitter);
    view->setAttribute(Qt::WA_StyledBackground, true);
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    parentSplitter->addWidget(view);
    m_renderWidgets.append(view);

    connect(view, &RenderWidget::viewActivated, this, [this](RenderWidget *activatedView) {
        setCurrentRenderWidget(activatedView);
    });
    connect(view, &RenderWidget::frameRendered, this,
            [this, view](float cpuMs, float gpuMs, bool gpuTimingSupported, bool gpuSampleValid) {
        if (view != currentRenderWidget())
            return;
        updateFrameTimeStats(cpuMs, gpuMs, gpuTimingSupported, gpuSampleValid);
    });
    connect(view, &RenderWidget::trackballCenterPicked, this,
            [this, view](const QVector3D &worldPos) {
        setCurrentRenderWidget(view);
        const QString msg = tr("Trackball center: (%1, %2, %3)")
            .arg(worldPos.x(), 0, 'f', 6)
            .arg(worldPos.y(), 0, 'f', 6)
            .arg(worldPos.z(), 0, 'f', 6);
        statusBar()->showMessage(msg, 3500);
        m_doc->writeLog(msg, Document::LogSource::Application);
    });
    connect(view, &QWidget::customContextMenuRequested, this,
            [this, view](const QPoint &pos) {
        setCurrentRenderWidget(view);

        QMenu menu(view);
        QAction *sceneModeAction = menu.addAction(tr("3D Scene Mode"));
        QAction *uvModeAction = menu.addAction(tr("Parametrization (UV) Mode"));
        menu.addSeparator();
        QAction *splitHAction =
            menu.addAction(QIcon(QStringLiteral(":/img/splitV.png")), tr("Split Horizontally"));
        QAction *splitVAction =
            menu.addAction(QIcon(QStringLiteral(":/img/splitH.png")), tr("Split Vertically"));
        menu.addSeparator();
        QAction *closeAction = menu.addAction(tr("Close View"));
        closeAction->setEnabled(m_renderWidgets.size() > 1);

        QAction *chosen = menu.exec(view->mapToGlobal(pos));
        if (!chosen)
            return;
        if (chosen == sceneModeAction) {
            setCurrentViewSceneMode();
        } else if (chosen == uvModeAction) {
            setCurrentViewParametrizationMode();
        } else if (chosen == splitHAction) {
            splitViewHorizontally();
        } else if (chosen == splitVAction) {
            splitViewVertically();
        } else if (chosen == closeAction) {
            closeCurrentView();
        }
    });

    return view;
}

void MainWindow::setCurrentRenderWidget(RenderWidget *view)
{
    if (!view)
        return;
    if (m_currentRenderWidget == view)
        return;
    m_currentRenderWidget = view;
    updateCurrentViewBorder();
    syncDocumentVisibilityFromCurrentView();
}

void MainWindow::updateCurrentViewBorder()
{
    const bool showCurrentViewIndicator = (m_renderWidgets.size() > 1);
    for (RenderWidget *view : std::as_const(m_renderWidgets)) {
        if (!view)
            continue;
        const bool isCurrent = (view == m_currentRenderWidget);
        view->setCurrentViewHighlighted(showCurrentViewIndicator && isCurrent);
        view->update();
    }
}

void MainWindow::syncDocumentVisibilityFromCurrentView()
{
    RenderWidget *view = currentRenderWidget();
    if (!view || !m_doc)
        return;

    m_syncingVisibilityProxy = true;
    for (int i = 0; i < m_doc->meshCount(); ++i)
        m_doc->setMeshVisible(i, view->meshVisible(i));
    m_syncingVisibilityProxy = false;
}

void MainWindow::splitCurrentView(Qt::Orientation orientation)
{
    RenderWidget *sourceView = currentRenderWidget();
    if (!sourceView)
        return;
    auto *parentSplitter = qobject_cast<QSplitter *>(sourceView->parentWidget());
    if (!parentSplitter)
        return;

    const int sourceIndex = parentSplitter->indexOf(sourceView);
    if (sourceIndex < 0)
        return;

    RenderWidget *newView = nullptr;
    if (parentSplitter->orientation() == orientation) {
        const QList<int> oldSizes = parentSplitter->sizes();
        newView = createRenderWidget(parentSplitter);
        if (!newView)
            return;
        parentSplitter->insertWidget(sourceIndex + 1, newView);

        // Split only the source pane into 50/50, keep all others unchanged.
        QList<int> newSizes = parentSplitter->sizes();
        if (oldSizes.size() == newSizes.size() - 1 && sourceIndex < oldSizes.size()) {
            const int sourceOldSize = qMax(2, oldSizes[sourceIndex]);
            const int firstHalf = sourceOldSize / 2;
            const int secondHalf = sourceOldSize - firstHalf;

            for (int i = 0; i < newSizes.size(); ++i) {
                if (i < sourceIndex) {
                    newSizes[i] = oldSizes[i];
                } else if (i == sourceIndex) {
                    newSizes[i] = firstHalf;
                } else if (i == sourceIndex + 1) {
                    newSizes[i] = secondHalf;
                } else {
                    newSizes[i] = oldSizes[i - 1];
                }
            }
            parentSplitter->setSizes(newSizes);
        }
    } else {
        const QList<int> oldParentSizes = parentSplitter->sizes();

        auto *nestedSplitter = new QSplitter(orientation, parentSplitter);
        nestedSplitter->setChildrenCollapsible(false);

        // Replace source view with a nested splitter in the parent.
        parentSplitter->insertWidget(sourceIndex, nestedSplitter);
        nestedSplitter->addWidget(sourceView);

        newView = createRenderWidget(nestedSplitter);
        if (!newView)
            return;
        nestedSplitter->setSizes(QList<int>{1, 1});

        // Preserve parent splitter allocation so other views are not affected.
        QList<int> parentSizes = parentSplitter->sizes();
        if (parentSizes.size() == oldParentSizes.size()) {
            for (int i = 0; i < parentSizes.size(); ++i)
                parentSizes[i] = oldParentSizes[i];
            parentSplitter->setSizes(parentSizes);
        }
    }

    newView->setRenderSettings(sourceView->renderSettings());
    newView->copyPerMeshRenderModesFrom(sourceView);
    newView->setMeshVisibilityState(sourceView->meshVisibilityState());
    QString viewModeError;
    newView->setViewMode(sourceView->viewMode(), &viewModeError);
    QString cameraError;
    newView->applyCameraStateJson(sourceView->cameraStateJson(), &cameraError);

    setCurrentRenderWidget(newView);
    statusBar()->showMessage(tr("Created new view"), 1500);
}

bool MainWindow::closeRenderWidget(RenderWidget *view)
{
    if (!view || m_renderWidgets.size() <= 1)
        return false;

    RenderWidget *nextCurrent = m_currentRenderWidget;
    if (nextCurrent == view) {
        nextCurrent = nullptr;
        for (RenderWidget *candidate : std::as_const(m_renderWidgets)) {
            if (candidate != view) {
                nextCurrent = candidate;
                break;
            }
        }
    }

    auto *parentSplitter = qobject_cast<QSplitter *>(view->parentWidget());
    m_renderWidgets.removeAll(view);

    view->setParent(nullptr);
    view->deleteLater();

    // Collapse nested splitters left with a single child.
    auto collapse = [this](QSplitter *splitter) {
        QSplitter *current = splitter;
        while (current && current != m_viewSplitter) {
            if (current->count() != 1) {
                current = qobject_cast<QSplitter *>(current->parentWidget());
                continue;
            }

            QWidget *onlyChild = current->widget(0);
            auto *parent = qobject_cast<QSplitter *>(current->parentWidget());
            if (!onlyChild || !parent)
                break;

            const int idx = parent->indexOf(current);
            onlyChild->setParent(parent);
            parent->insertWidget(idx, onlyChild);
            current->deleteLater();
            current = parent;
        }
    };
    collapse(parentSplitter);

    if (!nextCurrent && !m_renderWidgets.isEmpty())
        nextCurrent = m_renderWidgets.first();
    if (nextCurrent)
        setCurrentRenderWidget(nextCurrent);
    else
        updateCurrentViewBorder();

    return true;
}

void MainWindow::closeCurrentView()
{
    RenderWidget *view = currentRenderWidget();
    if (!view)
        return;
    if (!closeRenderWidget(view)) {
        statusBar()->showMessage(tr("Cannot close the last view"), 1800);
        return;
    }
    statusBar()->showMessage(tr("View closed"), 1500);
}

void MainWindow::splitViewHorizontally()
{
    splitCurrentView(Qt::Horizontal);
}

void MainWindow::splitViewVertically()
{
    splitCurrentView(Qt::Vertical);
}

void MainWindow::newDocument()
{
    const bool hadMeshes = (m_doc->meshCount() > 0);
    m_doc->beginUndoStep(tr("New Document"));
    // Remove from back to keep indices valid while emitting meshRemoved/currentMeshChanged.
    while (m_doc->meshCount() > 0)
        m_doc->removeMesh(m_doc->meshCount() - 1);
    m_doc->endUndoStep(hadMeshes);
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

    const bool groupUndoStep = (fileNames.size() > 1);
    if (groupUndoStep)
        m_doc->beginUndoStep(tr("Open Meshes"));

    int loadedCount = 0;
    int failedCount = 0;
    for (const QString &fileName : fileNames) {
        if (loadMeshFromPath(fileName))
            ++loadedCount;
        else
            ++failedCount;
    }
    if (groupUndoStep)
        m_doc->endUndoStep(loadedCount > 0);

    if (fileNames.size() > 1) {
        statusBar()->showMessage(
            tr("Open complete: %1 loaded, %2 failed")
                .arg(loadedCount)
                .arg(failedCount),
            3500);
    }
}

void MainWindow::reloadCurrentMesh()
{
    if (!m_doc || m_doc->meshCount() <= 0) {
        statusBar()->showMessage(tr("No mesh to reload"), 2500);
        return;
    }

    const int currentIndex = m_doc->currentMeshIndex();
    if (currentIndex < 0 || currentIndex >= m_doc->meshCount()) {
        statusBar()->showMessage(tr("No current mesh to reload"), 2500);
        return;
    }

    const QString meshName = m_doc->mesh(currentIndex).name;
    const int err = m_doc->reloadMesh(currentIndex);
    if (err != 0) {
        statusBar()->showMessage(tr("Failed to reload %1").arg(meshName), 3500);
        return;
    }

    statusBar()->showMessage(tr("Reloaded %1").arg(meshName), 2500);
}

void MainWindow::reloadAllMeshes()
{
    if (!m_doc || m_doc->meshCount() <= 0) {
        statusBar()->showMessage(tr("No meshes to reload"), 2500);
        return;
    }

    const int total = m_doc->meshCount();
    m_doc->beginUndoStep(tr("Reload All Meshes"));

    int reloadedCount = 0;
    int failedCount = 0;
    for (int i = 0; i < total; ++i) {
        if (m_doc->reloadMesh(i) == 0)
            ++reloadedCount;
        else
            ++failedCount;
    }

    m_doc->endUndoStep(reloadedCount > 0);
    statusBar()->showMessage(
        tr("Reload complete: %1 reloaded, %2 failed")
            .arg(reloadedCount)
            .arg(failedCount),
        failedCount > 0 ? 4500 : 3000);
}

void MainWindow::refreshFiltersMenu()
{
    if (!m_filtersMenu || !m_doc)
        return;

    m_filtersMenu->clear();
    QAction *filterBrowserAction = m_filtersMenu->addAction(
        tr("Filter Browser..."),
        this,
        &MainWindow::openFilterBrowser);
    filterBrowserAction->setShortcut(QKeySequence::Find);
    filterBrowserAction->setShortcutContext(Qt::ApplicationShortcut);
    m_filtersMenu->addSeparator();

    std::vector<Document::FilterInfo> infos = m_doc->filterInfos();
    if (infos.empty()) {
        QAction *emptyAction = m_filtersMenu->addAction(tr("No filters available"));
        emptyAction->setEnabled(false);
        return;
    }

    std::sort(infos.begin(), infos.end(), [](const Document::FilterInfo &a, const Document::FilterInfo &b) {
        const int menuCmp = a.descriptor.menuPath.compare(b.descriptor.menuPath, Qt::CaseInsensitive);
        if (menuCmp != 0)
            return menuCmp < 0;
        return a.descriptor.name.compare(b.descriptor.name, Qt::CaseInsensitive) < 0;
    });

    auto findOrCreateSubmenu = [](QMenu *parent, const QString &title) -> QMenu * {
        if (!parent)
            return nullptr;
        for (QAction *action : parent->actions()) {
            QMenu *submenu = action ? action->menu() : nullptr;
            if (submenu && submenu->title() == title)
                return submenu;
        }
        return parent->addMenu(title);
    };

    for (const Document::FilterInfo &info : infos) {
        QMenu *menu = m_filtersMenu;
        const QStringList groups = info.descriptor.menuPath.split(
            QLatin1Char('/'),
            Qt::SkipEmptyParts);
        for (const QString &group : groups) {
            menu = findOrCreateSubmenu(menu, group.trimmed());
            if (!menu)
                break;
        }
        if (!menu)
            continue;

        QAction *action = menu->addAction(info.descriptor.name, this, &MainWindow::runFilterAction);
        action->setData(info.key);
        action->setEnabled(info.applicable);

        QString tip = info.descriptor.shortDescription.trimmed();
        if (!info.applicable && !info.applicabilityError.trimmed().isEmpty()) {
            if (!tip.isEmpty())
                tip += QStringLiteral("\n");
            tip += tr("Unavailable: %1").arg(info.applicabilityError);
        }
        if (!tip.isEmpty()) {
            action->setToolTip(tip);
            action->setStatusTip(tip);
        }
    }
}

void MainWindow::openFilterBrowser()
{
    if (!m_doc || !m_filterPanel)
        return;

    if (m_filterDock) {
        if (m_filterDock->isFloating())
            m_filterDock->setFloating(false);
        m_filterDock->show();
        m_filterDock->raise();
    }
    m_filterPanel->showSearchResults();
    m_filterPanel->focusSearch();
}

void MainWindow::runFilterAction()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action || !m_doc || !m_filterPanel)
        return;

    const QString filterKey = action->data().toString();
    if (filterKey.isEmpty())
        return;

    if (m_filterDock) {
        if (m_filterDock->isFloating())
            m_filterDock->setFloating(false);
        m_filterDock->show();
        m_filterDock->raise();
    }
    m_filterPanel->selectFilterByKey(filterKey, true);
}

void MainWindow::executeFilter(
    const QString &filterKey,
    const QString &fallbackLabel,
    const QVariantMap &parameters)
{
    if (!m_doc || filterKey.trimmed().isEmpty())
        return;

    QString label = fallbackLabel;
    if (label.trimmed().isEmpty())
        label = tr("Filter");

    QElapsedTimer timer;
    timer.start();
    const MeshFilterRunResult result = m_doc->runFilter(filterKey, parameters);
    const double elapsedMs = double(timer.nsecsElapsed()) / 1e6;
    const QString elapsedText = QString::number(elapsedMs, 'f', 2);

    if (!result.success) {
        const QString msg = tr("Filter failed: %1").arg(result.errorMessage);
        statusBar()->showMessage(msg, 4500);
        m_doc->writeLog(msg, Document::LogSource::Application);
        m_doc->writeLog(
            tr("Filter '%1' runtime: %2 ms (failed)")
                .arg(label, elapsedText),
            Document::LogSource::Application);
        return;
    }

    QString status = tr("%1 executed").arg(label);
    if (!result.infoMessages.isEmpty())
        status = result.infoMessages.back();
    statusBar()->showMessage(status, 3200);
    m_doc->writeLog(
        tr("Filter '%1' runtime: %2 ms").arg(label, elapsedText),
        Document::LogSource::Application);
}

void MainWindow::undo()
{
    if (!m_doc->undo())
        return;

    const QString label = m_doc->redoText();
    const QString msg = label.isEmpty() ? tr("Undo") : tr("Undo: %1").arg(label);
    statusBar()->showMessage(msg, 1800);
}

void MainWindow::redo()
{
    if (!m_doc->redo())
        return;

    const QString label = m_doc->undoText();
    const QString msg = label.isEmpty() ? tr("Redo") : tr("Redo: %1").arg(label);
    statusBar()->showMessage(msg, 1800);
}

void MainWindow::saveCurrentMesh()
{
    const int currentIndex = m_doc->currentMeshIndex();
    if (currentIndex < 0 || currentIndex >= m_doc->meshCount()) {
        statusBar()->showMessage(tr("No current mesh to save"), 2500);
        return;
    }

    const Document::MeshEntry &entry = m_doc->mesh(currentIndex);
    const QString defaultPath = !entry.sourcePath.isEmpty()
        ? entry.sourcePath
        : QStringLiteral("%1.ply").arg(entry.name.isEmpty() ? QStringLiteral("mesh") : entry.name);

    QString selectedFilter;
    QString targetPath = QFileDialog::getSaveFileName(
        this,
        tr("Save Mesh"),
        defaultPath,
        m_doc->saveDialogFilter(),
        &selectedFilter);
    if (targetPath.isEmpty())
        return;
    targetPath = appendSaveExtensionIfMissing(targetPath, selectedFilter);

    const int capabilityMask = m_doc->saveMaskCapability(targetPath);
    if (capabilityMask == 0) {
        const QString msg =
            tr("No exporter is available for '.%1'").arg(fileExtensionLower(targetPath));
        statusBar()->showMessage(msg, 4000);
        m_doc->writeLog(msg, Document::LogSource::Application);
        return;
    }
    const int availableMask = availableSaveMaskForMesh(entry);
    const int requiredMask = requiredSaveMaskForMesh(entry, capabilityMask);
    const QString extension = fileExtensionLower(targetPath);
    const bool binarySupported = saveFormatSupportsBinary(extension);
    const bool supportsEmbeddedTextures = saveFormatSupportsEmbeddedTextures(extension);
    const bool supportsDracoCompression = saveFormatSupportsDracoCompression(extension);

    MeshIOSaveOptions initialOptions;
    initialOptions.mask = defaultSaveMaskForMesh(entry, capabilityMask);
    initialOptions.binary = binarySupported;
    initialOptions.embedTextures = (extension == QLatin1String("glb"));
    initialOptions.dracoCompression = false;
    initialOptions.dracoCompressionLevel = 7;

    MeshSaveOptionsDialog optionsDialog(
        targetPath,
        capabilityMask,
        availableMask,
        requiredMask,
        initialOptions,
        binarySupported,
        supportsEmbeddedTextures,
        supportsDracoCompression,
        this);
    if (optionsDialog.exec() != QDialog::Accepted)
        return;

    MeshIOSaveOptions saveOptions = optionsDialog.selectedOptions();
    const int err = m_doc->saveCurrentMesh(targetPath, saveOptions);
    if (err != 0) {
        statusBar()->showMessage(tr("Save failed"), 4000);
        return;
    }

    statusBar()->showMessage(tr("Mesh saved to %1").arg(targetPath), 3000);
}

void MainWindow::saveSnapshotPng()
{
    RenderWidget *view = currentRenderWidget();
    if (!view)
        return;

    QString targetPath = QFileDialog::getSaveFileName(
        this,
        tr("Save Snapshot"),
        QStringLiteral("snapshot.png"),
        tr("PNG Image (*.png)"));
    if (targetPath.isEmpty())
        return;
    if (!targetPath.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
        targetPath += QStringLiteral(".png");

    const qreal dpr = qMax(1.0, view->devicePixelRatioF());
    const QSize basePixelSize(
        qMax(1, int(std::lround(double(view->width()) * dpr))),
        qMax(1, int(std::lround(double(view->height()) * dpr))));

    QDialog optionsDialog(this);
    optionsDialog.setWindowTitle(tr("Snapshot Options"));
    auto *optionsLayout = new QVBoxLayout(&optionsDialog);
    auto *form = new QFormLayout();
    optionsLayout->addLayout(form);

    auto *widthSpin = new QSpinBox(&optionsDialog);
    widthSpin->setRange(64, 16384);
    widthSpin->setValue(basePixelSize.width());
    widthSpin->setSuffix(tr(" px"));

    auto *heightSpin = new QSpinBox(&optionsDialog);
    heightSpin->setRange(64, 16384);
    heightSpin->setValue(basePixelSize.height());
    heightSpin->setSuffix(tr(" px"));

    auto *lockAspect = new QCheckBox(tr("Lock aspect ratio"), &optionsDialog);
    lockAspect->setChecked(true);

    form->addRow(tr("Width"), widthSpin);
    form->addRow(tr("Height"), heightSpin);
    form->addRow(QString(), lockAspect);

    bool resizingFromLock = false;
    const double aspect =
        (basePixelSize.height() > 0)
        ? (double(basePixelSize.width()) / double(basePixelSize.height()))
        : 1.0;
    connect(widthSpin, qOverload<int>(&QSpinBox::valueChanged), &optionsDialog, [=, &resizingFromLock](int w) {
        if (!lockAspect->isChecked() || resizingFromLock || aspect <= 0.0)
            return;
        resizingFromLock = true;
        heightSpin->setValue(qMax(64, int(std::lround(double(w) / aspect))));
        resizingFromLock = false;
    });
    connect(heightSpin, qOverload<int>(&QSpinBox::valueChanged), &optionsDialog, [=, &resizingFromLock](int h) {
        if (!lockAspect->isChecked() || resizingFromLock)
            return;
        resizingFromLock = true;
        widthSpin->setValue(qMax(64, int(std::lround(double(h) * aspect))));
        resizingFromLock = false;
    });

    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &optionsDialog);
    optionsLayout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &optionsDialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &optionsDialog, &QDialog::reject);

    if (optionsDialog.exec() != QDialog::Accepted)
        return;

    const QSize snapshotSize(widthSpin->value(), heightSpin->value());
    QString captureError;
    const QImage snapshot = renderSnapshotOffscreen(view, snapshotSize, &captureError);
    if (snapshot.isNull()) {
        const QString msg = tr("Failed to capture snapshot: %1").arg(captureError);
        statusBar()->showMessage(msg, 3500);
        m_doc->writeLog(msg, Document::LogSource::Application);
        return;
    }

    QImage outImage = snapshot.convertToFormat(QImage::Format_RGBA8888);
    outImage.setText(QStringLiteral("QMeshLab.CameraTrackballState"), view->cameraStateJson());

    QImageWriter writer(targetPath, "png");
    if (!writer.write(outImage)) {
        const QString msg = tr("Failed to save snapshot: %1").arg(writer.errorString());
        statusBar()->showMessage(msg, 4500);
        m_doc->writeLog(msg, Document::LogSource::Application);
        return;
    }

    const QString msg = tr("Snapshot saved to %1").arg(targetPath);
    statusBar()->showMessage(msg, 3000);
    m_doc->writeLog(msg, Document::LogSource::Application);
}

QImage MainWindow::renderSnapshotOffscreen(
    RenderWidget *sourceView,
    const QSize &pixelSize,
    QString *errorMessage)
{
    auto fail = [&](const QString &msg) {
        if (errorMessage)
            *errorMessage = msg;
        return QImage();
    };

    if (!sourceView)
        return fail(tr("No active view"));
    if (pixelSize.width() <= 0 || pixelSize.height() <= 0)
        return fail(tr("Invalid snapshot resolution"));

    const QSize oldFixedSize = sourceView->fixedColorBufferSize();
    sourceView->setFixedColorBufferSize(pixelSize);
    sourceView->update();

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    const QMetaObject::Connection frameConn =
        QObject::connect(sourceView, &RenderWidget::frameRendered, &loop, [&loop](float, float, bool, bool) {
            loop.quit();
        });

    timeout.start(1500);
    loop.exec();
    QObject::disconnect(frameConn);

    const QImage result = sourceView->grabFramebuffer();

    sourceView->setFixedColorBufferSize(oldFixedSize);
    sourceView->update();

    if (result.isNull())
        return fail(tr("Render target capture failed"));

    if (errorMessage)
        errorMessage->clear();
    return result;
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
    const std::vector<Document::ImportPluginInfo> importPlugins = m_doc->importPluginInfos();
    const QStringList importExtensions = m_doc->importSupportedExtensions();
    const std::vector<Document::ExportPluginInfo> exportPlugins = m_doc->exportPluginInfos();
    const QStringList exportExtensions = m_doc->exportSupportedExtensions();

    const bool hasImportMatrix = !importPlugins.empty() && !importExtensions.isEmpty();
    const bool hasExportMatrix = !exportPlugins.empty() && !exportExtensions.isEmpty();
    if (!hasImportMatrix && !hasExportMatrix) {
        QMessageBox::information(this, tr("I/O Plugins"), tr("No plugins are available."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("I/O Plugins"));

    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Import preferences"), &dialog));

    std::vector<QButtonGroup *> groups;
    if (hasImportMatrix) {
        layout->addWidget(new QLabel(
            tr("Choose the preferred plugin for each file type/extension."),
            &dialog));

        auto *importTable = new QTableWidget(
            static_cast<int>(importPlugins.size()),
            importExtensions.size(),
            &dialog);
        importTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        importTable->setSelectionMode(QAbstractItemView::NoSelection);
        importTable->setFocusPolicy(Qt::NoFocus);
        importTable->setHorizontalHeaderLabels(importExtensions);
        importTable->verticalHeader()->setVisible(true);
        importTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        importTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

        QStringList rowLabels;
        rowLabels.reserve(static_cast<int>(importPlugins.size()));
        for (const auto &plugin : importPlugins)
            rowLabels << plugin.name;
        importTable->setVerticalHeaderLabels(rowLabels);

        groups.assign(static_cast<size_t>(importExtensions.size()), nullptr);
        for (int col = 0; col < importExtensions.size(); ++col) {
            auto *group = new QButtonGroup(importTable);
            group->setExclusive(true);
            groups[static_cast<size_t>(col)] = group;
        }

        for (int col = 0; col < importExtensions.size(); ++col) {
            const QString extension = importExtensions[col];
            const QString preferredPluginId = m_doc->preferredImportPluginForExtension(extension);
            int preferredRow = -1;
            int firstSupportedRow = -1;

            for (int row = 0; row < static_cast<int>(importPlugins.size()); ++row) {
                const bool supports = importPlugins[static_cast<size_t>(row)].extensions.contains(extension);
                if (!supports)
                    continue;

                if (firstSupportedRow < 0)
                    firstSupportedRow = row;
                if (importPlugins[static_cast<size_t>(row)].id == preferredPluginId)
                    preferredRow = row;

                auto *radio = new QRadioButton(importTable);
                radio->setToolTip(
                    tr("Use \"%1\" for .%2 files")
                        .arg(importPlugins[static_cast<size_t>(row)].name, extension));
                groups[static_cast<size_t>(col)]->addButton(radio, row);

                auto *cell = new QWidget(importTable);
                auto *cellLayout = new QHBoxLayout(cell);
                cellLayout->setContentsMargins(0, 0, 0, 0);
                cellLayout->addWidget(radio, 0, Qt::AlignCenter);
                importTable->setCellWidget(row, col, cell);
            }

            const int rowToSelect = (preferredRow >= 0) ? preferredRow : firstSupportedRow;
            if (rowToSelect >= 0) {
                if (QAbstractButton *button = groups[static_cast<size_t>(col)]->button(rowToSelect))
                    button->setChecked(true);
            }
        }

        importTable->resizeColumnsToContents();
        importTable->resizeRowsToContents();
        importTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        importTable->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        importTable->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
        layout->addWidget(importTable, 1);
    } else {
        auto *label = new QLabel(tr("No import plugins are available."), &dialog);
        label->setStyleSheet(QStringLiteral("color: palette(mid);"));
        layout->addWidget(label);
    }

    layout->addSpacing(8);
    layout->addWidget(new QLabel(tr("Export support"), &dialog));
    if (hasExportMatrix) {
        auto *summary = new QLabel(
            tr("Savable formats: %1")
                .arg(exportExtensions.join(QStringLiteral(", "))),
            &dialog);
        summary->setStyleSheet(QStringLiteral("color: palette(mid);"));
        layout->addWidget(summary);
    } else {
        auto *summary = new QLabel(tr("No savable formats available."), &dialog);
        summary->setStyleSheet(QStringLiteral("color: palette(mid);"));
        layout->addWidget(summary);
    }

    if (hasExportMatrix) {
        auto *exportTable = new QTableWidget(
            static_cast<int>(exportPlugins.size()),
            exportExtensions.size(),
            &dialog);
        exportTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        exportTable->setSelectionMode(QAbstractItemView::NoSelection);
        exportTable->setFocusPolicy(Qt::NoFocus);
        exportTable->setHorizontalHeaderLabels(exportExtensions);
        exportTable->verticalHeader()->setVisible(true);
        exportTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        exportTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

        QStringList rowLabels;
        rowLabels.reserve(static_cast<int>(exportPlugins.size()));
        for (const auto &plugin : exportPlugins)
            rowLabels << plugin.name;
        exportTable->setVerticalHeaderLabels(rowLabels);

        for (int col = 0; col < exportExtensions.size(); ++col) {
            const QString extension = exportExtensions[col];
            for (int row = 0; row < static_cast<int>(exportPlugins.size()); ++row) {
                const bool supports =
                    exportPlugins[static_cast<size_t>(row)].extensions.contains(extension);

                auto *item = new QTableWidgetItem();
                item->setFlags(Qt::ItemIsEnabled);
                item->setTextAlignment(Qt::AlignCenter);
                if (supports) {
                    item->setText(QStringLiteral("●"));
                    item->setToolTip(
                        tr("\"%1\" can save .%2 files")
                            .arg(exportPlugins[static_cast<size_t>(row)].name, extension));
                }
                exportTable->setItem(row, col, item);
            }
        }

        exportTable->resizeColumnsToContents();
        exportTable->resizeRowsToContents();
        exportTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        exportTable->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        exportTable->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
        layout->addWidget(exportTable, 1);
    } else {
        auto *label = new QLabel(tr("No export plugins are available."), &dialog);
        label->setStyleSheet(QStringLiteral("color: palette(mid);"));
        layout->addWidget(label);
    }

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
        dialog.resize(dialog.sizeHint().boundedTo(maxDialogSize));
    }

    if (dialog.exec() != QDialog::Accepted)
        return;

    for (int col = 0; col < importExtensions.size(); ++col) {
        QButtonGroup *group = groups[static_cast<size_t>(col)];
        if (!group)
            continue;
        const int selectedRow = group->checkedId();
        if (selectedRow < 0 || selectedRow >= static_cast<int>(importPlugins.size()))
            continue;

        m_doc->setPreferredImportPluginForExtension(
            importExtensions[col],
            importPlugins[static_cast<size_t>(selectedRow)].id);
    }

    if (hasImportMatrix)
        statusBar()->showMessage(tr("Plugin preferences updated"), 2000);
}

void MainWindow::showFilterPlugins()
{
    const QStringList pluginSummaries = m_doc->loadedFilterPluginSummaries();
    const std::vector<Document::FilterInfo> filters = m_doc->filterInfos();
    if (pluginSummaries.isEmpty() && filters.empty()) {
        QMessageBox::information(this, tr("Filter Plugins"), tr("No filter plugins are available."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Filter Plugins"));
    auto *layout = new QVBoxLayout(&dialog);

    auto *countLabel = new QLabel(
        tr("Total filters: %1").arg(static_cast<int>(filters.size())),
        &dialog);
    QFont countFont = countLabel->font();
    countFont.setBold(true);
    countLabel->setFont(countFont);
    layout->addWidget(countLabel);

    layout->addWidget(new QLabel(tr("Loaded plugins"), &dialog));

    auto *pluginList = new QListWidget(&dialog);
    pluginList->setSelectionMode(QAbstractItemView::NoSelection);
    pluginList->setFocusPolicy(Qt::NoFocus);
    if (pluginSummaries.isEmpty()) {
        pluginList->addItem(tr("No loaded plugins."));
    } else {
        for (const QString &summary : pluginSummaries)
            pluginList->addItem(summary);
    }
    pluginList->setMaximumHeight(std::min(180, std::max(80, pluginList->sizeHintForRow(0) * 6)));
    layout->addWidget(pluginList);

    if (!filters.empty()) {
        struct PluginAggregate {
            QString id;
            QString name;
            int filterCount = 0;
            int applicableCount = 0;
            QSet<QString> categories;
        };
        std::vector<PluginAggregate> aggregates;
        aggregates.reserve(filters.size());

        auto getAggregateIndex = [&aggregates](const QString &pluginId) -> int {
            for (int i = 0; i < static_cast<int>(aggregates.size()); ++i) {
                if (aggregates[static_cast<size_t>(i)].id == pluginId)
                    return i;
            }
            return -1;
        };

        for (const auto &info : filters) {
            int idx = getAggregateIndex(info.pluginId);
            if (idx < 0) {
                PluginAggregate aggregate;
                aggregate.id = info.pluginId;
                aggregate.name = info.pluginName;
                aggregates.push_back(std::move(aggregate));
                idx = static_cast<int>(aggregates.size()) - 1;
            }

            PluginAggregate &aggregate = aggregates[static_cast<size_t>(idx)];
            ++aggregate.filterCount;
            if (info.applicable)
                ++aggregate.applicableCount;

            QString category = info.descriptor.menuPath.section('/', 0, 0).trimmed();
            if (category.isEmpty())
                category = tr("General");
            aggregate.categories.insert(category);
        }

        std::sort(aggregates.begin(), aggregates.end(), [](const PluginAggregate &a, const PluginAggregate &b) {
            return a.name.localeAwareCompare(b.name) < 0;
        });

        layout->addSpacing(8);
        layout->addWidget(new QLabel(tr("Plugin details"), &dialog));

        auto *pluginTable = new QTableWidget(static_cast<int>(aggregates.size()), 5, &dialog);
        pluginTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        pluginTable->setSelectionMode(QAbstractItemView::NoSelection);
        pluginTable->setFocusPolicy(Qt::NoFocus);
        pluginTable->verticalHeader()->setVisible(false);
        pluginTable->setHorizontalHeaderLabels(
            { tr("Plugin"), tr("Id"), tr("Filters"), tr("Applicable"), tr("Categories") });
        pluginTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        pluginTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        pluginTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        pluginTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        pluginTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);

        for (int row = 0; row < static_cast<int>(aggregates.size()); ++row) {
            const PluginAggregate &aggregate = aggregates[static_cast<size_t>(row)];
            QStringList categoryList = aggregate.categories.values();
            categoryList.sort(Qt::CaseInsensitive);
            const QString categories = categoryList.join(QStringLiteral(", "));

            auto *nameItem = new QTableWidgetItem(aggregate.name);
            nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
            pluginTable->setItem(row, 0, nameItem);

            auto *idItem = new QTableWidgetItem(aggregate.id);
            idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
            pluginTable->setItem(row, 1, idItem);

            auto *countItem = new QTableWidgetItem(QString::number(aggregate.filterCount));
            countItem->setTextAlignment(Qt::AlignCenter);
            countItem->setFlags(countItem->flags() & ~Qt::ItemIsEditable);
            pluginTable->setItem(row, 2, countItem);

            auto *applicableItem = new QTableWidgetItem(
                QStringLiteral("%1/%2").arg(aggregate.applicableCount).arg(aggregate.filterCount));
            applicableItem->setTextAlignment(Qt::AlignCenter);
            applicableItem->setFlags(applicableItem->flags() & ~Qt::ItemIsEditable);
            pluginTable->setItem(row, 3, applicableItem);

            auto *categoriesItem = new QTableWidgetItem(categories);
            categoriesItem->setFlags(categoriesItem->flags() & ~Qt::ItemIsEditable);
            pluginTable->setItem(row, 4, categoriesItem);
        }

        pluginTable->resizeRowsToContents();
        layout->addWidget(pluginTable, 1);

        layout->addSpacing(8);
        layout->addWidget(new QLabel(tr("Declared filters"), &dialog));

        std::vector<Document::FilterInfo> sortedFilters = filters;
        std::sort(sortedFilters.begin(), sortedFilters.end(),
            [](const Document::FilterInfo &a, const Document::FilterInfo &b) {
                const int pluginCmp = a.pluginName.localeAwareCompare(b.pluginName);
                if (pluginCmp != 0)
                    return pluginCmp < 0;
                const int menuCmp = a.descriptor.menuPath.localeAwareCompare(b.descriptor.menuPath);
                if (menuCmp != 0)
                    return menuCmp < 0;
                return a.descriptor.name.localeAwareCompare(b.descriptor.name) < 0;
            });

        auto *filterTable = new QTableWidget(static_cast<int>(sortedFilters.size()), 6, &dialog);
        filterTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        filterTable->setSelectionMode(QAbstractItemView::NoSelection);
        filterTable->setFocusPolicy(Qt::NoFocus);
        filterTable->verticalHeader()->setVisible(false);
        filterTable->setHorizontalHeaderLabels(
            { tr("Filter"), tr("Plugin"), tr("Menu"), tr("Input"), tr("Output"), tr("Status") });
        filterTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        filterTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        filterTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        filterTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        filterTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        filterTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);

        for (int row = 0; row < static_cast<int>(sortedFilters.size()); ++row) {
            const auto &info = sortedFilters[static_cast<size_t>(row)];

            auto setTextCell = [filterTable, row](int col, const QString &text, Qt::Alignment align = Qt::AlignLeft) {
                auto *item = new QTableWidgetItem(text);
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                item->setTextAlignment(align);
                filterTable->setItem(row, col, item);
            };

            setTextCell(0, info.descriptor.name);
            setTextCell(1, info.pluginName);
            setTextCell(2, info.descriptor.menuPath.isEmpty() ? tr("General") : info.descriptor.menuPath);
            setTextCell(3, filterInputDomainLabel(info.descriptor.inputDomain), Qt::AlignCenter);
            setTextCell(4, filterOutputDomainLabel(info.descriptor.outputDomain), Qt::AlignCenter);

            const QString statusText = info.applicable ? tr("OK") : tr("Unavailable");
            setTextCell(5, statusText, Qt::AlignCenter);
            if (!info.applicable && filterTable->item(row, 5))
                filterTable->item(row, 5)->setToolTip(info.applicabilityError);
        }

        filterTable->resizeRowsToContents();
        layout->addWidget(filterTable, 2);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, Qt::Horizontal, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);
    dialog.adjustSize();

    if (QScreen *screen = dialog.screen()) {
        const QSize maxDialogSize(
            screen->availableGeometry().width() * 9 / 10,
            screen->availableGeometry().height() * 9 / 10);
        const QSize initialSize = QSize(
            std::max(maxDialogSize.width() * 8 / 10, dialog.sizeHint().width()),
            std::max(maxDialogSize.height() * 8 / 10, dialog.sizeHint().height()))
                                      .boundedTo(maxDialogSize);
        dialog.resize(initialSize);
    }

    dialog.exec();
}

void MainWindow::resetCamera()
{
    RenderWidget *view = currentRenderWidget();
    if (!view)
        return;
    view->resetCameraToScene();
    statusBar()->showMessage(tr("Camera reset"), 1500);
}

void MainWindow::setCurrentViewSceneMode()
{
    RenderWidget *view = currentRenderWidget();
    if (!view)
        return;
    QString error;
    if (!view->setViewMode(RenderWidget::ViewMode::Scene3D, &error)) {
        const QString msg = tr("Cannot switch to 3D mode: %1").arg(error);
        statusBar()->showMessage(msg, 3000);
        m_doc->writeLog(msg, Document::LogSource::Application);
        return;
    }
    statusBar()->showMessage(tr("View mode: 3D scene"), 2000);
}

void MainWindow::setCurrentViewParametrizationMode()
{
    RenderWidget *view = currentRenderWidget();
    if (!view)
        return;
    QString error;
    if (!view->setViewMode(RenderWidget::ViewMode::ParametrizationUV, &error)) {
        const QString msg = tr("Cannot switch to parametrization mode: %1").arg(error);
        statusBar()->showMessage(msg, 3500);
        m_doc->writeLog(msg, Document::LogSource::Application);
        return;
    }
    statusBar()->showMessage(tr("View mode: parametrization (UV)"), 2000);
}

void MainWindow::copyCameraState()
{
    RenderWidget *view = currentRenderWidget();
    if (!view)
        return;

    const QString json = view->cameraStateJson();
    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(json, QClipboard::Clipboard);
        if (clipboard->supportsSelection())
            clipboard->setText(json, QClipboard::Selection);
    }

    const QString msg = tr("Camera/trackball JSON copied to clipboard");
    statusBar()->showMessage(msg, 2000);
    m_doc->writeLog(msg, Document::LogSource::Application);
}

void MainWindow::pasteCameraState()
{
    RenderWidget *view = currentRenderWidget();
    if (!view)
        return;

    QString jsonText;
    if (QClipboard *clipboard = QGuiApplication::clipboard())
        jsonText = clipboard->text(QClipboard::Clipboard);

    QString error;
    if (!view->applyCameraStateJson(jsonText, &error)) {
        const QString msg = tr("Cannot paste camera/trackball JSON: %1").arg(error);
        statusBar()->showMessage(msg, 3500);
        m_doc->writeLog(msg, Document::LogSource::Application);
        return;
    }

    const QString msg = tr("Camera/trackball state restored from clipboard JSON");
    statusBar()->showMessage(msg, 2500);
    m_doc->writeLog(msg, Document::LogSource::Application);
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
    cleaned.reserve(kRecentMeshesLimit);
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
        if (cleaned.size() >= kRecentMeshesLimit)
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

    const std::array<QString, 8> shortcuts = {
        QStringLiteral("Ctrl+1"),
        QStringLiteral("Ctrl+2"),
        QStringLiteral("Ctrl+3"),
        QStringLiteral("Ctrl+4"),
        QStringLiteral("Ctrl+5"),
        QStringLiteral("Ctrl+6"),
        QStringLiteral("Ctrl+7"),
        QStringLiteral("Ctrl+8")
    };

    for (int i = 0; i < m_recentMeshes.size() && i < kRecentMeshesLimit; ++i) {
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
