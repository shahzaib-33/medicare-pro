#pragma once
#include <QSqlDatabase>
#include <QString>

namespace mcp {
class PerformanceProfile;

// Two separate SQLite files:
//   pharmacy.db  - the pharmacy's own private data (medicines, stock, sales, update history)
//   reference.db - the national medicine reference catalog, replaced only by verified updates
// They are never mixed, so a reference update can never touch inventory.
namespace Db {
inline constexpr const char *Pharmacy = "pharmacy";
inline constexpr const char *Reference = "reference";

// Opens a connection with tuned pragmas. Returns false and fills err on failure.
bool open(const QString &connection, const QString &path, const PerformanceProfile &perf, QString *err);
void close(const QString &connection);
// Closes and opens again with the same file and settings (used after a file restore).
bool reopen(const QString &connection, QString *err);
QString pathOf(const QString &connection);
QSqlDatabase get(const QString &connection);

// Opens a private clone of an existing connection for a worker thread.
QSqlDatabase cloneForThread(const QString &connection, const QString &suffix);

// Runs migrations. Safe to run on every start.
bool migratePharmacy(QSqlDatabase db, QString *err);
bool migrateReference(QSqlDatabase db, QString *err);

// Consistent online copy of a database file using VACUUM INTO.
bool snapshot(QSqlDatabase db, const QString &targetPath, QString *err);

// Executes a batch of ';' separated statements.
bool execScript(QSqlDatabase db, const QString &sql, QString *err);
QString nowIso();
}
}
