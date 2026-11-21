#include "UpdateClient.h"
#include "Signature.h"
#include "UpdateLog.h"
#include "app/AppPaths.h"
#include "app/Version.h"
#include "db/Database.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QSettings>
#include <QSqlQuery>

namespace mcp {

UpdateClient::UpdateClient(QString ph, UpdateConfig cfg, QObject *parent)
    : QObject(parent), m_ph(std::move(ph)), m_cfg(std::move(cfg))
{
    QSettings s(AppPaths::settingsFile(), QSettings::IniFormat);
    m_lastCheck = s.value("updates/lastCheck").toDateTime();
    m_nam.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    m_nam.setTransferTimeout(60000);
    // Hourly tick decides if a check is due. Cheap, and works after sleep or offline periods.
    m_timer.setInterval(60 * 60 * 1000);
    connect(&m_timer, &QTimer::timeout, this, &UpdateClient::maybeAutoCheck);
    m_timer.start();
    // First automatic check runs a while after start so it never slows the startup.
    QTimer::singleShot(90 * 1000, this, &UpdateClient::maybeAutoCheck);
}

void UpdateClient::setConfig(const UpdateConfig &cfg) { m_cfg = cfg; }

void UpdateClient::maybeAutoCheck()
{
    if (!m_cfg.autoCheck || m_cfg.manifestUrl.isEmpty()) return;
    if (m_lastCheck.isValid() && m_lastCheck.secsTo(QDateTime::currentDateTime()) < m_cfg.checkIntervalHours * 3600) return;
    checkNow();
}

bool UpdateClient::urlAllowed(const QUrl &url) const
{
    return url.scheme() == "https" && m_cfg.allowedHosts.contains(url.host(), Qt::CaseInsensitive);
}

QNetworkRequest UpdateClient::request(const QUrl &url) const
{
    QNetworkRequest r(url);
    r.setHeader(QNetworkRequest::UserAgentHeader, "MediCarePro/" + appVersion());
    r.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    return r;
}

qint64 UpdateClient::lastSequence() const
{
    QSqlQuery q(Db::get(m_ph));
    q.exec("SELECT COALESCE(MAX(sequence),0) FROM update_manifests WHERE signature_ok=1");
    return q.next() ? q.value(0).toLongLong() : 0;
}

bool UpdateClient::verifyManifest(const QByteArray &body, const QByteArray &sigB64, const QByteArray &pubKey,
                                  qint64 lastSeq, UpdateManifest *out, QString *err)
{
    const QByteArray sig = QByteArray::fromBase64(sigB64.trimmed());
    if (!Signature::verify(pubKey, body, sig)) { *err = "Manifest signature is not valid"; return false; }
    UpdateManifest m;
    if (!UpdateManifest::parse(body, &m, err)) return false;
    if (m.sequence < lastSeq) { *err = "Manifest is older than one already seen (possible replay)"; return false; }
    if (m.expiresAt.isValid() && m.expiresAt < QDateTime::currentDateTimeUtc()) { *err = "Manifest has expired"; return false; }
    *out = m;
    return true;
}

bool UpdateClient::verifyPackageFile(const UpdateItem &item, const QString &path, const QByteArray &pubKey, QString *err)
{
    qint64 size = 0;
    const QString sha = Signature::sha256File(path, &size);
    if (size != item.size) { *err = "Downloaded size does not match"; return false; }
    if (sha != item.sha256) { *err = "Downloaded file hash does not match"; return false; }
    if (!Signature::verify(pubKey, Signature::packageStatement(item.type, item.version, sha), item.signature)) {
        *err = "Package signature is not valid";
        return false;
    }
    return true;
}

void UpdateClient::checkNow()
{
    const QUrl url(m_cfg.manifestUrl);
    if (!urlAllowed(url)) { emit checkFinished(false, tr("Update address is not trusted.")); return; }
    if (m_reply) return;
    emit checkStarted();
    QNetworkReply *body = m_nam.get(request(url));
    m_reply = body;
    connect(body, &QNetworkReply::finished, this, [this, body, url] {
        body->deleteLater();
        m_reply = nullptr;
        if (body->error() != QNetworkReply::NoError) {
            emit checkFinished(false, tr("Could not reach the update service. Working offline."));
            return;
        }
        const QByteArray manifest = body->readAll().left(4 * 1024 * 1024);
        QNetworkReply *sigReply = m_nam.get(request(QUrl(m_cfg.manifestUrl + ".sig")));
        connect(sigReply, &QNetworkReply::finished, this, [this, sigReply, manifest, url] {
            sigReply->deleteLater();
            const QByteArray sig = sigReply->readAll().left(1024);
            QString err;
            UpdateManifest m;
            const bool ok = sigReply->error() == QNetworkReply::NoError
                && verifyManifest(manifest, sig, m_cfg.publicKeyPem, lastSequence(), &m, &err);
            QSqlQuery q(Db::get(m_ph));
            q.prepare("INSERT INTO update_manifests(fetched_at, url, sequence, sha256, signature_ok, body) VALUES(?,?,?,?,?,?)");
            q.addBindValue(Db::nowIso()); q.addBindValue(url.toString()); q.addBindValue(m.sequence);
            q.addBindValue(Signature::sha256(manifest)); q.addBindValue(ok ? 1 : 0);
            q.addBindValue(ok ? QString::fromUtf8(manifest) : QString());
            q.exec();
            if (!ok) {
                UpdateLog::security(Db::get(m_ph), "manifest_rejected", err);
                emit checkFinished(false, tr("Update information was rejected: %1").arg(err));
                return;
            }
            for (const auto &item : m.items) {
                if (!urlAllowed(QUrl(item.url))) {
                    UpdateLog::security(Db::get(m_ph), "untrusted_url", item.url);
                    emit checkFinished(false, tr("Update list contains an untrusted address."));
                    return;
                }
            }
            m_manifest = m;
            m_lastCheck = QDateTime::currentDateTime();
            QSettings s(AppPaths::settingsFile(), QSettings::IniFormat);
            s.setValue("updates/lastCheck", m_lastCheck);
            emit checkFinished(true, tr("Update information received."));
        });
    });
}

void UpdateClient::download(const UpdateItem &item)
{
    if (m_reply) return;
    const QUrl url(item.url);
    if (!urlAllowed(url)) { emit downloadFinished(item, {}, false, tr("Update address is not trusted.")); return; }
    const QString started = Db::nowIso();
    const QString path = AppPaths::downloadDir() + '/' + item.type + '-' + item.version + ".part";
    auto *file = new QFile(path, this);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        delete file;
        emit downloadFinished(item, {}, false, tr("Cannot write to the download folder."));
        return;
    }
    QNetworkReply *r = m_nam.get(request(url));
    m_reply = r;
    connect(r, &QNetworkReply::readyRead, this, [r, file, item] {
        file->write(r->readAll());
        if (file->size() > item.size) r->abort();   // never accept more than announced
    });
    connect(r, &QNetworkReply::downloadProgress, this, &UpdateClient::downloadProgress);
    connect(r, &QNetworkReply::finished, this, [this, r, file, item, path, started] {
        file->write(r->readAll());
        file->close();
        file->deleteLater();
        r->deleteLater();
        m_reply = nullptr;
        // Redirects must stay on trusted hosts.
        if (r->error() != QNetworkReply::NoError || !urlAllowed(r->url())) {
            QFile::remove(path);
            UpdateLog::download(Db::get(m_ph), item.type, item.version, item.url, 0, {}, false, started, r->errorString());
            emit downloadFinished(item, {}, false, tr("Download failed: %1").arg(r->errorString()));
            return;
        }
        QString err;
        const bool ok = verifyPackageFile(item, path, m_cfg.publicKeyPem, &err);
        UpdateLog::download(Db::get(m_ph), item.type, item.version, item.url, QFileInfo(path).size(), item.sha256, ok,
                            started, ok ? "verified" : err);
        if (!ok) {
            QFile::remove(path);
            UpdateLog::security(Db::get(m_ph), "package_rejected", item.id + ": " + err);
            emit downloadFinished(item, {}, false, tr("Downloaded file failed verification and was deleted."));
            return;
        }
        const QString finalPath = path.left(path.size() - 5) + (item.type == "software" ? ".exe" : ".pkg");
        QFile::remove(finalPath);
        QFile::rename(path, finalPath);
        emit downloadFinished(item, finalPath, true, tr("Downloaded and verified."));
    });
}

void UpdateClient::cancel()
{
    if (m_reply) m_reply->abort();
}
}
