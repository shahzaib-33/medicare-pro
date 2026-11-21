#pragma once
#include <QSqlDatabase>
#include <QStringList>
#include "MedicineRecord.h"

namespace mcp {

struct DataSourceInfo {
    QString code;            // e.g. "drap-registered-products-2026-11"
    QString name;            // e.g. "DRAP registered product list, provided by DRAP"
    QString url;
    QString accessMethod;    // e.g. "Official file supplied by DRAP to SoftzenAgency"
    QString termsNote;
    QString retrievedAt;
};

struct ImportOptions {
    DataSourceInfo source;
    bool markVerified = false;   // admin confirms the file is an official verified dataset
    bool markSample = false;     // demonstration data, removed by the first real import
    bool dryRun = false;         // validate only
    QString coverageNote;        // e.g. "Human drugs from DRAP list dated 2026-10-31"
    int batchSize = 2000;
};

struct ImportIssue { int line; QString message; };

struct ImportReport {
    int sampleRemoved = 0;
    int rowsRead = 0, added = 0, updated = 0, unchanged = 0, skipped = 0, duplicates = 0;
    QList<ImportIssue> issues;
    int newVersion = 0;
    QString backupPath;
    QString error;
    bool ok = false;
};

// Writes reference records. Shared by the CSV importer and the update installer.
class CatalogWriter {
public:
    enum class Outcome { Added, Updated, Unchanged, Failed };
    struct Change { Outcome outcome; qint64 id; QString oldStatus; QString oldRegNo; QString oldHash; };

    explicit CatalogWriter(QSqlDatabase db) : m_db(db) {}
    Change upsert(const MedicineRecord &r, int version, QString *err);
    bool deactivate(const QString &refKey, const QString &newStatus, const QString &details,
                    const QString &sourceCode, int version, QString *oldStatus, QString *err);
    qint64 sourceId(const QString &code, const DataSourceInfo *info = nullptr);

private:
    QSqlDatabase m_db;
    QHash<QString, qint64> m_manuCache, m_catCache, m_srcCache;
    qint64 manufacturerId(const QString &name);
    qint64 categoryId(const QString &name);
    bool logEvent(qint64 medId, const QString &refKey, const QString &regNo, const QString &event,
                  const QString &oldS, const QString &newS, const QString &details, qint64 src, int version);
};

namespace CatalogImporter {
// Validates and parses one CSV row. Exposed for tests.
bool parseRow(const QHash<QString, int> &cols, const QStringList &row, const QString &sourceCode,
              const QString &verification, MedicineRecord *out, QString *problem);
QHash<QString, int> mapHeader(const QStringList &header, QStringList *unknown);

// Full import of an administrator supplied official CSV file into reference.db.
// Creates a safety backup first and runs in one transaction. On failure nothing changes.
ImportReport importCsv(QSqlDatabase refDb, const QString &csvPath, const ImportOptions &opt);

int currentVersion(QSqlDatabase refDb);
}
}
