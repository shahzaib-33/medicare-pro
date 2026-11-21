#pragma once
#include "app/PerformanceProfile.h"
#include <QObject>

namespace mcp {
class UpdateClient;
class ConnectivityMonitor;

// Shared services handed to every page.
struct AppContext {
    PerformanceProfile perf;
    UpdateClient *updates = nullptr;
    ConnectivityMonitor *network = nullptr;
};

// Simple app wide signals so pages refresh after imports or updates.
class AppEvents : public QObject {
    Q_OBJECT
public:
    static AppEvents *instance() { static AppEvents e; return &e; }
signals:
    void referenceChanged();
    void pharmacyChanged();
    void navigate(int page);
    void signOutRequested();
};

namespace Pages {
enum {
    Dashboard, Pos, Sales, Medicines, Inventory, Purchases, Suppliers, Customers, Prescriptions, Expenses, Shifts, Reports,
    Catalog, MedicineUpdates, InfoUpdates, Users, Backup, Settings, Count
};
}
}
