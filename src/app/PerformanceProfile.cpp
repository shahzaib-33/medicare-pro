#include "PerformanceProfile.h"
#include <QFile>
#include <QThread>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace mcp {
static qint64 detectRamMb()
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX st{};
    st.dwLength = sizeof(st);
    if (GlobalMemoryStatusEx(&st))
        return static_cast<qint64>(st.ullTotalPhys / (1024 * 1024));
    return 0;
#else
    QFile f("/proc/meminfo");
    if (f.open(QIODevice::ReadOnly)) {
        const QByteArray line = f.readLine();   // MemTotal: 16318640 kB
        const QList<QByteArray> parts = line.simplified().split(' ');
        if (parts.size() >= 2)
            return parts[1].toLongLong() / 1024;
    }
    return 0;
#endif
}

PerformanceProfile PerformanceProfile::detect(Mode override)
{
    PerformanceProfile p;
    p.m_ramMb = detectRamMb();
    p.m_threads = qMax(1, QThread::idealThreadCount());
    switch (override) {
    case Mode::Lite: p.m_lite = true; break;
    case Mode::Standard: p.m_lite = false; break;
    case Mode::Auto:
        p.m_lite = (p.m_ramMb > 0 && p.m_ramMb <= 4096) || p.m_threads <= 2;
        break;
    }
    return p;
}

QString PerformanceProfile::describe() const
{
    return QStringLiteral("%1 mode, RAM %2 MB, %3 CPU threads")
        .arg(m_lite ? "Lite" : "Standard").arg(m_ramMb).arg(m_threads);
}
}
