#include "SearchIndexer.h"
#include "TextNormalizer.h"
#include <QCryptographicHash>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>

namespace mcp {

QString MedicineRecord::contentHash() const
{
    QStringList parts = {brandName, brandNameUr, genericName, genericNameUr, ingredients.join('|'),
                         composition, strength, dosageForm, manufacturer, mahName, registrationNo,
                         registrationDate, registrationStatus, category, packSize, barcode, rxClass,
                         referencePrice.isNull() ? QString() : referencePrice.toString(), priceSource,
                         sourceCode, verificationStatus};
    return QString::fromLatin1(
        QCryptographicHash::hash(parts.join(QChar(0x1F)).toUtf8(), QCryptographicHash::Sha256).toHex());
}

namespace SearchIndexer {

bool reindex(QSqlDatabase db, qint64 id, const MedicineRecord &r, QString *err)
{
    QSqlQuery del(db);
    del.prepare("DELETE FROM search_index WHERE medicine_id=?");
    del.addBindValue(id);
    if (!del.exec()) { if (err) *err = del.lastError().text(); return false; }

    struct Entry { QString term; int kind; int weight; };
    QList<Entry> entries;
    QSet<QString> seen;
    auto addText = [&](const QString &text, int weight) {
        for (const QString &t : TextNormalizer::tokens(text)) {
            if (t.size() < 1) continue;
            if (!seen.contains("w" + t)) { seen.insert("w" + t); entries.append({t, Word, weight}); }
            const QString pk = TextNormalizer::phoneticKey(t);
            if (pk.size() >= 2 && !seen.contains("p" + pk)) {
                seen.insert("p" + pk);
                entries.append({pk, Phonetic, weight});
            }
        }
    };
    // Weight tells the ranker which field matched. Higher is better.
    addText(r.brandName, 100);
    addText(r.brandNameUr, 95);
    addText(r.genericName, 80);
    addText(r.genericNameUr, 78);
    for (const QString &i : r.ingredients) addText(i, 70);
    addText(r.composition, 60);
    addText(r.strength, 40);
    addText(r.dosageForm, 30);
    addText(r.manufacturer, 50);
    addText(r.mahName, 45);
    for (const QString &code : {r.registrationNo, r.barcode}) {
        const QString c = TextNormalizer::normalize(code).remove(' ');
        if (!c.isEmpty() && !seen.contains("c" + c)) { seen.insert("c" + c); entries.append({c, Code, 90}); }
    }

    QSqlQuery ins(db);
    ins.prepare("INSERT INTO search_index(term, kind, medicine_id, weight) VALUES(?,?,?,?)");
    for (const Entry &e : entries) {
        ins.bindValue(0, e.term);
        ins.bindValue(1, e.kind);
        ins.bindValue(2, id);
        ins.bindValue(3, e.weight);
        if (!ins.exec()) { if (err) *err = ins.lastError().text(); return false; }
    }
    return true;
}
}
}
