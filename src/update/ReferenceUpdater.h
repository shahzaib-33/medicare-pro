#pragma once
#include <QSqlDatabase>
#include <QStringList>
#include <QVector>
#include "refdata/CatalogImporter.h"
#include "refdata/MedicineRecord.h"

namespace mcp {

struct StatusChange { QString refKey; QString status; QString details; QString sourceCode; };

// A reference data package published by the SoftzenAgency update service.
struct ReferencePackage {
    int baseVersion = 0;
    int targetVersion = 0;
    bool full = false;
    QString publishedAt;
    QString description;
    QString coverageNote;
    QVector<DataSourceInfo> sources;
    QVector<MedicineRecord> upserts;
    QVector<StatusChange> statusChanges;   // only from reliable official data
};

struct UpdatePreview {
    int added = 0, updated = 0, unchanged = 0, deactivated = 0;
    QStringList newBrands;     // first 200 for display
    QStringList changedBrands;
    QStringList inactiveBrands;
};

struct InstallResult {
    bool ok = false;
    bool rolledBack = false;
    int added = 0, updated = 0, unchanged = 0, deactivated = 0, flagged = 0, pricesChanged = 0;
    QString backupPath;
    QString error;
};

class ReferenceUpdater {
public:
    ReferenceUpdater(QString referenceConnection, QString pharmacyConnection)
        : m_ref(std::move(referenceConnection)), m_ph(std::move(pharmacyConnection)) {}

    // raw may be plain JSON or qCompress'ed JSON (.mcz).
    static bool parsePackage(const QByteArray &raw, ReferencePackage *out, QStringList *problems);
    static QByteArray encodePackage(const ReferencePackage &p, bool compress);

    UpdatePreview preview(const ReferencePackage &p) const;

    // Backup, one transaction, verify, flag affected local products. Restores the backup on any failure.
    // testFailAfterApply simulates a crash after records were written (used by tests).
    InstallResult install(const ReferencePackage &p, const QString &packageSha, bool testFailAfterApply = false);

    // Puts a backup file back as the reference database.
    bool restoreBackup(const QString &backupPath, QString *err);
    QStringList backups() const;

    // When on, a new official price in the update becomes the sale price of the linked medicine.
    // The new price applies to all stock, old and new, and goes up or down. Every change is in the audit log.
    void setApplyPrices(bool on) { m_applyPrices = on; }

private:
    QString m_ref, m_ph;
    bool m_applyPrices = false;
    void pruneBackups(int keep) const;
};
}
