#include "ConnectivityMonitor.h"
#include <QNetworkInformation>
#include <QNetworkInterface>

namespace mcp {

ConnectivityMonitor::ConnectivityMonitor(QObject *parent) : QObject(parent)
{
    if (QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability)) {
        if (auto *ni = QNetworkInformation::instance()) {
            m_useNetInfo = true;
            connect(ni, &QNetworkInformation::reachabilityChanged, this, &ConnectivityMonitor::evaluate);
        }
    }
    // Fallback for systems without a backend: check interfaces every 30 seconds. Very cheap.
    m_poll.setInterval(30000);
    connect(&m_poll, &QTimer::timeout, this, &ConnectivityMonitor::evaluate);
    m_poll.start();
    evaluate();
}

void ConnectivityMonitor::setForcedOffline(bool off)
{
    m_forcedOffline = off;
    evaluate();
}

void ConnectivityMonitor::evaluate()
{
    if (m_forcedOffline) { setOnline(false); return; }
    if (m_useNetInfo) {
        const auto r = QNetworkInformation::instance()->reachability();
        if (r != QNetworkInformation::Reachability::Unknown) {
            setOnline(r == QNetworkInformation::Reachability::Online);
            return;
        }
    }
    bool any = false;
    for (const QNetworkInterface &i : QNetworkInterface::allInterfaces()) {
        const auto f = i.flags();
        if (f.testFlag(QNetworkInterface::IsUp) && f.testFlag(QNetworkInterface::IsRunning)
            && !f.testFlag(QNetworkInterface::IsLoopBack) && !i.addressEntries().isEmpty()) { any = true; break; }
    }
    setOnline(any);
}

void ConnectivityMonitor::setOnline(bool on)
{
    if (on == m_online) return;
    m_online = on;
    emit onlineChanged(on);
}
}
