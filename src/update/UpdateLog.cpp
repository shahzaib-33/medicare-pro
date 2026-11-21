#include "UpdateLog.h"
#include "db/Database.h"
#include <QSqlQuery>
#include <QVariant>

namespace mcp::UpdateLog {
void security(QSqlDatabase db, const QString &event, const QString &detail)
{
    QSqlQuery q(db);
    q.prepare("INSERT INTO update_security_logs(ts, event, detail) VALUES(?,?,?)");
    q.addBindValue(Db::nowIso()); q.addBindValue(event); q.addBindValue(detail);
    q.exec();
}

qint64 beginHistory(QSqlDatabase db, const QString &kind, int from, int to, const QString &backup)
{
    QSqlQuery q(db);
    q.prepare("INSERT INTO medicine_update_history(kind, from_version, to_version, started_at, status, backup_path) "
              "VALUES(?,?,?,?,'running',?)");
    q.addBindValue(kind); q.addBindValue(from); q.addBindValue(to); q.addBindValue(Db::nowIso()); q.addBindValue(backup);
    q.exec();
    return q.lastInsertId().toLongLong();
}

void finishHistory(QSqlDatabase db, qint64 id, const QString &status, int added, int updated, int deactivated,
                   int skipped, int flagged, const QString &message)
{
    QSqlQuery q(db);
    q.prepare("UPDATE medicine_update_history SET finished_at=?, status=?, added=?, updated=?, deactivated=?, "
              "skipped=?, flagged=?, message=? WHERE id=?");
    q.addBindValue(Db::nowIso()); q.addBindValue(status); q.addBindValue(added); q.addBindValue(updated);
    q.addBindValue(deactivated); q.addBindValue(skipped); q.addBindValue(flagged); q.addBindValue(message); q.addBindValue(id);
    q.exec();
}

void line(QSqlDatabase db, qint64 historyId, const QString &level, const QString &message)
{
    QSqlQuery q(db);
    q.prepare("INSERT INTO medicine_update_logs(history_id, ts, level, message) VALUES(?,?,?,?)");
    q.addBindValue(historyId); q.addBindValue(Db::nowIso()); q.addBindValue(level); q.addBindValue(message);
    q.exec();
}

void download(QSqlDatabase db, const QString &type, const QString &version, const QString &url, qint64 bytes,
              const QString &sha, bool ok, const QString &startedAt, const QString &message)
{
    QSqlQuery q(db);
    q.prepare("INSERT INTO update_download_history(item_type, version, url, bytes, sha256, ok, started_at, finished_at, message) "
              "VALUES(?,?,?,?,?,?,?,?,?)");
    q.addBindValue(type); q.addBindValue(version); q.addBindValue(url); q.addBindValue(bytes); q.addBindValue(sha);
    q.addBindValue(ok ? 1 : 0); q.addBindValue(startedAt); q.addBindValue(Db::nowIso()); q.addBindValue(message);
    q.exec();
}
}
