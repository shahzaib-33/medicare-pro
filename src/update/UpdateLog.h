#pragma once
#include <QSqlDatabase>
#include <QString>

namespace mcp::UpdateLog {
void security(QSqlDatabase pharmacyDb, const QString &event, const QString &detail);
qint64 beginHistory(QSqlDatabase pharmacyDb, const QString &kind, int fromVersion, int toVersion, const QString &backup);
void finishHistory(QSqlDatabase pharmacyDb, qint64 id, const QString &status, int added, int updated,
                   int deactivated, int skipped, int flagged, const QString &message);
void line(QSqlDatabase pharmacyDb, qint64 historyId, const QString &level, const QString &message);
void download(QSqlDatabase pharmacyDb, const QString &type, const QString &version, const QString &url,
              qint64 bytes, const QString &sha, bool ok, const QString &startedAt, const QString &message);
}
