#pragma once
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

namespace mcp {
struct CatalogStats {
    int totalMedicines = 0;
    int activeRegistrations = 0;
    int inactiveRegistrations = 0;   // cancelled, suspended, discontinued
    int verified = 0;
    int unverified = 0;
    int sample = 0;
    int manufacturers = 0;
    int version = 0;
    QString lastUpdate;      // installed_at of newest version
    QString lastVerified;    // newest verified_at
    QString coverageNote;
    QStringList sources;

    static CatalogStats load(QSqlDatabase refDb);
    // Honest summary, never claims full coverage.
    QString coverageText() const;
};
}
