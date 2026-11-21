#include "Signature.h"
#include <QCryptographicHash>
#include <QFile>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <memory>

namespace mcp::Signature {

namespace {
struct BioDel { void operator()(BIO *b) const { BIO_free(b); } };
struct KeyDel { void operator()(EVP_PKEY *k) const { EVP_PKEY_free(k); } };
struct CtxDel { void operator()(EVP_MD_CTX *c) const { EVP_MD_CTX_free(c); } };
}

bool verify(const QByteArray &pubPem, const QByteArray &msg, const QByteArray &sig)
{
    if (pubPem.isEmpty() || sig.size() != 64) return false;
    std::unique_ptr<BIO, BioDel> bio(BIO_new_mem_buf(pubPem.constData(), int(pubPem.size())));
    std::unique_ptr<EVP_PKEY, KeyDel> key(PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr));
    if (!key || EVP_PKEY_id(key.get()) != EVP_PKEY_ED25519) return false;
    std::unique_ptr<EVP_MD_CTX, CtxDel> ctx(EVP_MD_CTX_new());
    if (EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) != 1) return false;
    return EVP_DigestVerify(ctx.get(), reinterpret_cast<const unsigned char *>(sig.constData()), size_t(sig.size()),
                            reinterpret_cast<const unsigned char *>(msg.constData()), size_t(msg.size())) == 1;
}

QByteArray sign(const QByteArray &privPem, const QByteArray &msg, QString *err)
{
    std::unique_ptr<BIO, BioDel> bio(BIO_new_mem_buf(privPem.constData(), int(privPem.size())));
    std::unique_ptr<EVP_PKEY, KeyDel> key(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
    if (!key || EVP_PKEY_id(key.get()) != EVP_PKEY_ED25519) { if (err) *err = "Not an Ed25519 private key"; return {}; }
    std::unique_ptr<EVP_MD_CTX, CtxDel> ctx(EVP_MD_CTX_new());
    if (EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) != 1) { if (err) *err = "sign init failed"; return {}; }
    size_t len = 64;
    QByteArray out(64, 0);
    if (EVP_DigestSign(ctx.get(), reinterpret_cast<unsigned char *>(out.data()), &len,
                       reinterpret_cast<const unsigned char *>(msg.constData()), size_t(msg.size())) != 1) {
        if (err) *err = "sign failed";
        return {};
    }
    out.resize(int(len));
    return out;
}

bool generateKeyPair(QByteArray *privPem, QByteArray *pubPem, QString *err)
{
    EVP_PKEY *raw = nullptr;
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    if (!pctx || EVP_PKEY_keygen_init(pctx) != 1 || EVP_PKEY_keygen(pctx, &raw) != 1) {
        EVP_PKEY_CTX_free(pctx);
        if (err) *err = "keygen failed";
        return false;
    }
    EVP_PKEY_CTX_free(pctx);
    std::unique_ptr<EVP_PKEY, KeyDel> key(raw);
    auto dump = [](auto writer) {
        std::unique_ptr<BIO, BioDel> b(BIO_new(BIO_s_mem()));
        writer(b.get());
        char *data = nullptr;
        const long n = BIO_get_mem_data(b.get(), &data);
        return QByteArray(data, int(n));
    };
    *privPem = dump([&](BIO *b) { PEM_write_bio_PrivateKey(b, key.get(), nullptr, nullptr, 0, nullptr, nullptr); });
    *pubPem = dump([&](BIO *b) { PEM_write_bio_PUBKEY(b, key.get()); });
    return !privPem->isEmpty() && !pubPem->isEmpty();
}

QByteArray packageStatement(const QString &type, const QString &version, const QString &sha)
{
    return QStringLiteral("medicarepro-package:v1:%1:%2:%3").arg(type, version, sha.toLower()).toUtf8();
}

QString sha256(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QString sha256File(const QString &path, qint64 *size)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    if (size) *size = f.size();
    return QString::fromLatin1(h.result().toHex());
}
}
