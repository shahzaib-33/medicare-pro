#pragma once
#include <QByteArray>
#include <QString>

namespace mcp {
// Ed25519 signatures through OpenSSL. The app only holds the PUBLIC key.
// The private key stays offline with SoftzenAgency and is used by the refpack tool.
namespace Signature {
bool verify(const QByteArray &publicKeyPem, const QByteArray &message, const QByteArray &signature);
QByteArray sign(const QByteArray &privateKeyPem, const QByteArray &message, QString *err);
bool generateKeyPair(QByteArray *privatePem, QByteArray *publicPem, QString *err);

// The exact text that a package signature covers. Binds type, version and hash together
// so a valid file cannot be replayed as a different update.
QByteArray packageStatement(const QString &type, const QString &version, const QString &sha256Hex);

QString sha256File(const QString &path, qint64 *size = nullptr);
QString sha256(const QByteArray &data);
}
}
