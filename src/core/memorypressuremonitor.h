#pragma once

#include <QObject>

#include <memory>

enum class MemoryPressureLevel {
    Warning,
    Critical
};

class MemoryPressureMonitor : public QObject
{
    Q_OBJECT
public:
    explicit MemoryPressureMonitor(QObject *parent = nullptr);
    ~MemoryPressureMonitor() override;

    bool isSupported() const { return m_supported; }

signals:
    void memoryPressure(MemoryPressureLevel level);

private:
    struct NativeState;
    std::unique_ptr<NativeState> m_native;
    bool m_supported = false;
};

Q_DECLARE_METATYPE(MemoryPressureLevel)
