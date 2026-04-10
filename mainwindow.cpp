#include "mainwindow.h"
#include "document.h"
#include "renderwidget.h"
#include "layerwidget.h"
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
#include <QStyle>
#include <QDialog>
#include <QDialogButtonBox>
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
    m_viewSplitter->setStyleSheet(QStringLiteral(
        "RenderWidget {"
        "  border: 1px solid rgb(90, 90, 90);"
        "}"
        "RenderWidget[currentView=\"true\"] {"
        "  border: 2px solid rgb(42, 160, 240);"
        "}"
    ));
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
    helpMenu->addAction(tr("Import &Plugins..."), this, &MainWindow::showImportPlugins);

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
    view->setProperty("currentView", false);
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
    for (RenderWidget *view : std::as_const(m_renderWidgets)) {
        if (!view)
            continue;
        const bool isCurrent = (view == m_currentRenderWidget);
        if (view->property("currentView").toBool() == isCurrent)
            continue;
        view->setProperty("currentView", isCurrent);
        if (QStyle *s = view->style()) {
            s->unpolish(view);
            s->polish(view);
        }
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
