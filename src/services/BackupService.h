#pragma once
#include <QStringList>
#include "Common.h"

namespace mcp {

struct BackupInfo { QString path; QString createdAt; qint64 size = 0; QString kind; };

// Backups of the private pharmacy database. Each backup is a consistent SQLite copy
// with a SHA-256 checksum file next to it. Restore verifies both before replacing data.
namespace BackupService {
// kind: manual, auto, pre_restore, pre_update. targetDir empty = the app backup folder.
Result create(const QString &kind, const QString &targetDir = {}, QString *pathOut = nullptr);
// Checks the checksum, SQLite integrity and that the file is a MediCare Pro pharmacy database.
Result verify(const QString &path);
// Makes a safety backup of the current data first, then replaces it. All connections are reopened.
Result restore(const QString &path);
QList<BackupInfo> list(const QString &dir = {});
// Creates a daily automatic backup when the last one is older than a day. Keeps the newest keep files.
Result autoBackupIfDue(int keep = 14);
QString sha256File(const QString &path);
}
}
