#include "document_internal.h"

using namespace DocumentInternal;

QStringList Document::loadedPluginSummaries() const
{
    return m_pluginManager->loadedPluginSummaries();
}

QStringList Document::loadedFilterPluginSummaries() const
{
    if (!m_filterPluginManager)
        return {};
    return m_filterPluginManager->loadedPluginSummaries();
}

std::vector<Document::FilterInfo> Document::filterInfos() const
{
    std::vector<FilterInfo> infos;
    if (!m_filterPluginManager)
        return infos;

    const auto managedInfos = m_filterPluginManager->filterInfos(*this);
    infos.reserve(managedInfos.size());
    for (const auto &managedInfo : managedInfos) {
        FilterInfo info;
        info.key = managedInfo.key;
        info.pluginId = managedInfo.pluginId;
        info.pluginName = managedInfo.pluginName;
        info.descriptor = managedInfo.descriptor;
        info.applicable = managedInfo.applicable;
        info.applicabilityError = managedInfo.applicabilityError;
        infos.push_back(std::move(info));
    }
    return infos;
}

bool Document::validateFilterInvocation(
    const QString &filterKey,
    const MeshFilterParameterValues &parameters,
    QString &errorMessage) const
{
    if (!m_filterPluginManager) {
        errorMessage = tr("Filter manager is not available.");
        return false;
    }
    return m_filterPluginManager->validateFilterInvocation(filterKey, parameters, *this, errorMessage);
}

MeshFilterRunResult Document::runFilter(
    const QString &filterKey,
    const MeshFilterParameterValues &parameters)
{
    if (!m_filterPluginManager) {
        return {
            false,
            false,
            tr("No filter plugin manager is available.")
        };
    }

    // Bracket the whole run: most filters call doc.progressCallback() without ever
    // calling beginFilterProgress, so without this their callback mode would stay
    // None and their progress would not be reported at all. Done without emitting the
    // started/finished signals, so filters that *do* drive the lifecycle themselves
    // still own the progress bar.
    Document *previousCallbackDocument = g_callbackDocument;
    const CallbackMode previousCallbackMode = m_callbackMode;
    g_callbackDocument = this;
    m_callbackMode = CallbackMode::Filter;

    MeshFilterRunResult result = m_filterPluginManager->runFilter(filterKey, parameters, *this);

    m_callbackMode = previousCallbackMode;
    g_callbackDocument = previousCallbackDocument;
    // The transient progress line must never outlive its run, including for filters
    // that never called finishFilterProgress.
    clearProgressLog();
    return result;
}

vcg::CallBackPos *Document::progressCallback()
{
    return logCallback();
}

void Document::beginFilterProgress(const QString &label)
{
    m_lastProgressPos = -1;
    m_loadCallbackCount = 0;
    m_loadProgressEmitCount = 0;
    m_loadProcessEventsCount = 0;
    m_loadProcessEventsNs = 0;
    m_lastProgressEmitMs = -1;
    m_lastProcessEventsMs = -1;
    m_loadCallbackTimer.invalidate();
    m_loadCallbackTimer.start();
    m_cancelRequested.store(false, std::memory_order_relaxed);
    m_callbackMode = CallbackMode::Filter;
    // Drop any stale progress line left by an earlier operation.
    clearProgressLog();

    const QString normalizedLabel = label.trimmed().isEmpty() ? tr("Filter") : label.trimmed();
    emit filterProgressStarted(normalizedLabel);
    emit filterProgressUpdated(0, normalizedLabel);
}

void Document::finishFilterProgress(bool success, const QString &message)
{
    const QString normalizedMessage = message.trimmed();
    m_callbackMode = CallbackMode::None;
    m_cancelRequested.store(false, std::memory_order_relaxed);
    // The operation is over, so its progress line goes away; whatever the filter
    // reports lands as ordinary log entries instead.
    clearProgressLog();
    emit filterProgressFinished(success, normalizedMessage);
}

void Document::requestOperationCancel()
{
    m_cancelRequested.store(true, std::memory_order_relaxed);
}

bool Document::isOperationCancelRequested() const
{
    return m_cancelRequested.load(std::memory_order_relaxed);
}

