#include "MedicineSearch.h"
#include "SearchIndexer.h"
#include "TextNormalizer.h"
#include <QHash>
#include <QSet>
#include <QSqlQuery>
#include <algorithm>

namespace mcp {

namespace {
struct Match { int score = 0; int bestWeight = 0; bool phonetic = false; bool code = false; };

QString fieldName(int weight, bool phonetic, bool code)
{
    if (code) return "registration";
    if (phonetic) return "phonetic";
    if (weight >= 100) return "brand";
    if (weight >= 95) return "urdu";
    if (weight >= 80) return "generic";
    if (weight >= 78) return "urdu";
    if (weight >= 70) return "ingredient";
    if (weight >= 60) return "composition";
    if (weight >= 50) return "manufacturer";
    if (weight >= 45) return "mah";
    if (weight >= 40) return "strength";
    return "form";
}

QString upper(const QString &t) { return t + QChar(0xFFFF); }
}

SearchResult MedicineSearch::search(const QString &query, int limit) const
{
    SearchResult res;
    const QStringList toks = TextNormalizer::tokens(query);
    if (toks.isEmpty()) return res;

    // 1. Exact registration number or barcode.
    QHash<qint64, Match> matches;
    const QString compact = TextNormalizer::normalize(query).remove(' ');
    {
        QSqlQuery q(m_db);
        q.prepare("SELECT medicine_id FROM search_index WHERE kind=3 AND term=?");
        q.addBindValue(compact);
        q.exec();
        while (q.next()) {
            Match &m = matches[q.value(0).toLongLong()];
            m.score = 1000; m.code = true; m.bestWeight = 90;
        }
    }

    // 2. Every token must match some field (AND), by word prefix or by sound.
    QHash<qint64, Match> combined;
    bool first = true;
    for (const QString &t : toks) {
        QHash<qint64, Match> per;
        QSqlQuery q(m_db);
        if (t.size() >= 2) {
            q.prepare("SELECT medicine_id, weight, term FROM search_index WHERE kind=1 AND term>=? AND term<? LIMIT 50000");
            q.addBindValue(t);
            q.addBindValue(upper(t));
        } else {
            q.prepare("SELECT medicine_id, weight, term FROM search_index WHERE kind=1 AND term=? LIMIT 50000");
            q.addBindValue(t);
        }
        q.exec();
        while (q.next()) {
            const qint64 id = q.value(0).toLongLong();
            const int w = q.value(1).toInt();
            const bool exact = q.value(2).toString() == t;
            const int s = w + (exact ? 50 : 0);
            Match &m = per[id];
            if (s > m.score) { m.score = s; m.bestWeight = w; }
        }
        const QString pk = TextNormalizer::phoneticKey(t);
        if (pk.size() >= 3 || (pk.size() >= 2 && TextNormalizer::containsUrdu(t))) {
            QSqlQuery p(m_db);
            p.prepare("SELECT medicine_id, weight, term FROM search_index WHERE kind=2 AND term>=? AND term<? LIMIT 50000");
            p.addBindValue(pk);
            p.addBindValue(upper(pk));
            p.exec();
            while (p.next()) {
                const qint64 id = p.value(0).toLongLong();
                const int w = p.value(1).toInt();
                const bool exact = p.value(2).toString() == pk;
                const int s = w / 2 + (exact ? 15 : 0);
                Match &m = per[id];
                if (s > m.score) { m.score = s; m.bestWeight = w; m.phonetic = true; }
            }
        }
        if (first) { combined = per; first = false; }
        else {
            QHash<qint64, Match> next;
            for (auto it = combined.cbegin(); it != combined.cend(); ++it) {
                auto f = per.constFind(it.key());
                if (f == per.cend()) continue;
                Match m = it.value();
                m.score += f->score;
                m.phonetic = m.phonetic && f->phonetic;
                next.insert(it.key(), m);
            }
            combined = next;
        }
        if (combined.isEmpty()) break;
    }
    for (auto it = combined.cbegin(); it != combined.cend(); ++it)
        if (!matches.contains(it.key())) matches.insert(it.key(), it.value());

    res.totalCandidates = matches.size();

    // 3. Rank: score, then active registrations first, then brand name.
    QVector<QPair<qint64, Match>> ranked;
    ranked.reserve(matches.size());
    for (auto it = matches.cbegin(); it != matches.cend(); ++it) ranked.append({it.key(), it.value()});
    std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.second.score > b.second.score; });
    if (ranked.size() > limit * 3) ranked.resize(limit * 3);

    if (!ranked.isEmpty()) {
        QStringList ids;
        for (const auto &r : ranked) ids << QString::number(r.first);
        QSqlQuery q(m_db);
        q.exec("SELECT c.id, c.ref_key, c.brand_name, c.generic_name, c.strength, c.dosage_form, "
               "m.name, c.registration_no, c.registration_status, c.verification_status, "
               "(SELECT value FROM medicine_translations t WHERE t.medicine_id=c.id AND t.lang='ur' AND t.field='brand') "
               "FROM national_medicine_catalog c LEFT JOIN manufacturers_reference m ON m.id=c.manufacturer_id "
               "WHERE c.id IN (" + ids.join(',') + ")");
        QHash<qint64, CatalogHit> rows;
        while (q.next()) {
            CatalogHit h;
            h.id = q.value(0).toLongLong();
            h.refKey = q.value(1).toString();
            h.brandName = q.value(2).toString();
            h.genericName = q.value(3).toString();
            h.strength = q.value(4).toString();
            h.dosageForm = q.value(5).toString();
            h.manufacturer = q.value(6).toString();
            h.registrationNo = q.value(7).toString();
            h.registrationStatus = q.value(8).toString();
            h.verificationStatus = q.value(9).toString();
            h.brandNameUr = q.value(10).toString();
            rows.insert(h.id, h);
        }
        for (const auto &r : ranked) {
            auto it = rows.find(r.first);
            if (it == rows.end()) continue;
            it->score = r.second.score;
            it->matchedBy = fieldName(r.second.bestWeight, r.second.phonetic, r.second.code);
            res.hits.append(*it);
        }
        std::stable_sort(res.hits.begin(), res.hits.end(), [](const CatalogHit &a, const CatalogHit &b) {
            if (a.score != b.score) return a.score > b.score;
            const bool aa = a.registrationStatus == "active" || a.registrationStatus == "unknown";
            const bool ba = b.registrationStatus == "active" || b.registrationStatus == "unknown";
            if (aa != ba) return aa;
            return a.brandName.compare(b.brandName, Qt::CaseInsensitive) < 0;
        });
        if (res.hits.size() > limit) res.hits.resize(limit);
    }

    // 4. Suggestions for likely spelling mistakes.
    if (res.hits.size() < 3) {
        const QString t = toks.first();
        if (t.size() >= 3) {
            QSqlQuery q(m_db);
            q.prepare("SELECT term, MAX(weight) FROM search_index WHERE kind=1 AND term>=? AND term<? "
                      "AND length(term) BETWEEN ? AND ? GROUP BY term LIMIT 20000");
            q.addBindValue(t.left(1));
            q.addBindValue(upper(t.left(1)));
            q.addBindValue(t.size() - 2);
            q.addBindValue(t.size() + 2);
            q.exec();
            QVector<QPair<int, QString>> cands;
            while (q.next()) {
                const QString term = q.value(0).toString();
                if (term == t) continue;
                const int d = TextNormalizer::editDistance(t, term, 2);
                if (d <= 2) cands.append({d * 1000 - q.value(1).toInt(), term});
            }
            std::sort(cands.begin(), cands.end());
            for (int i = 0; i < cands.size() && i < 5; ++i) res.suggestions << cands[i].second;
        }
    }
    return res;
}

bool MedicineSearch::loadRecord(qint64 id, MedicineRecord *r) const
{
    QSqlQuery q(m_db);
    q.prepare("SELECT c.ref_key, c.brand_name, c.generic_name, c.composition, c.strength, c.dosage_form, "
              "m.name, c.mah_name, c.registration_no, c.registration_date, c.registration_status, k.name, "
              "c.pack_size, c.barcode, c.rx_class, c.reference_price, c.price_source, s.code, "
              "c.verification_status, c.verified_at "
              "FROM national_medicine_catalog c "
              "LEFT JOIN manufacturers_reference m ON m.id=c.manufacturer_id "
              "LEFT JOIN medicine_categories_reference k ON k.id=c.category_id "
              "LEFT JOIN medicine_data_sources s ON s.id=c.source_id WHERE c.id=?");
    q.addBindValue(id);
    if (!q.exec() || !q.next()) return false;
    int i = 0;
    r->refKey = q.value(i++).toString();
    r->brandName = q.value(i++).toString();
    r->genericName = q.value(i++).toString();
    r->composition = q.value(i++).toString();
    r->strength = q.value(i++).toString();
    r->dosageForm = q.value(i++).toString();
    r->manufacturer = q.value(i++).toString();
    r->mahName = q.value(i++).toString();
    r->registrationNo = q.value(i++).toString();
    r->registrationDate = q.value(i++).toString();
    r->registrationStatus = q.value(i++).toString();
    r->category = q.value(i++).toString();
    r->packSize = q.value(i++).toString();
    r->barcode = q.value(i++).toString();
    r->rxClass = q.value(i++).toString();
    r->referencePrice = q.value(i++);
    r->priceSource = q.value(i++).toString();
    r->sourceCode = q.value(i++).toString();
    r->verificationStatus = q.value(i++).toString();
    r->verifiedAt = q.value(i++).toString();

    QSqlQuery t(m_db);
    t.prepare("SELECT field, value FROM medicine_translations WHERE medicine_id=? AND lang='ur'");
    t.addBindValue(id);
    t.exec();
    while (t.next()) {
        if (t.value(0).toString() == "brand") r->brandNameUr = t.value(1).toString();
        else if (t.value(0).toString() == "generic") r->genericNameUr = t.value(1).toString();
    }
    QSqlQuery g(m_db);
    g.prepare("SELECT ingredient FROM medicine_ingredients WHERE medicine_id=? ORDER BY position");
    g.addBindValue(id);
    g.exec();
    r->ingredients.clear();
    while (g.next()) r->ingredients << g.value(0).toString();
    return true;
}
}
