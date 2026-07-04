#include "document_internal.h"

#include <QTime>

using namespace DocumentInternal;

void Document::clearLog()
{
    if (m_logMessages.empty())
        return;

    m_logMessages.clear();
    m_lastCallbackBucket = -1;
    emit logCleared();
}

void Document::writeLog(const QString &message, LogSource source, bool replaceLast)
{
    QString normalizedMessage = message;
    if (!replaceLast && normalizedMessage.endsWith('\r'))
        replaceLast = true;

    while (!normalizedMessage.isEmpty() && (normalizedMessage.endsWith('\n') || normalizedMessage.endsWith('\r')))
        normalizedMessage.chop(1);
    normalizedMessage = normalizedMessage.trimmed();

    if (normalizedMessage.isEmpty())
        return;

    // Prefix every entry with the wall-clock time (ms since midnight) so gaps
    // between consecutive log lines reveal where real time is actually spent.
    normalizedMessage = QStringLiteral("[t=%1] %2")
                            .arg(QTime::currentTime().msecsSinceStartOfDay())
                            .arg(normalizedMessage);

    if (replaceLast && !m_logMessages.empty()) {
        m_logMessages.back() = LogEntry{normalizedMessage, source};
    } else {
        m_logMessages.push_back(LogEntry{normalizedMessage, source});
    }

    emit logMessageAdded(normalizedMessage, source, replaceLast);
}

vcg::CallBackPos *Document::logCallback()
{
    return &Document::dispatchLogCallback;
}

bool Document::handleLogCallback(int pos, const char *message)
{
    ++m_loadCallbackCount;

    const int clampedPos = std::clamp(pos, 0, 100);
    const bool isLoadCallback = (m_callbackMode == CallbackMode::Load);
    const bool isFilterCallback = (m_callbackMode == CallbackMode::Filter);
    const qint64 nowMs = m_loadCallbackTimer.isValid() ? m_loadCallbackTimer.elapsed() : 0;
    const bool forceUiUpdate = (clampedPos == 0 || clampedPos == 100);
    const bool progressChanged = (clampedPos != m_lastProgressPos);
    const bool uiThrottleElapsed =
        (m_lastProgressEmitMs < 0) || (nowMs - m_lastProgressEmitMs >= 33);

    QString text;
    bool textDecoded = false;
    auto decodeText = [&]() {
        if (textDecoded)
            return;
        const QByteArray rawMessage(message ? message : "");
        text = QString::fromLocal8Bit(rawMessage);
        while (!text.isEmpty() && (text.endsWith('\n') || text.endsWith('\r')))
            text.chop(1);
        text = text.trimmed();
        textDecoded = true;
    };

    if (progressChanged && (forceUiUpdate || uiThrottleElapsed)) {
        m_lastProgressPos = clampedPos;
        m_lastProgressEmitMs = nowMs;
        decodeText();
        if (isLoadCallback || isFilterCallback) {
            if (isLoadCallback)
                emit loadProgressUpdated(clampedPos, text);
            else
                emit filterProgressUpdated(clampedPos, text);
            ++m_loadProgressEmitCount;
        }

        const bool processEventsThrottleElapsed =
            (m_lastProcessEventsMs < 0) || (nowMs - m_lastProcessEventsMs >= 80);
        if ((isLoadCallback || isFilterCallback) && (forceUiUpdate || processEventsThrottleElapsed)) {
            QElapsedTimer processTimer;
            processTimer.start();
            const QEventLoop::ProcessEventsFlags flags = isFilterCallback
                ? QEventLoop::AllEvents
                : QEventLoop::ExcludeUserInputEvents;
            QCoreApplication::processEvents(flags);
            m_loadProcessEventsNs += processTimer.nsecsElapsed();
            m_lastProcessEventsMs = nowMs;
            ++m_loadProcessEventsCount;
        }
    }

    const int bucket = clampedPos / 10;
    if (bucket == m_lastCallbackBucket)
        return !m_cancelRequested.load(std::memory_order_relaxed);

    m_lastCallbackBucket = bucket;
    decodeText();
    const bool replaceLast = message ? QByteArray(message).endsWith('\r') : false;

    if (text.isEmpty())
        writeLog(tr("Progress %1%").arg(clampedPos), LogSource::VCG, replaceLast);
    else
        writeLog(tr("%1% - %2").arg(clampedPos, 3).arg(text), LogSource::VCG, replaceLast);

    return !m_cancelRequested.load(std::memory_order_relaxed);
}

bool Document::dispatchLogCallback(int pos, const char *message)
{
    if (!g_callbackDocument)
        return true;

    return g_callbackDocument->handleLogCallback(pos, message);
}
