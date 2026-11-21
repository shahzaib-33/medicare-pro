#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

namespace mcp {

struct UpdateItem {
    QString id;
    QString type;            // reference_delta, reference_full, software, translations, receipt_templates
    QString version;
    QString baseVersion;     // for reference_delta
    QString title;
    QString description;
    QString publishedAt;
    QStringList components;  // affected components
    QStringList notes;       // features and fixes (software)
    bool major = false;
    QString minAppVersion;
    QString url;
    QString sha256;
    qint64 size = 0;
    QByteArray signature;    // over Signature::packageStatement(type, version, sha256)
};

struct UpdateManifest {
    QString format;
    QString publisher;
    qint64 sequence = 0;     // grows with every publication. Older manifests are rejected.
    QDateTime generatedAt;
    QDateTime expiresAt;
    QVector<UpdateItem> items;

    // Parses and checks structure. Signature is checked separately on the raw bytes.
    static bool parse(const QByteArray &json, UpdateManifest *out, QString *err);
    const UpdateItem *latest(const QString &type) const;
};
}
