#include "UpdateManifest.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>
#include <QVersionNumber>

namespace mcp {

static const QStringList kTypes = {"reference_delta", "reference_full", "software", "translations", "receipt_templates"};

bool UpdateManifest::parse(const QByteArray &json, UpdateManifest *m, QString *err)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) { *err = "Manifest is not valid JSON"; return false; }
    const QJsonObject o = doc.object();
    m->format = o.value("format").toString();
    if (m->format != "medicarepro.manifest/1") { *err = "Unknown manifest format"; return false; }
    m->publisher = o.value("publisher").toString();
    m->sequence = o.value("sequence").toInteger();
    m->generatedAt = QDateTime::fromString(o.value("generated_at").toString(), Qt::ISODate);
    m->expiresAt = QDateTime::fromString(o.value("expires_at").toString(), Qt::ISODate);
    if (m->sequence <= 0 || !m->generatedAt.isValid()) { *err = "Manifest sequence or date missing"; return false; }
    const QRegularExpression hex("^[0-9a-f]{64}$");
    for (const QJsonValue &v : o.value("items").toArray()) {
        const QJsonObject i = v.toObject();
        UpdateItem it;
        it.id = i.value("id").toString();
        it.type = i.value("type").toString();
        it.version = i.value("version").toString();
        it.baseVersion = i.value("base_version").toString();
        it.title = i.value("title").toString();
        it.description = i.value("description").toString();
        it.publishedAt = i.value("published_at").toString();
        for (const QJsonValue &c : i.value("components").toArray()) it.components << c.toString();
        for (const QJsonValue &c : i.value("notes").toArray()) it.notes << c.toString();
        it.major = i.value("major").toBool();
        it.minAppVersion = i.value("min_app_version").toString();
        it.url = i.value("url").toString();
        it.sha256 = i.value("sha256").toString().toLower();
        it.size = i.value("size").toInteger();
        it.signature = QByteArray::fromBase64(i.value("signature").toString().toLatin1());
        if (!kTypes.contains(it.type)) continue;   // newer types are ignored safely
        const QUrl url(it.url);
        if (url.scheme() != "https" || !url.isValid()) { *err = "Item " + it.id + " does not use HTTPS"; return false; }
        if (!hex.match(it.sha256).hasMatch()) { *err = "Item " + it.id + " has a bad hash"; return false; }
        if (it.signature.size() != 64) { *err = "Item " + it.id + " is not signed"; return false; }
        if (it.version.isEmpty() || it.size <= 0) { *err = "Item " + it.id + " lacks version or size"; return false; }
        m->items.append(it);
    }
    return true;
}

const UpdateItem *UpdateManifest::latest(const QString &type) const
{
    const UpdateItem *best = nullptr;
    for (const auto &i : items) {
        if (i.type != type) continue;
        if (!best || QVersionNumber::fromString(i.version) > QVersionNumber::fromString(best->version)) best = &i;
    }
    return best;
}
}
