#include "Database.h"
#include "Schema.h"
#include "app/PerformanceProfile.h"
#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QRegularExpression>

namespace mcp::Db {

QString nowIso() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODate); }

static bool pragma(QSqlDatabase &db, const QString &sql, QString *err)
{
    QSqlQuery q(db);
    if (!q.exec(sql)) {
        if (err) *err = sql + ": " + q.lastError().text();
        return false;
    }
    return true;
}

struct OpenInfo { QString path; int cacheKb; int mmapMb; };
static QHash<QString, OpenInfo> &infos() { static QHash<QString, OpenInfo> h; return h; }

static bool openRaw(const QString &connection, const QString &path, int cacheKb, int mmapMb, QString *err);

bool open(const QString &connection, const QString &path, const PerformanceProfile &perf, QString *err)
{
    return openRaw(connection, path, perf.sqliteCacheKb(), perf.sqliteMmapMb(), err);
}

bool reopen(const QString &connection, QString *err)
{
    const OpenInfo i = infos().value(connection);
    if (i.path.isEmpty()) { if (err) *err = "connection was never opened"; return false; }
    return openRaw(connection, i.path, i.cacheKb, i.mmapMb, err);
}

QString pathOf(const QString &connection) { return infos().value(connection).path; }

static bool openRaw(const QString &connection, const QString &path, int cacheKb, int mmapMb, QString *err)
{
    infos().insert(connection, {path, cacheKb, mmapMb});
    if (QSqlDatabase::contains(connection))
        close(connection);
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
    db.setDatabaseName(path);
    db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
    if (!db.open()) {
        if (err) *err = db.lastError().text();
        return false;
    }
    // WAL keeps reads fast while writing and survives power cuts well.
    return pragma(db, "PRAGMA journal_mode=WAL", err)
        && pragma(db, "PRAGMA synchronous=NORMAL", err)
        && pragma(db, "PRAGMA foreign_keys=ON", err)
        && pragma(db, "PRAGMA temp_store=MEMORY", err)
        && pragma(db, QStringLiteral("PRAGMA cache_size=-%1").arg(cacheKb), err)
        && pragma(db, QStringLiteral("PRAGMA mmap_size=%1").arg(qint64(mmapMb) * 1024 * 1024), err);
}

void close(const QString &connection)
{
    {
        QSqlDatabase db = QSqlDatabase::database(connection, false);
        if (db.isOpen()) db.close();
    }
    QSqlDatabase::removeDatabase(connection);
}

QSqlDatabase get(const QString &connection) { return QSqlDatabase::database(connection, false); }

QSqlDatabase cloneForThread(const QString &connection, const QString &suffix)
{
    const QString name = connection + "_" + suffix + "_"
        + QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    if (QSqlDatabase::contains(name))
        return QSqlDatabase::database(name);
    QSqlDatabase db = QSqlDatabase::cloneDatabase(connection, name);
    db.open();
    QSqlQuery q(db);
    q.exec("PRAGMA foreign_keys=ON");
    return db;
}

bool execScript(QSqlDatabase db, const QString &sql, QString *err)
{
    // Split on ';' at line ends. Our schema scripts never contain ';' inside strings.
    const QStringList parts = sql.split(QRegularExpression(";\\s*\\n"), Qt::SkipEmptyParts);
    for (QString stmt : parts) {
        // drop comment lines
        QStringList lines;
        for (const QString &l : stmt.split('\n'))
            if (!l.trimmed().startsWith("--")) lines << l;
        stmt = lines.join('\n').trimmed();
        if (stmt.endsWith(';')) stmt.chop(1);
        if (stmt.isEmpty()) continue;
        QSqlQuery q(db);
        if (!q.exec(stmt)) {
            if (err) *err = q.lastError().text() + "\n" + stmt.left(200);
            return false;
        }
    }
    return true;
}

static bool migrate(QSqlDatabase db, const QStringList &steps, QString *err)
{
    QSqlQuery q(db);
    q.exec("PRAGMA user_version");
    const int current = q.next() ? q.value(0).toInt() : 0;
    for (int v = current; v < steps.size(); ++v) {
        if (!db.transaction()) { if (err) *err = db.lastError().text(); return false; }
        if (!execScript(db, steps[v], err)) { db.rollback(); return false; }
        QSqlQuery u(db);
        if (!u.exec(QStringLiteral("PRAGMA user_version=%1").arg(v + 1))) {
            db.rollback();
            if (err) *err = u.lastError().text();
            return false;
        }
        if (!db.commit()) { if (err) *err = db.lastError().text(); return false; }
    }
    return true;
}

bool migratePharmacy(QSqlDatabase db, QString *err) { return migrate(db, Schema::pharmacyMigrations(), err); }
bool migrateReference(QSqlDatabase db, QString *err) { return migrate(db, Schema::referenceMigrations(), err); }

bool snapshot(QSqlDatabase db, const QString &targetPath, QString *err)
{
    QFile::remove(targetPath);
    QSqlQuery q(db);
    q.prepare("VACUUM INTO ?");
    q.addBindValue(targetPath);
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return false;
    }
    return true;
}
}
