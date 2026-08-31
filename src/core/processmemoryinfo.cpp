#include "processmemoryinfo.h"

#if defined(Q_OS_DARWIN)
#include <mach/mach.h>
#include <mach/task_info.h>
#elif defined(Q_OS_LINUX)
#include <QFile>
#include <QRegularExpression>
#include <QString>
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <psapi.h>
#endif

ProcessMemoryInfo queryCurrentProcessMemoryInfo()
{
    ProcessMemoryInfo result;

#if defined(Q_OS_DARWIN)
    task_vm_info_data_t vmInfo{};
    mach_msg_type_number_t vmCount = TASK_VM_INFO_COUNT;
    if (task_info(
            mach_task_self(),
            TASK_VM_INFO,
            reinterpret_cast<task_info_t>(&vmInfo),
            &vmCount) == KERN_SUCCESS) {
        result.physicalFootprintBytes = qint64(vmInfo.phys_footprint);
    }

    mach_task_basic_info_data_t basicInfo{};
    mach_msg_type_number_t basicCount = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(
            mach_task_self(),
            MACH_TASK_BASIC_INFO,
            reinterpret_cast<task_info_t>(&basicInfo),
            &basicCount) == KERN_SUCCESS) {
        result.residentBytes = qint64(basicInfo.resident_size);
    }
#elif defined(Q_OS_LINUX)
    QFile status(QStringLiteral("/proc/self/status"));
    if (status.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString text = QString::fromLatin1(status.readAll());
        const QRegularExpressionMatch match =
            QRegularExpression(QStringLiteral("^VmRSS:\\s+(\\d+)\\s+kB$"),
                               QRegularExpression::MultilineOption)
                .match(text);
        if (match.hasMatch())
            result.residentBytes = match.captured(1).toLongLong() * 1024LL;
    }

    QFile rollup(QStringLiteral("/proc/self/smaps_rollup"));
    if (rollup.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString text = QString::fromLatin1(rollup.readAll());
        qint64 privateKiB = 0;
        bool foundPrivate = false;
        const QRegularExpression expression(
            QStringLiteral("^Private_(?:Clean|Dirty):\\s+(\\d+)\\s+kB$"),
            QRegularExpression::MultilineOption);
        QRegularExpressionMatchIterator it = expression.globalMatch(text);
        while (it.hasNext()) {
            privateKiB += it.next().captured(1).toLongLong();
            foundPrivate = true;
        }
        if (foundPrivate)
            result.privateBytes = privateKiB * 1024LL;
    }
#elif defined(Q_OS_WIN)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
            sizeof(counters))) {
        result.residentBytes = qint64(counters.WorkingSetSize);
        result.privateBytes = qint64(counters.PrivateUsage);
    }
#endif

    return result;
}
