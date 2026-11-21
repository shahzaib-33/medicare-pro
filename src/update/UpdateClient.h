#pragma once
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include "UpdateManifest.h"

class QNetworkReply;

namespace mcp {

struct UpdateConfig {
    QString manifestUrl;          // https://updates.<softzen domain>/medicarepro/stable/manifest.json
    QByteArray publicKeyPem;      // Ed25519 public key compiled into the app
    QStringList allowedHosts;     // downloads only from these hosts
    int checkIntervalHours = 24;  // automatic check, once daily by default
    bool autoCheck = true;
    bool autoInstallReference = true;   // reference data only. Software is never auto installed.
};

// Talks to the central SoftzenAgency update service. Sends no pharmacy data:
// requests carry only the app version in the User-Agent.
class UpdateClient : public QObject {
    Q_OBJECT
public:
    UpdateClient(QString pharmacyConnection, UpdateConfig cfg, QObject *parent = nullptr);

    const UpdateConfig &config() const { return m_cfg; }
    void setConfig(const UpdateConfig &cfg);
    const UpdateManifest &manifest() const { return m_manifest; }
    bool hasManifest() const { return m_manifest.sequence > 0; }
    QDateTime lastCheck() const { return m_lastCheck; }

    // Pure checks, used by the network flow and by tests.
    static bool verifyManifest(const QByteArray &body, const QByteArray &sigB64, const QByteArray &pubKey,
                               qint64 lastSequence, UpdateManifest *out, QString *err);
    static bool verifyPackageFile(const UpdateItem &item, const QString &path, const QByteArray &pubKey, QString *err);
    bool urlAllowed(const QUrl &url) const;

public slots:
    void checkNow();
    void download(const UpdateItem &item);
    void cancel();

signals:
    void checkStarted();
    void checkFinished(bool ok, const QString &message);
    void downloadProgress(qint64 received, qint64 total);
    void downloadFinished(const UpdateItem &item, const QString &path, bool ok, const QString &message);

private:
    QString m_ph;
    UpdateConfig m_cfg;
    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_reply;
    UpdateManifest m_manifest;
    QDateTime m_lastCheck;
    QTimer m_timer;
    QNetworkRequest request(const QUrl &url) const;
    qint64 lastSequence() const;
    void maybeAutoCheck();
};
}
