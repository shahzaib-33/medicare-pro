#include "SoftwareUpdater.h"
#include "app/AppPaths.h"
#include "app/Version.h"
#include "db/Database.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSqlQuery>
#include <QVersionNumber>

namespace mcp {

QString SoftwareUpdater::markerPath() { return AppPaths::dataRoot() + "/pending_update.json"; }

bool SoftwareUpdater::isNewer(const UpdateItem &item)
{
    return QVersionNumber::fromString(item.version) > appVersionNumber();
}

QString SoftwareUpdater::prepare(const UpdateItem &item, const QString &installer, QString *err)
{
    const QString dir = AppPaths::backupDir() + "/before_update_" + item.version + "_"
        + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QDir().mkpath(dir);
    if (!Db::snapshot(Db::get(m_ph), dir + "/pharmacy.db", err)) return {};
    if (!Db::snapshot(Db::get(m_ref), dir + "/reference.db", err)) return {};
    // Keep the current installer if present so the previous version can be reinstalled.
    QSqlQuery q(Db::get(m_ph));
    q.prepare("INSERT INTO software_update_history(from_version, to_version, started_at, status, backup_path, message) "
              "VALUES(?,?,?,'installing',?,?)");
    q.addBindValue(appVersion()); q.addBindValue(item.version); q.addBindValue(Db::nowIso());
    q.addBindValue(dir); q.addBindValue(item.title);
    q.exec();
    QJsonObject m{{"from", appVersion()}, {"to", item.version}, {"backup", dir}, {"installer", installer},
                  {"history_id", q.lastInsertId().toLongLong()}, {"started", Db::nowIso()}};
    QFile f(markerPath());
    if (!f.open(QIODevice::WriteOnly)) { *err = "Cannot write update marker"; return {}; }
    f.write(QJsonDocument(m).toJson());
    return dir;
}

bool SoftwareUpdater::launchInstaller(const QString &installer, QString *err)
{
    // Inno Setup flags: show progress, close and restart MediCare Pro, keep a log.
    const QStringList args = {"/SILENT", "/SUPPRESSMSGBOXES", "/CLOSEAPPLICATIONS", "/RESTARTAPPLICATIONS",
                              "/LOG=" + QDir::toNativeSeparators(AppPaths::dataRoot() + "/update_install.log")};
    if (!QProcess::startDetached(installer, args)) { *err = "Could not start the installer"; return false; }
    return true;
}

StartupUpdateReport SoftwareUpdater::checkAfterStart()
{
    StartupUpdateReport rep;
    QSqlDatabase db = Db::get(m_ph);
    {
        QSqlQuery q(db);
        q.prepare("INSERT OR IGNORE INTO software_versions(version, first_run_at) VALUES(?,?)");
        q.addBindValue(appVersion()); q.addBindValue(Db::nowIso());
        q.exec();
    }
    QFile f(markerPath());
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return rep;
    const QJsonObject m = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    rep.hadPendingUpdate = true;
    rep.fromVersion = m.value("from").toString();
    rep.toVersion = m.value("to").toString();
    rep.backupDir = m.value("backup").toString();
    const qint64 started = QDateTime::fromString(m.value("started").toString(), Qt::ISODate).secsTo(QDateTime::currentDateTimeUtc());
    rep.succeeded = appVersion() == rep.toVersion;
    if (!rep.succeeded && started < 15 * 60 && appVersion() == rep.fromVersion) {
        // Installer may still be running. Leave the marker for the next start.
        rep.hadPendingUpdate = false;
        return rep;
    }
    rep.message = rep.succeeded
        ? QObject::tr("MediCare Pro was updated to version %1 and started successfully.").arg(rep.toVersion)
        : QObject::tr("The update to version %1 did not complete. You are still on version %2. "
                      "Your data backup is in %3. You can retry the update from the Update Center.")
              .arg(rep.toVersion, appVersion(), rep.backupDir);
    QSqlQuery q(db);
    q.prepare("UPDATE software_update_history SET finished_at=?, status=?, message=? WHERE id=?");
    q.addBindValue(Db::nowIso()); q.addBindValue(rep.succeeded ? "success" : "failed");
    q.addBindValue(rep.message); q.addBindValue(m.value("history_id").toInteger());
    q.exec();
    QFile::remove(markerPath());
    return rep;
}
}
