#include "CatalogStats.h"
#include <QCoreApplication>
#include <QSqlQuery>

namespace mcp {
CatalogStats CatalogStats::load(QSqlDatabase db)
{
    CatalogStats s;
    QSqlQuery q(db);
    q.exec("SELECT COUNT(*), "
           "SUM(registration_status IN ('active','unknown')), "
           "SUM(registration_status IN ('cancelled','suspended','discontinued')), "
           "SUM(verification_status='verified'), SUM(verification_status='unverified'), "
           "SUM(verification_status='sample'), MAX(verified_at) FROM national_medicine_catalog");
    if (q.next()) {
        s.totalMedicines = q.value(0).toInt();
        s.activeRegistrations = q.value(1).toInt();
        s.inactiveRegistrations = q.value(2).toInt();
        s.verified = q.value(3).toInt();
        s.unverified = q.value(4).toInt();
        s.sample = q.value(5).toInt();
        s.lastVerified = q.value(6).toString();
    }
    q.exec("SELECT COUNT(*) FROM manufacturers_reference");
    if (q.next()) s.manufacturers = q.value(0).toInt();
    q.exec("SELECT version, installed_at, coverage_note FROM medicine_database_versions ORDER BY version DESC LIMIT 1");
    if (q.next()) {
        s.version = q.value(0).toInt();
        s.lastUpdate = q.value(1).toString();
        s.coverageNote = q.value(2).toString();
    }
    q.exec("SELECT name FROM medicine_data_sources ORDER BY id");
    while (q.next()) s.sources << q.value(0).toString();
    return s;
}

QString CatalogStats::coverageText() const
{
    if (totalMedicines == 0)
        return QCoreApplication::translate("CatalogStats", "No reference medicines are installed yet. Import an official dataset or install an update.");
    QString t = QCoreApplication::translate("CatalogStats", "%1 reference medicines from %2 source(s). %3 verified, %4 unverified, %5 sample.")
                    .arg(totalMedicines).arg(sources.size()).arg(verified).arg(unverified).arg(sample);
    t += ' ' + QCoreApplication::translate("CatalogStats", "This list may not include every medicine sold in Pakistan.");
    if (!coverageNote.isEmpty()) t += ' ' + coverageNote;
    return t;
}
}
