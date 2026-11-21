#pragma once
#include <QObject>
#include <QTimer>

namespace mcp {
// Reports online or offline. Never blocks the UI and never stops local work.
class ConnectivityMonitor : public QObject {
    Q_OBJECT
public:
    explicit ConnectivityMonitor(QObject *parent = nullptr);
    bool isOnline() const { return m_online; }
    void setForcedOffline(bool off);   // for testing and for pharmacies that want no network use

signals:
    void onlineChanged(bool online);

private:
    bool m_online = false;
    bool m_forcedOffline = false;
    bool m_useNetInfo = false;
    QTimer m_poll;
    void evaluate();
    void setOnline(bool on);
};
}
