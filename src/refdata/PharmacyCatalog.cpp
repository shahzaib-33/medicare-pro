#include "PharmacyCatalog.h"
#include "db/Database.h"
#include "services/AuthService.h"
#include "services/InventoryService.h"
#include <QSqlError>
#include <QSqlQuery>

namespace mcp::PharmacyCatalog {


qint64 findByReference(QSqlDatabase db, const QString &refKey)
{
    QSqlQuery q(db);
    q.prepare("SELECT id FROM medicines WHERE reference_ref_key=? AND archived=0");
    q.addBindValue(refKey);
    q.exec();
    return q.next() ? q.value(0).toLongLong() : 0;
}

AddResult addFromReference(QSqlDatabase db, const MedicineRecord &ref, int refVersion, const LocalMedicineSettings &l)
{
    Q_UNUSED(db);
    AddResult res;
    if (const qint64 existing = findByReference(db, ref.refKey)) {
        res.alreadyExists = true;
        res.medicineId = existing;
        res.error = QObject::tr("This medicine is already in your pharmacy list.");
        return res;
    }
    for (const auto &b : l.openingBatches) {
        if (b.batchNo.trimmed().isEmpty() || !b.expiry.isValid() || b.quantity <= 0) {
            res.error = QObject::tr("Each opening batch needs a batch number, an expiry date and a quantity.");
            return res;
        }
    }
    const Result r = transaction([&] {
        require("medicines.edit");
        if (!l.openingBatches.isEmpty()) require("inventory.adjust");
        if (!l.barcode.trimmed().isEmpty()) {
            const QString used = scalar("SELECT brand_name FROM medicines WHERE barcode=?", {l.barcode.trimmed()}).toString();
            if (!used.isEmpty()) throw ServiceError{QObject::tr("Barcode is already used by %1.").arg(used)};
        }
        const QString now = Db::nowIso();
        const qint64 id = insert(
            "INSERT INTO medicines(reference_ref_key, reference_version, reference_hash, brand_name, brand_name_ur, "
            "generic_name, generic_name_ur, active_ingredients, composition, strength, dosage_form, manufacturer, mah_name, "
            "registration_no, registration_status, category, pack_size, units_per_pack, barcode, purchase_price, "
            "sale_price, tax_rate_bp, supplier_id, rack_location, min_stock, rx_required, created_at, updated_at) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            {ref.refKey, refVersion, ref.contentHash(), ref.brandName, nullIf(ref.brandNameUr), nullIf(ref.genericName),
             nullIf(ref.genericNameUr), nullIf(ref.ingredients.join(", ")), nullIf(ref.composition), nullIf(ref.strength),
             nullIf(ref.dosageForm), nullIf(ref.manufacturer), nullIf(ref.mahName), nullIf(ref.registrationNo),
             nullIf(ref.registrationStatus), nullIf(ref.category), nullIf(l.packSize.isEmpty() ? ref.packSize : l.packSize),
             qMax(1, l.unitsPerPack), nullIf(l.barcode), l.purchasePrice, l.salePrice, qMax(0, l.taxRateBp),
             nullIfZero(l.supplierId), nullIf(l.rackLocation), l.minStock, l.rxRequired ? 1 : 0, now, now});
        if (l.unitsPerPack > 1)
            exec("INSERT OR REPLACE INTO medicine_units(medicine_id, unit_name, factor) VALUES(?,?,?)", {id, "pack", l.unitsPerPack});
        for (const auto &b : l.openingBatches) {
            const qint64 batch = InventoryService::findOrCreateBatch(id, b.batchNo, {}, b.expiry.toString(Qt::ISODate),
                                                                     l.supplierId, l.purchasePrice.toLongLong(), {});
            InventoryService::move(batch, "available", b.quantity, "opening", "opening", 0, QObject::tr("Opening stock"));
        }
        audit("medicine_added_from_reference", "medicine", id, ref.refKey);
        return Result::success(id);
    });
    res.ok = r.ok;
    res.error = r.error;
    res.medicineId = r.id;
    return res;
}

int flagChangedProducts(QSqlDatabase db, QSqlDatabase refDb, int newVersion)
{
    QSqlQuery local(db);
    local.exec("SELECT id, reference_ref_key, registration_no, registration_status FROM medicines "
               "WHERE reference_ref_key IS NOT NULL AND archived=0");
    int flagged = 0;
    while (local.next()) {
        const qint64 id = local.value(0).toLongLong();
        const QString key = local.value(1).toString();
        const QString oldReg = local.value(2).toString();
        const QString oldStatus = local.value(3).toString();
        QSqlQuery r(refDb);
        r.prepare("SELECT registration_no, registration_status, version_changed FROM national_medicine_catalog WHERE ref_key=?");
        r.addBindValue(key);
        if (!r.exec() || !r.next()) continue;
        if (r.value(2).toInt() != newVersion) continue;
        const QString newReg = r.value(0).toString();
        const QString newStatus = r.value(1).toString();
        auto flag = [&](const QString &reason, const QString &o, const QString &n) {
            QSqlQuery ex(db);
            ex.prepare("SELECT 1 FROM medicine_review_flags WHERE medicine_id=? AND reason=? AND new_value IS ? AND resolved_at IS NULL");
            ex.addBindValue(id); ex.addBindValue(reason); ex.addBindValue(n);
            ex.exec();
            if (ex.next()) return;
            QSqlQuery f(db);
            f.prepare("INSERT INTO medicine_review_flags(medicine_id, ref_key, reason, old_value, new_value, "
                      "reference_version, created_at) VALUES(?,?,?,?,?,?,?)");
            f.addBindValue(id); f.addBindValue(key); f.addBindValue(reason);
            f.addBindValue(o); f.addBindValue(n); f.addBindValue(newVersion); f.addBindValue(Db::nowIso());
            if (f.exec()) ++flagged;
        };
        if (newStatus != oldStatus && !newStatus.isEmpty()) flag("registration_status_changed", oldStatus, newStatus);
        if (newReg != oldReg) flag("registration_no_changed", oldReg, newReg);
        if (newStatus == oldStatus && newReg == oldReg) flag("reference_details_changed", {}, {});
    }
    return flagged;
}

int applyReferencePrices(QSqlDatabase db, QSqlDatabase refDb, int newVersion)
{
    QSqlQuery local(db);
    local.exec("SELECT id, reference_ref_key, COALESCE(sale_price,0), units_per_pack, brand_name FROM medicines "
               "WHERE reference_ref_key IS NOT NULL AND archived=0");
    int changed = 0;
    while (local.next()) {
        const qint64 id = local.value(0).toLongLong();
        QSqlQuery r(refDb);
        r.prepare("SELECT reference_price, version_changed FROM national_medicine_catalog WHERE ref_key=?");
        r.addBindValue(local.value(1).toString());
        if (!r.exec() || !r.next() || r.value(0).isNull() || r.value(1).toInt() != newVersion) continue;
        const double packPrice = r.value(0).toDouble();
        if (packPrice <= 0) continue;
        const int perPack = qMax(1, local.value(3).toInt());
        const qint64 newPrice = qRound64(packPrice * 100.0 / perPack);
        const qint64 oldPrice = local.value(2).toLongLong();
        if (newPrice <= 0 || newPrice == oldPrice) continue;
        const QString now = Db::nowIso();
        QSqlQuery u(db);
        u.prepare("UPDATE medicines SET sale_price=?, updated_at=? WHERE id=?");
        u.addBindValue(newPrice); u.addBindValue(now); u.addBindValue(id);
        if (!u.exec()) continue;
        QSqlQuery b(db);
        b.prepare("UPDATE medicine_batches SET sale_price=? WHERE medicine_id=?");
        b.addBindValue(newPrice); b.addBindValue(id);
        b.exec();
        QSqlQuery a(db);
        a.prepare("INSERT INTO audit_logs(ts, action, entity, entity_id, details) VALUES(?,?,?,?,?)");
        a.addBindValue(now); a.addBindValue("medicine_price_changed"); a.addBindValue("medicine"); a.addBindValue(id);
        a.addBindValue(QString("%1: %2 -> %3 (online update %4)").arg(local.value(4).toString())
                           .arg(oldPrice / 100.0, 0, 'f', 2).arg(newPrice / 100.0, 0, 'f', 2).arg(newVersion));
        a.exec();
        ++changed;
    }
    return changed;
}

int openReviewFlags(QSqlDatabase db)
{
    QSqlQuery q(db);
    q.exec("SELECT COUNT(*) FROM medicine_review_flags WHERE resolved_at IS NULL");
    return q.next() ? q.value(0).toInt() : 0;
}

int stockOf(QSqlDatabase db, qint64 medicineId)
{
    QSqlQuery q(db);
    q.prepare("SELECT COALESCE(SUM(qty_available),0) FROM medicine_batches WHERE medicine_id=?");
    q.addBindValue(medicineId);
    q.exec();
    return q.next() ? q.value(0).toInt() : 0;
}
}
