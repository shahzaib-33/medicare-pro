#include "ReferenceUpdater.h"
#include "UpdateLog.h"
#include "app/AppPaths.h"
#include "db/Database.h"
#include "refdata/PharmacyCatalog.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>

namespace mcp {

static const QStringList kRecordKeys = {"ref_key", "brand", "brand_ur", "generic", "generic_ur", "ingredients",
    "composition", "strength", "form", "manufacturer", "mah", "reg_no", "reg_date", "status", "category", "pack",
    "barcode", "rx", "price", "price_source", "verified_at"};
static const QStringList kStatuses = {"active", "suspended", "cancelled", "discontinued", "unknown"};

bool ReferenceUpdater::parsePackage(const QByteArray &rawIn, ReferencePackage *p, QStringList *problems)
{
    QByteArray raw = rawIn;
    if (!raw.startsWith('{')) raw = qUncompress(raw);
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) { problems->append("Package is not valid JSON"); return false; }
    const QJsonObject o = doc.object();
    if (o.value("format").toString() != "medicarepro.refpackage/1") { problems->append("Unknown package format"); return false; }
    p->baseVersion = o.value("base_version").toInt();
    p->targetVersion = o.value("target_version").toInt();
    p->full = o.value("full").toBool();
    p->publishedAt = o.value("published_at").toString();
    p->description = o.value("description").toString();
    p->coverageNote = o.value("coverage_note").toString();
    if (p->targetVersion <= 0 || (!p->full && p->baseVersion >= p->targetVersion)) {
        problems->append("Package versions are inconsistent");
        return false;
    }
    QSet<QString> sourceCodes;
    for (const QJsonValue &v : o.value("sources").toArray()) {
        const QJsonObject s = v.toObject();
        DataSourceInfo d{s.value("code").toString(), s.value("name").toString(), s.value("url").toString(),
                         s.value("access_method").toString(), s.value("terms_note").toString(),
                         s.value("retrieved_at").toString()};
        if (d.code.isEmpty() || d.name.isEmpty()) { problems->append("Source without code or name"); return false; }
        sourceCodes.insert(d.code);
        p->sources.append(d);
    }
    QHash<QString, int> cols;
    for (int i = 0; i < kRecordKeys.size(); ++i) {
        const QString k = kRecordKeys[i];
        cols.insert(k == "ingredients" ? "ingredients" : k, i);
    }
    QSet<QString> keys;
    int idx = 0;
    for (const QJsonValue &v : o.value("upserts").toArray()) {
        ++idx;
        const QJsonObject r = v.toObject();
        QStringList row;
        for (const QString &k : kRecordKeys) {
            const QJsonValue f = r.value(k);
            if (f.isArray()) {
                QStringList parts;
                for (const QJsonValue &x : f.toArray()) parts << x.toString();
                row << parts.join(" ; ");
            } else if (f.isDouble()) row << QString::number(f.toDouble(), 'f', 2);
            else row << f.toString();
        }
        const QString src = r.value("source").toString();
        const QString ver = r.value("verification").toString("unverified");
        if (!sourceCodes.contains(src)) { problems->append(QStringLiteral("Record %1: unknown source").arg(idx)); continue; }
        if (!QStringList{"verified", "unverified", "sample"}.contains(ver)) {
            problems->append(QStringLiteral("Record %1: bad verification value").arg(idx)); continue;
        }
        if (r.value("ref_key").toString().isEmpty()) { problems->append(QStringLiteral("Record %1: missing ref_key").arg(idx)); continue; }
        const QString st = r.value("status").toString();
        if (!st.isEmpty() && !kStatuses.contains(st)) { problems->append(QStringLiteral("Record %1: bad status").arg(idx)); continue; }
        MedicineRecord rec;
        QString problem;
        if (!CatalogImporter::parseRow(cols, row, src, ver, &rec, &problem)) {
            problems->append(QStringLiteral("Record %1: %2").arg(idx).arg(problem));
            continue;
        }
        if (keys.contains(rec.refKey)) { problems->append("Duplicate record " + rec.refKey); continue; }
        keys.insert(rec.refKey);
        p->upserts.append(rec);
    }
    for (const QJsonValue &v : o.value("status_changes").toArray()) {
        const QJsonObject s = v.toObject();
        StatusChange c{s.value("ref_key").toString(), s.value("status").toString(), s.value("details").toString(),
                       s.value("source").toString()};
        if (c.refKey.isEmpty() || !kStatuses.contains(c.status) || !sourceCodes.contains(c.sourceCode)) {
            problems->append("Bad status change for " + c.refKey);
            continue;
        }
        p->statusChanges.append(c);
    }
    return problems->isEmpty();
}

QByteArray ReferenceUpdater::encodePackage(const ReferencePackage &p, bool compress)
{
    QJsonObject o;
    o["format"] = "medicarepro.refpackage/1";
    o["base_version"] = p.baseVersion;
    o["target_version"] = p.targetVersion;
    o["full"] = p.full;
    o["published_at"] = p.publishedAt;
    o["description"] = p.description;
    o["coverage_note"] = p.coverageNote;
    QJsonArray sources;
    for (const auto &s : p.sources)
        sources.append(QJsonObject{{"code", s.code}, {"name", s.name}, {"url", s.url}, {"access_method", s.accessMethod},
                                   {"terms_note", s.termsNote}, {"retrieved_at", s.retrievedAt}});
    o["sources"] = sources;
    QJsonArray ups;
    for (const auto &r : p.upserts) {
        QJsonObject j{{"ref_key", r.refKey}, {"brand", r.brandName}, {"source", r.sourceCode},
                      {"verification", r.verificationStatus}, {"status", r.registrationStatus}};
        auto put = [&j](const char *k, const QString &v) { if (!v.isEmpty()) j[k] = v; };
        put("brand_ur", r.brandNameUr); put("generic", r.genericName); put("generic_ur", r.genericNameUr);
        put("composition", r.composition); put("strength", r.strength); put("form", r.dosageForm);
        put("manufacturer", r.manufacturer); put("mah", r.mahName); put("reg_no", r.registrationNo);
        put("reg_date", r.registrationDate); put("category", r.category); put("pack", r.packSize);
        put("barcode", r.barcode); put("rx", r.rxClass); put("verified_at", r.verifiedAt);
        if (!r.ingredients.isEmpty()) j["ingredients"] = QJsonArray::fromStringList(r.ingredients);
        if (!r.referencePrice.isNull()) { j["price"] = r.referencePrice.toDouble(); put("price_source", r.priceSource); }
        ups.append(j);
    }
    o["upserts"] = ups;
    QJsonArray sc;
    for (const auto &c : p.statusChanges)
        sc.append(QJsonObject{{"ref_key", c.refKey}, {"status", c.status}, {"details", c.details}, {"source", c.sourceCode}});
    o["status_changes"] = sc;
    const QByteArray json = QJsonDocument(o).toJson(QJsonDocument::Compact);
    return compress ? qCompress(json, 9) : json;
}

UpdatePreview ReferenceUpdater::preview(const ReferencePackage &p) const
{
    UpdatePreview pv;
    QSqlDatabase db = Db::get(m_ref);
    for (const auto &r : p.upserts) {
        QSqlQuery q(db);
        q.prepare("SELECT row_hash FROM national_medicine_catalog WHERE ref_key=?");
        q.addBindValue(r.refKey);
        q.exec();
        if (!q.next()) { ++pv.added; if (pv.newBrands.size() < 200) pv.newBrands << r.brandName + ' ' + r.strength; }
        else if (q.value(0).toString() != r.contentHash()) {
            ++pv.updated;
            if (pv.changedBrands.size() < 200) pv.changedBrands << r.brandName + ' ' + r.strength;
        } else ++pv.unchanged;
    }
    for (const auto &c : p.statusChanges) {
        QSqlQuery q(db);
        q.prepare("SELECT brand_name, registration_status FROM national_medicine_catalog WHERE ref_key=?");
        q.addBindValue(c.refKey);
        q.exec();
        if (q.next() && q.value(1).toString() != c.status) {
            ++pv.deactivated;
            if (pv.inactiveBrands.size() < 200) pv.inactiveBrands << q.value(0).toString() + " (" + c.status + ")";
        }
    }
    return pv;
}

InstallResult ReferenceUpdater::install(const ReferencePackage &p, const QString &packageSha, bool testFail)
{
    InstallResult res;
    QSqlDatabase ref = Db::get(m_ref);
    QSqlDatabase ph = Db::get(m_ph);
    const int current = CatalogImporter::currentVersion(ref);
    if (p.targetVersion <= current) { res.error = QObject::tr("This version is already installed."); return res; }
    if (!p.full && p.baseVersion != current) {
        res.error = QObject::tr("This update needs database version %1. You have version %2.").arg(p.baseVersion).arg(current);
        return res;
    }

    // 1. Safety backup of the reference database.
    res.backupPath = AppPaths::backupDir() + QStringLiteral("/reference_v%1_%2.db")
                         .arg(current).arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString err;
    if (!Db::snapshot(ref, res.backupPath, &err)) { res.error = "Backup failed: " + err; return res; }
    const qint64 hist = UpdateLog::beginHistory(ph, p.full ? "reference_full" : "reference_delta", current,
                                                p.targetVersion, res.backupPath);
    UpdateLog::line(ph, hist, "info", "Backup created: " + res.backupPath);

    auto failInTx = [&](const QString &why) {
        ref.rollback();
        res.error = why;
        res.rolledBack = true;
        UpdateLog::line(ph, hist, "error", why);
        UpdateLog::finishHistory(ph, hist, "rolled_back", 0, 0, 0, 0, 0, why);
        return res;
    };

    // 2. Apply everything in one transaction.
    if (!ref.transaction()) return failInTx(ref.lastError().text());
    CatalogWriter w(ref);
    for (const auto &s : p.sources) w.sourceId(s.code, &s);
    for (const auto &r : p.upserts) {
        const auto ch = w.upsert(r, p.targetVersion, &err);
        switch (ch.outcome) {
        case CatalogWriter::Outcome::Added: ++res.added; break;
        case CatalogWriter::Outcome::Updated: ++res.updated; break;
        case CatalogWriter::Outcome::Unchanged: ++res.unchanged; break;
        case CatalogWriter::Outcome::Failed: return failInTx("Record " + r.refKey + ": " + err);
        }
    }
    for (const auto &c : p.statusChanges) {
        QString old;
        if (!w.deactivate(c.refKey, c.status, c.details, c.sourceCode, p.targetVersion, &old, &err))
            UpdateLog::line(ph, hist, "warning", "Status change skipped: " + err);
        else if (old != c.status) ++res.deactivated;
    }
    QSqlQuery cnt(ref);
    cnt.exec("SELECT COUNT(*) FROM national_medicine_catalog");
    const int total = cnt.next() ? cnt.value(0).toInt() : 0;
    QSqlQuery v(ref);
    v.prepare("INSERT INTO medicine_database_versions(version, published_at, installed_at, description, record_count, "
              "coverage_note, package_sha256, kind) VALUES(?,?,?,?,?,?,?,?)");
    v.addBindValue(p.targetVersion); v.addBindValue(p.publishedAt); v.addBindValue(Db::nowIso());
    v.addBindValue(p.description); v.addBindValue(total); v.addBindValue(p.coverageNote);
    v.addBindValue(packageSha); v.addBindValue(p.full ? "full" : "delta");
    if (!v.exec()) return failInTx(v.lastError().text());
    if (testFail) return failInTx("Simulated failure during install");
    if (!ref.commit()) return failInTx(ref.lastError().text());

    // 3. Verify the result. Restore the backup if anything looks wrong.
    QSqlQuery chk(ref);
    chk.exec("PRAGMA quick_check");
    if (!chk.next() || chk.value(0).toString() != "ok") {
        QString rerr;
        restoreBackup(res.backupPath, &rerr);
        res.rolledBack = true;
        res.error = "Integrity check failed. Previous version restored.";
        UpdateLog::finishHistory(ph, hist, "rolled_back", 0, 0, 0, 0, 0, res.error);
        return res;
    }

    // 4. Local products are only flagged for review. Prices, stock, barcodes and suppliers stay untouched.
    res.flagged = PharmacyCatalog::flagChangedProducts(ph, ref, p.targetVersion);
    if (m_applyPrices) res.pricesChanged = PharmacyCatalog::applyReferencePrices(ph, ref, p.targetVersion);
    UpdateLog::finishHistory(ph, hist, "installed", res.added, res.updated, res.deactivated, res.unchanged,
                             res.flagged, p.description);
    pruneBackups(5);
    res.ok = true;
    return res;
}

bool ReferenceUpdater::restoreBackup(const QString &backupPath, QString *err)
{
    if (!QFile::exists(backupPath)) { *err = "Backup file not found"; return false; }
    const QString path = Db::pathOf(m_ref);
    const QString tmp = path + ".restoring";
    QFile::remove(tmp);
    if (!QFile::copy(backupPath, tmp)) { *err = "Cannot copy backup"; return false; }
    Db::close(m_ref);
    QFile::remove(path + "-wal");
    QFile::remove(path + "-shm");
    const QString broken = path + ".replaced";
    QFile::remove(broken);
    QFile::rename(path, broken);
    if (!QFile::rename(tmp, path)) {
        QFile::rename(broken, path);
        Db::reopen(m_ref, err);
        *err = "Cannot replace database file";
        return false;
    }
    QFile::remove(broken);
    if (!Db::reopen(m_ref, err)) return false;
    UpdateLog::security(Db::get(m_ph), "reference_restored", backupPath);
    return true;
}

QStringList ReferenceUpdater::backups() const
{
    QDir d(AppPaths::backupDir());
    QStringList out;
    for (const QFileInfo &fi : d.entryInfoList({"reference_*.db"}, QDir::Files, QDir::Time))
        out << fi.absoluteFilePath();
    return out;
}

void ReferenceUpdater::pruneBackups(int keep) const
{
    const QStringList all = backups();
    for (int i = keep; i < all.size(); ++i) QFile::remove(all[i]);
}
}
