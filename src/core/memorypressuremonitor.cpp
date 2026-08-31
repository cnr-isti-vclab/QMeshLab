#include "memorypressuremonitor.h"

#if defined(Q_OS_DARWIN)
#include <dispatch/dispatch.h>
#endif

struct MemoryPressureMonitor::NativeState {
#if defined(Q_OS_DARWIN)
    dispatch_source_t source = nullptr;
#endif
};

MemoryPressureMonitor::MemoryPressureMonitor(QObject *parent)
    : QObject(parent)
    , m_native(std::make_unique<NativeState>())
{
#if defined(Q_OS_DARWIN)
    m_native->source = dispatch_source_create(
        DISPATCH_SOURCE_TYPE_MEMORYPRESSURE,
        0,
        DISPATCH_MEMORYPRESSURE_WARN | DISPATCH_MEMORYPRESSURE_CRITICAL,
        dispatch_get_main_queue());
    if (!m_native->source)
        return;

    dispatch_set_context(m_native->source, this);
    dispatch_source_set_event_handler_f(m_native->source, [](void *context) {
        auto *monitor = static_cast<MemoryPressureMonitor *>(context);
        if (!monitor || !monitor->m_native || !monitor->m_native->source)
            return;
        const unsigned long flags = dispatch_source_get_data(monitor->m_native->source);
        if (flags & DISPATCH_MEMORYPRESSURE_CRITICAL)
            emit monitor->memoryPressure(MemoryPressureLevel::Critical);
        else if (flags & DISPATCH_MEMORYPRESSURE_WARN)
            emit monitor->memoryPressure(MemoryPressureLevel::Warning);
    });
    dispatch_resume(m_native->source);
    m_supported = true;
#endif
}

MemoryPressureMonitor::~MemoryPressureMonitor()
{
#if defined(Q_OS_DARWIN)
    if (m_native && m_native->source) {
        dispatch_set_context(m_native->source, nullptr);
        dispatch_source_cancel(m_native->source);
#if !OS_OBJECT_USE_OBJC
        dispatch_release(m_native->source);
#endif
        m_native->source = nullptr;
    }
#endif
}
