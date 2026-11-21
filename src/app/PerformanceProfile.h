#pragma once
#include <QString>

namespace mcp {
// Picks sensible settings for the PC the app runs on.
// Low end: 4 GB RAM or less, or 2 CPU threads or less.
class PerformanceProfile {
public:
    enum class Mode { Auto, Lite, Standard };

    static PerformanceProfile detect(Mode override = Mode::Auto);

    bool lite() const { return m_lite; }
    qint64 totalRamMb() const { return m_ramMb; }
    int cpuThreads() const { return m_threads; }

    // Tunables used across the app.
    int sqliteCacheKb() const { return m_lite ? 4096 : 32768; }
    int sqliteMmapMb() const { return m_lite ? 0 : 128; }
    int searchPageSize() const { return m_lite ? 100 : 300; }
    int searchDebounceMs() const { return m_lite ? 350 : 180; }
    bool animations() const { return !m_lite; }
    int importBatchSize() const { return m_lite ? 500 : 2000; }
    QString describe() const;

private:
    bool m_lite = false;
    qint64 m_ramMb = 0;
    int m_threads = 1;
};
}
