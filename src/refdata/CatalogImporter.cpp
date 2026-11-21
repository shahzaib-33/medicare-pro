#include "CatalogImporter.h"
#include "CsvReader.h"
#include "SearchIndexer.h"
#include "TextNormalizer.h"
#include "app/AppPaths.h"
#include "db/Database.h"
#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>

namespace mcp {

static QVariant nullIfEmpty(const QString &s) { return s.isEmpty() ? QVariant() : QVariant(s); }

qint64 CatalogWriter::sourceId(const QString &code, const DataSourceInfo *info)
{
    if (code.isEmpty()) return 0;
    if (m_srcCache.contains(code) && !info) return m_srcCache.value(code);
    QSqlQuery q(m_db);
    q.prepare("SELECT id FROM medicine_data_sources WHERE code=?");
    q.addBindValue(code);
    q.exec();
    qint64 id = q.next() ? q.value(0).toLongLong() : 0;
    if (info) {
        QSqlQuery u(m_db);
        if (id) {
            u.prepare("UPDATE medicine_data_sources SET name=?, url=?, access_method=?, terms_note=?, retrieved_at=? WHERE id=?");
        } else {
            u.prepare("INSERT INTO medicine_data_sources(name, url, access_method, terms_note, retrieved_at, code) VALUES(?,?,?,?,?,?)");
        }
        u.addBindValue(info->name.isEmpty() ? code : info->name);
        u.addBindValue(nullIfEmpty(info->url));
        u.addBindValue(nullIfEmpty(info->accessMethod));
        u.addBindValue(nullIfEmpty(info->termsNote));
        u.addBindValue(nullIfEmpty(info->retrievedAt));
        u.addBindValue(id ? QVariant(id) : QVariant(code));
        u.exec();
        if (!id) id = u.lastInsertId().toLongLong();
    } else if (!id) {
        QSqlQuery u(m_db);
        u.prepare("INSERT INTO medicine_data_sources(code, name) VALUES(?,?)");
        u.addBindValue(code);
        u.addBindValue(code);
        u.exec();
        id = u.lastInsertId().toLongLong();
    }
    m_srcCache.insert(code, id);
    return id;
}

qint64 CatalogWriter::manufacturerId(const QString &name)
{
    if (name.trimmed().isEmpty()) return 0;
    const QString norm = TextNormalizer::normalize(name);
    if (auto it = m_manuCache.constFind(norm); it != m_manuCache.cend()) return *it;
    QSqlQuery q(m_db);
    q.prepare("SELECT id FROM manufacturers_reference WHERE name_norm=?");
    q.addBindValue(norm);
    q.exec();
    qint64 id = 0;
    if (q.next()) id = q.value(0).toLongLong();
    else {
        QSqlQuery i(m_db);
        i.prepare("INSERT INTO manufacturers_reference(name, name_norm, updated_at) VALUES(?,?,?)");
        i.addBindValue(name.trimmed());
        i.addBindValue(norm);
        i.addBindValue(Db::nowIso());
        i.exec();
        id = i.lastInsertId().toLongLong();
    }
    m_manuCache.insert(norm, id);
    return id;
}

qint64 CatalogWriter::categoryId(const QString &name)
{
    if (name.trimmed().isEmpty()) return 0;
    const QString norm = TextNormalizer::normalize(name);
    if (auto it = m_catCache.constFind(norm); it != m_catCache.cend()) return *it;
    QSqlQuery q(m_db);
    q.prepare("SELECT id FROM medicine_categories_reference WHERE name_norm=?");
    q.addBindValue(norm);
    q.exec();
    qint64 id = 0;
    if (q.next()) id = q.value(0).toLongLong();
    else {
        QSqlQuery i(m_db);
        i.prepare("INSERT INTO medicine_categories_reference(name, name_norm) VALUES(?,?)");
        i.addBindValue(name.trimmed());
        i.addBindValue(norm);
        i.exec();
        id = i.lastInsertId().toLongLong();
    }
    m_catCache.insert(norm, id);
    return id;
}

bool CatalogWriter::logEvent(qint64 medId, const QString &refKey, const QString &regNo, const QString &event,
                             const QString &oldS, const QString &newS, const QString &details, qint64 src, int version)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO regulatory_registration_records(medicine_id, ref_key, registration_no, event, old_status, "
              "new_status, event_date, details, source_id, db_version) VALUES(?,?,?,?,?,?,?,?,?,?)");
    q.addBindValue(medId ? QVariant(medId) : QVariant());
    q.addBindValue(refKey);
    q.addBindValue(nullIfEmpty(regNo));
    q.addBindValue(event);
    q.addBindValue(nullIfEmpty(oldS));
    q.addBindValue(nullIfEmpty(newS));
    q.addBindValue(QDate::currentDate().toString(Qt::ISODate));
    q.addBindValue(nullIfEmpty(details));
    q.addBindValue(src ? QVariant(src) : QVariant());
    q.addBindValue(version);
    return q.exec();
}

CatalogWriter::Change CatalogWriter::upsert(const MedicineRecord &r, int version, QString *err)
{
    Change ch{Outcome::Failed, 0, {}, {}, {}};
    const QString hash = r.contentHash();
    QSqlQuery f(m_db);
    f.prepare("SELECT id, row_hash, registration_status, registration_no FROM national_medicine_catalog WHERE ref_key=?");
    f.addBindValue(r.refKey);
    if (!f.exec()) { if (err) *err = f.lastError().text(); return ch; }
    const bool exists = f.next();
    if (exists) {
        ch.id = f.value(0).toLongLong();
        ch.oldHash = f.value(1).toString();
        ch.oldStatus = f.value(2).toString();
        ch.oldRegNo = f.value(3).toString();
        if (ch.oldHash == hash) { ch.outcome = Outcome::Unchanged; return ch; }
    }
    const qint64 src = sourceId(r.sourceCode);
    const qint64 manu = manufacturerId(r.manufacturer);
    const qint64 cat = categoryId(r.category);

    QSqlQuery q(m_db);
    if (exists) {
        q.prepare("UPDATE national_medicine_catalog SET brand_name=?, generic_name=?, composition=?, strength=?, "
                  "dosage_form=?, manufacturer_id=?, mah_name=?, registration_no=?, registration_date=?, "
                  "registration_status=?, category_id=?, pack_size=?, barcode=?, rx_class=?, reference_price=?, "
                  "price_source=?, source_id=?, verification_status=?, verified_at=?, last_updated=?, row_hash=?, "
                  "brand_norm=?, generic_norm=?, version_changed=? WHERE id=?");
    } else {
        q.prepare("INSERT INTO national_medicine_catalog(brand_name, generic_name, composition, strength, dosage_form, "
                  "manufacturer_id, mah_name, registration_no, registration_date, registration_status, category_id, "
                  "pack_size, barcode, rx_class, reference_price, price_source, source_id, verification_status, "
                  "verified_at, last_updated, row_hash, brand_norm, generic_norm, version_changed, version_added, ref_key) "
                  "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    }
    q.addBindValue(r.brandName);
    q.addBindValue(nullIfEmpty(r.genericName));
    q.addBindValue(nullIfEmpty(r.composition));
    q.addBindValue(nullIfEmpty(r.strength));
    q.addBindValue(nullIfEmpty(r.dosageForm));
    q.addBindValue(manu ? QVariant(manu) : QVariant());
    q.addBindValue(nullIfEmpty(r.mahName));
    q.addBindValue(nullIfEmpty(r.registrationNo));
    q.addBindValue(nullIfEmpty(r.registrationDate));
    q.addBindValue(r.registrationStatus.isEmpty() ? QString("unknown") : r.registrationStatus);
    q.addBindValue(cat ? QVariant(cat) : QVariant());
    q.addBindValue(nullIfEmpty(r.packSize));
    q.addBindValue(nullIfEmpty(r.barcode));
    q.addBindValue(nullIfEmpty(r.rxClass));
    q.addBindValue(r.referencePrice);
    q.addBindValue(nullIfEmpty(r.priceSource));
    q.addBindValue(src ? QVariant(src) : QVariant());
    q.addBindValue(r.verificationStatus);
    q.addBindValue(nullIfEmpty(r.verifiedAt));
    q.addBindValue(Db::nowIso());
    q.addBindValue(hash);
    q.addBindValue(TextNormalizer::normalize(r.brandName));
    q.addBindValue(nullIfEmpty(TextNormalizer::normalize(r.genericName)));
    q.addBindValue(version);
    if (exists) q.addBindValue(ch.id);
    else { q.addBindValue(version); q.addBindValue(r.refKey); }
    if (!q.exec()) { if (err) *err = q.lastError().text(); return ch; }
    if (!exists) ch.id = q.lastInsertId().toLongLong();

    // Translations: only values that were supplied. Empty means keep nothing.
    QSqlQuery dt(m_db);
    dt.prepare("DELETE FROM medicine_translations WHERE medicine_id=? AND lang='ur'");
    dt.addBindValue(ch.id);
    dt.exec();
    for (const auto &[field, value] : {std::pair{QString("brand"), r.brandNameUr}, std::pair{QString("generic"), r.genericNameUr}}) {
        if (value.trimmed().isEmpty()) continue;
        QSqlQuery t(m_db);
        t.prepare("INSERT INTO medicine_translations(medicine_id, lang, field, value, value_norm, provenance) VALUES(?,?,?,?,?,?)");
        t.addBindValue(ch.id);
        t.addBindValue("ur");
        t.addBindValue(field);
        t.addBindValue(value.trimmed());
        t.addBindValue(TextNormalizer::normalize(value));
        t.addBindValue(r.verificationStatus == "verified" ? "verified" : "manual");
        if (!t.exec()) { if (err) *err = t.lastError().text(); return ch; }
    }
    QSqlQuery di(m_db);
    di.prepare("DELETE FROM medicine_ingredients WHERE medicine_id=?");
    di.addBindValue(ch.id);
    di.exec();
    for (int i = 0; i < r.ingredients.size(); ++i) {
        QSqlQuery t(m_db);
        t.prepare("INSERT INTO medicine_ingredients(medicine_id, position, ingredient, ingredient_norm) VALUES(?,?,?,?)");
        t.addBindValue(ch.id);
        t.addBindValue(i);
        t.addBindValue(r.ingredients[i]);
        t.addBindValue(TextNormalizer::normalize(r.ingredients[i]));
        if (!t.exec()) { if (err) *err = t.lastError().text(); return ch; }
    }
    if (!SearchIndexer::reindex(m_db, ch.id, r, err)) return ch;

    if (!exists) {
        logEvent(ch.id, r.refKey, r.registrationNo, "added", {}, r.registrationStatus, {}, src, version);
        ch.outcome = Outcome::Added;
    } else {
        if (ch.oldStatus != r.registrationStatus)
            logEvent(ch.id, r.refKey, r.registrationNo, "status_changed", ch.oldStatus, r.registrationStatus, {}, src, version);
        if (ch.oldRegNo != r.registrationNo)
            logEvent(ch.id, r.refKey, r.registrationNo, "registration_no_changed", ch.oldRegNo, r.registrationNo, {}, src, version);
        ch.outcome = Outcome::Updated;
    }
    return ch;
}

bool CatalogWriter::deactivate(const QString &refKey, const QString &newStatus, const QString &details,
                               const QString &sourceCode, int version, QString *oldStatus, QString *err)
{
    QSqlQuery f(m_db);
    f.prepare("SELECT id, registration_status, registration_no FROM national_medicine_catalog WHERE ref_key=?");
    f.addBindValue(refKey);
    if (!f.exec() || !f.next()) { if (err) *err = "unknown ref_key " + refKey; return false; }
    const qint64 id = f.value(0).toLongLong();
    *oldStatus = f.value(1).toString();
    const QString regNo = f.value(2).toString();
    if (*oldStatus == newStatus) return true;
    // Records are never deleted. Their status changes and history is kept.
    QSqlQuery u(m_db);
    u.prepare("UPDATE national_medicine_catalog SET registration_status=?, last_updated=?, version_changed=?, "
              "row_hash=row_hash||'~' WHERE id=?");
    u.addBindValue(newStatus);
    u.addBindValue(Db::nowIso());
    u.addBindValue(version);
    u.addBindValue(id);
    if (!u.exec()) { if (err) *err = u.lastError().text(); return false; }
    return logEvent(id, refKey, regNo, "status_changed", *oldStatus, newStatus, details, sourceId(sourceCode), version);
}

namespace CatalogImporter {

static const QHash<QString, QString> &aliases()
{
    static const QHash<QString, QString> a = {
        {"ref_key", "ref_key"}, {"reference key", "ref_key"},
        {"brand_name", "brand"}, {"brand", "brand"}, {"brand name", "brand"}, {"product name", "brand"},
        {"product", "brand"}, {"trade name", "brand"}, {"name", "brand"},
        {"brand_name_ur", "brand_ur"}, {"urdu brand name", "brand_ur"}, {"brand name urdu", "brand_ur"},
        {"generic_name", "generic"}, {"generic", "generic"}, {"generic name", "generic"},
        {"generic_name_ur", "generic_ur"}, {"urdu generic name", "generic_ur"},
        {"active_ingredients", "ingredients"}, {"active ingredients", "ingredients"}, {"ingredients", "ingredients"},
        {"composition", "composition"}, {"formulation", "composition"},
        {"strength", "strength"},
        {"dosage_form", "form"}, {"dosage form", "form"}, {"form", "form"},
        {"manufacturer", "manufacturer"}, {"company", "manufacturer"}, {"firm", "manufacturer"}, {"manufactured by", "manufacturer"},
        {"mah", "mah"}, {"marketing authorization holder", "mah"}, {"marketing_authorization_holder", "mah"},
        {"registration_no", "reg_no"}, {"registration no", "reg_no"}, {"reg no", "reg_no"}, {"reg. no", "reg_no"},
        {"registration number", "reg_no"}, {"reg #", "reg_no"},
        {"registration_date", "reg_date"}, {"registration date", "reg_date"}, {"date of registration", "reg_date"},
        {"registration_status", "status"}, {"status", "status"},
        {"category", "category"}, {"product category", "category"}, {"therapeutic category", "category"},
        {"pack_size", "pack"}, {"pack size", "pack"}, {"pack", "pack"},
        {"barcode", "barcode"}, {"gtin", "barcode"},
        {"rx_class", "rx"}, {"prescription", "rx"}, {"schedule", "rx"},
        {"price", "price"}, {"mrp", "price"}, {"maximum retail price", "price"}, {"retail price", "price"},
        {"price_source", "price_source"}, {"price source", "price_source"},
        {"verified_at", "verified_at"}, {"verification date", "verified_at"},
    };
    return a;
}

QHash<QString, int> mapHeader(const QStringList &header, QStringList *unknown)
{
    QHash<QString, int> cols;
    for (int i = 0; i < header.size(); ++i) {
        const QString h = header[i].trimmed().toLower().replace(QRegularExpression("\\s+"), " ");
        const QString key = aliases().value(h);
        if (key.isEmpty()) { if (unknown) unknown->append(header[i]); continue; }
        if (!cols.contains(key)) cols.insert(key, i);
    }
    return cols;
}

static QString parseDate(const QString &s, bool *ok)
{
    *ok = true;
    const QString t = s.trimmed();
    if (t.isEmpty()) return {};
    for (const char *fmt : {"yyyy-MM-dd", "dd/MM/yyyy", "dd-MM-yyyy", "d/M/yyyy", "dd.MM.yyyy", "dd-MMM-yyyy", "d-MMM-yy"}) {
        const QDate d = QDate::fromString(t, QString::fromLatin1(fmt));
        if (d.isValid()) {
            QDate fixed = d;
            if (fixed.year() < 1950) fixed = fixed.addYears(100);
            return fixed.toString(Qt::ISODate);
        }
    }
    *ok = false;
    return {};
}

static QString normStatus(const QString &s, bool *ok)
{
    *ok = true;
    const QString t = s.trimmed().toLower();
    if (t.isEmpty()) return "unknown";
    if (t.contains("cancel")) return "cancelled";
    if (t.contains("suspend")) return "suspended";
    if (t.contains("discontinu") || t.contains("withdraw")) return "discontinued";
    if (t.contains("inactive")) return "discontinued";
    if (t == "active" || t.contains("registered") || t == "valid") return "active";
    if (t == "unknown") return "unknown";
    *ok = false;
    return "unknown";
}

bool parseRow(const QHash<QString, int> &cols, const QStringList &row, const QString &sourceCode,
              const QString &verification, MedicineRecord *r, QString *problem)
{
    auto v = [&](const char *k) -> QString {
        const int i = cols.value(QString::fromLatin1(k), -1);
        return (i >= 0 && i < row.size()) ? row[i].simplified() : QString();
    };
    *r = MedicineRecord{};
    r->brandName = v("brand");
    if (r->brandName.isEmpty()) { *problem = "Missing brand name"; return false; }
    if (r->brandName.size() > 200) { *problem = "Brand name too long"; return false; }
    r->brandNameUr = v("brand_ur");
    r->genericName = v("generic");
    r->genericNameUr = v("generic_ur");
    if (!r->brandNameUr.isEmpty() && !TextNormalizer::containsUrdu(r->brandNameUr)) {
        *problem = "Urdu brand name has no Urdu letters"; return false;
    }
    const QString ingr = v("ingredients");
    if (!ingr.isEmpty()) {
        for (const QString &p : ingr.split(QRegularExpression("\\s*[+;|]\\s*"), Qt::SkipEmptyParts))
            r->ingredients << p.trimmed();
    } else if (!r->genericName.isEmpty()) {
        // Generic names in DRAP style lists name the active ingredients, e.g. "Paracetamol + Caffeine".
        for (const QString &p : r->genericName.split(QRegularExpression("\\s*[+]\\s*"), Qt::SkipEmptyParts))
            r->ingredients << p.trimmed();
    }
    r->composition = v("composition");
    r->strength = v("strength");
    r->dosageForm = v("form");
    r->manufacturer = v("manufacturer");
    r->mahName = v("mah");
    r->registrationNo = v("reg_no");
    if (!r->registrationNo.isEmpty() && !QRegularExpression("^[A-Za-z0-9/\\-. ]{1,40}$").match(r->registrationNo).hasMatch()) {
        *problem = "Registration number has unexpected characters"; return false;
    }
    bool ok = true;
    r->registrationDate = parseDate(v("reg_date"), &ok);
    if (!ok) { *problem = "Registration date not understood: " + v("reg_date"); return false; }
    r->registrationStatus = normStatus(v("status"), &ok);
    if (!ok) { *problem = "Registration status not understood: " + v("status"); return false; }
    r->category = v("category");
    r->packSize = v("pack");
    r->barcode = v("barcode");
    if (!r->barcode.isEmpty() && !QRegularExpression("^[0-9]{8,14}$").match(r->barcode).hasMatch()) {
        *problem = "Barcode must be 8 to 14 digits"; return false;
    }
    r->rxClass = v("rx");
    const QString price = v("price");
    if (!price.isEmpty()) {
        bool pok = false;
        const double p = QString(price).remove(',').remove("Rs").remove("PKR").trimmed().toDouble(&pok);
        if (!pok || p <= 0) { *problem = "Price is not a positive number"; return false; }
        r->priceSource = v("price_source");
        if (r->priceSource.isEmpty()) { *problem = "Price given without price_source"; return false; }
        r->referencePrice = p;
    }
    r->verifiedAt = parseDate(v("verified_at"), &ok);
    r->sourceCode = sourceCode;
    r->verificationStatus = verification;
    r->refKey = v("ref_key");
    if (r->refKey.isEmpty()) {
        if (!r->registrationNo.isEmpty())
            r->refKey = "REG:" + TextNormalizer::normalize(r->registrationNo).remove(' ').toUpper();
        else
            r->refKey = "SRC:" + QString::fromLatin1(QCryptographicHash::hash(
                (TextNormalizer::normalize(r->brandName) + '|' + TextNormalizer::normalize(r->strength) + '|'
                 + TextNormalizer::normalize(r->dosageForm) + '|' + TextNormalizer::normalize(r->manufacturer)).toUtf8(),
                QCryptographicHash::Sha1).toHex().left(20));
    }
    return true;
}

int currentVersion(QSqlDatabase db)
{
    QSqlQuery q(db);
    q.exec("SELECT COALESCE(MAX(version),0) FROM medicine_database_versions");
    return q.next() ? q.value(0).toInt() : 0;
}

ImportReport importCsv(QSqlDatabase db, const QString &csvPath, const ImportOptions &opt)
{
    ImportReport rep;
    QFile f(csvPath);
    if (!f.open(QIODevice::ReadOnly)) { rep.error = "Cannot open file: " + f.errorString(); return rep; }
    if (opt.source.code.trimmed().isEmpty()) { rep.error = "A data source code is required"; return rep; }
    CsvReader csv(&f);
    QStringList header;
    if (!csv.readRow(&header)) { rep.error = "File is empty"; return rep; }
    QStringList unknown;
    const auto cols = mapHeader(header, &unknown);
    if (!cols.contains("brand")) { rep.error = "No brand name column found"; return rep; }
    for (const QString &u : unknown) rep.issues.append({1, "Column ignored: " + u});

    if (!opt.dryRun) {
        rep.backupPath = AppPaths::backupDir() + "/reference_before_import_"
            + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".db";
        QString err;
        if (!Db::snapshot(db, rep.backupPath, &err)) { rep.error = "Safety backup failed: " + err; return rep; }
        if (!db.transaction()) { rep.error = db.lastError().text(); return rep; }
    }
    const int version = currentVersion(db) + 1;
    rep.newVersion = version;
    CatalogWriter w(db);
    if (!opt.dryRun) w.sourceId(opt.source.code, &opt.source);
    QSet<QString> seenKeys;
    QStringList row;
    const QString verification = opt.markSample ? "sample" : (opt.markVerified ? "verified" : "unverified");
    if (!opt.dryRun && !opt.markSample) {
        // Real data replaces the demonstration records shipped for first run.
        QSqlQuery del(db);
        del.exec("DELETE FROM national_medicine_catalog WHERE verification_status='sample'");
        rep.sampleRemoved = del.numRowsAffected();
    }
    while (csv.readRow(&row)) {
        if (row.size() == 1 && row[0].trimmed().isEmpty()) continue;
        ++rep.rowsRead;
        MedicineRecord r;
        QString problem;
        if (!parseRow(cols, row, opt.source.code, verification, &r, &problem)) {
            ++rep.skipped;
            rep.issues.append({csv.lineNumber(), problem});
            continue;
        }
        if (opt.markVerified && r.verifiedAt.isEmpty()) r.verifiedAt = QDate::currentDate().toString(Qt::ISODate);
        if (seenKeys.contains(r.refKey)) {
            ++rep.duplicates;
            rep.issues.append({csv.lineNumber(), "Duplicate of an earlier row: " + r.refKey});
            continue;
        }
        seenKeys.insert(r.refKey);
        if (opt.dryRun) { ++rep.added; continue; }
        QString err;
        const auto ch = w.upsert(r, version, &err);
        switch (ch.outcome) {
        case CatalogWriter::Outcome::Added: ++rep.added; break;
        case CatalogWriter::Outcome::Updated: ++rep.updated; break;
        case CatalogWriter::Outcome::Unchanged: ++rep.unchanged; break;
        case CatalogWriter::Outcome::Failed:
            db.rollback();
            rep.error = QStringLiteral("Line %1: %2. Nothing was changed.").arg(csv.lineNumber()).arg(err);
            return rep;
        }
    }
    if (opt.dryRun) { rep.ok = true; return rep; }

    QSqlQuery cnt(db);
    cnt.exec("SELECT COUNT(*) FROM national_medicine_catalog");
    const int total = cnt.next() ? cnt.value(0).toInt() : 0;
    QSqlQuery v(db);
    v.prepare("INSERT INTO medicine_database_versions(version, published_at, installed_at, description, record_count, "
              "coverage_note, kind) VALUES(?,?,?,?,?,?,'import')");
    v.addBindValue(version);
    v.addBindValue(QDate::currentDate().toString(Qt::ISODate));
    v.addBindValue(Db::nowIso());
    v.addBindValue(QStringLiteral("Imported %1 from %2").arg(QFileInfo(csvPath).fileName(), opt.source.name));
    v.addBindValue(total);
    v.addBindValue(opt.coverageNote);
    if (!v.exec() || !db.commit()) {
        rep.error = db.lastError().text();
        db.rollback();
        return rep;
    }
    rep.ok = true;
    return rep;
}
}
}
