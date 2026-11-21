#include "BackupService.h"
#include "AuthService.h"
#include "app/AppPaths.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

namespace mcp::BackupService {

static const char *kPrefix = "MediCare_Backup_";
static const char *kSuffix = ".mcbak";

QString sha256File(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f)) return {};
    return QString::fromLatin1(h.result().toHex());
}

static void record(const QString &kind, const QString &path, bool ok, const QString &msg)
{
    try {
        const QFileInfo fi(path);
        exec("INSERT INTO backups(ts, path, size, sha256, kind, ok, message) VALUES(?,?,?,?,?,?,?)",
             {Db::nowIso(), path, fi.exists() ? fi.size() : 0, ok ? sha256File(path) : QString(), kind, ok ? 1 : 0, nullIf(msg)});
    } catch (const ServiceError &) {
    }
}

Result create(const QString &kind, const QString &targetDir, QString *pathOut)
{
    if (kind != "auto" && kind != "pre_restore" && !Session::instance().can("backup.manage"))
        return Result::fail(QObject::tr("You do not have permission to do this."));
    const QString dir = targetDir.isEmpty() ? AppPaths::backupDir() : targetDir;
    if (!QDir().mkpath(dir)) return Result::fail(QObject::tr("Cannot write to the backup folder."));
    const QString name = kPrefix + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz") + "_" + kind + kSuffix;
    const QString path = QDir(dir).filePath(name);
    QString err;
    if (!Db::snapshot(pharmacyDb(), path, &err)) {
        record(kind, path, false, err);
        return Result::fail(QObject::tr("Backup failed: %1").arg(err));
    }
    const QString sum = sha256File(path);
    QFile s(path + ".sha256");
    if (!s.open(QIODevice::WriteOnly) || s.write(sum.toLatin1()) < 0) {
        QFile::remove(path);
        return Result::fail(QObject::tr("Backup failed: %1").arg(s.errorString()));
    }
    s.close();
    record(kind, path, true, {});
    if (kind == "manual") audit("backup_created", "backup", 0, path);
    if (pathOut) *pathOut = path;
    return Result::success();
}

Result verify(const QString &path)
{
    if (!QFileInfo::exists(path)) return Result::fail(QObject::tr("Backup file not found."));
    QFile s(path + ".sha256");
    if (s.open(QIODevice::ReadOnly)) {
        const QString expected = QString::fromLatin1(s.readAll()).trimmed();
        if (!expected.isEmpty() && expected != sha256File(path))
            return Result::fail(QObject::tr("The backup file is damaged. Its checksum does not match."));
    }
    const QString conn = "verify_" + QUuid::createUuid().toString(QUuid::Id128);
    Result r = Result::success();
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
        db.setDatabaseName(path);
        db.setConnectOptions("QSQLITE_OPEN_READONLY");
        if (!db.open()) {
            r = Result::fail(QObject::tr("The backup file cannot be opened."));
        } else {
            QSqlQuery q(db);
            if (!q.exec("PRAGMA integrity_check") || !q.next() || q.value(0).toString() != "ok")
                r = Result::fail(QObject::tr("The backup file failed the integrity check."));
            else if (!q.exec("SELECT COUNT(*) FROM users") || !q.next())
                r = Result::fail(QObject::tr("This file is not a MediCare Pro backup."));
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(conn);
    return r;
}

Result restore(const QString &path)
{
    if (!Session::instance().can("backup.manage")) return Result::fail(QObject::tr("You do not have permission to do this."));
    if (Result v = verify(path); !v.ok) return v;
    QString safety;
    if (Result b = create("pre_restore", {}, &safety); !b.ok) return b;
    audit("backup_restore_started", "backup", 0, path);

    const QString live = Db::pathOf(Db::Pharmacy);
    const QString staged = live + ".restore";
    QFile::remove(staged);
    if (!QFile::copy(path, staged)) return Result::fail(QObject::tr("Could not copy the backup file."));
    Db::close(Db::Pharmacy);
    const auto swapIn = [&](const QString &from) {
        for (const QString &ext : {QString(), QString("-wal"), QString("-shm")}) QFile::remove(live + ext);
        return QFile::copy(from, live);
    };
    QString err;
    bool ok = swapIn(staged);
    QFile::remove(staged);
    if (ok) ok = Db::reopen(Db::Pharmacy, &err) && Db::migratePharmacy(pharmacyDb(), &err);
    if (!ok) {
        // Put the safety copy back so the pharmacy keeps working.
        Db::close(Db::Pharmacy);
        swapIn(safety);
        QString e2;
        Db::reopen(Db::Pharmacy, &e2);
        return Result::fail(QObject::tr("Restore failed. Your previous data was kept. %1").arg(err));
    }
    record("restored", path, true, QObject::tr("Restored. Safety copy: %1").arg(safety));
    audit("backup_restored", "backup", 0, path);
    return Result::success();
}

QList<BackupInfo> list(const QString &dir)
{
    QList<BackupInfo> out;
    QDir d(dir.isEmpty() ? AppPaths::backupDir() : dir);
    const auto files = d.entryInfoList({QString(kPrefix) + "*" + kSuffix}, QDir::Files, QDir::Time);
    for (const QFileInfo &fi : files) {
        const QString base = fi.completeBaseName();
        const QString kind = base.section('_', 5);
        out.append({fi.absoluteFilePath(), fi.lastModified().toString("yyyy-MM-dd HH:mm"), fi.size(), kind});
    }
    return out;
}

Result autoBackupIfDue(int keep)
{
    const QString last = scalar("SELECT MAX(ts) FROM backups WHERE kind='auto' AND ok=1", {}, QString()).toString();
    const QDateTime lastTs = QDateTime::fromString(last, Qt::ISODate);
    if (lastTs.isValid() && lastTs.secsTo(QDateTime::currentDateTimeUtc()) < 20 * 3600) return Result::success();
    Result r = create("auto");
    if (!r.ok) return r;
    int autos = 0;
    for (const BackupInfo &b : list()) {
        if (b.kind != "auto") continue;
        if (++autos > keep) { QFile::remove(b.path); QFile::remove(b.path + ".sha256"); }
    }
    return r;
}
}
