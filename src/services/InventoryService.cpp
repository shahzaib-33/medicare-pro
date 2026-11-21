#include "InventoryService.h"
#include "AuthService.h"
#include <QRegularExpression>

namespace mcp::InventoryService {

static void validate(const MedicineInput &m)
{
    if (m.brandName.trimmed().isEmpty()) throw ServiceError{QObject::tr("Brand name is required.")};
    if (m.unitsPerPack < 1) throw ServiceError{QObject::tr("Units per pack must be at least 1.")};
    if (m.purchasePrice < 0 || m.salePrice < 0) throw ServiceError{QObject::tr("Prices cannot be negative.")};
    if (m.taxRateBp < 0 || m.taxRateBp > 10000) throw ServiceError{QObject::tr("Tax rate must be between 0 and 100 percent.")};
    if (!m.barcode.trimmed().isEmpty() && !QRegularExpression("^[0-9A-Za-z\\-]{4,32}$").match(m.barcode.trimmed()).hasMatch())
        throw ServiceError{QObject::tr("Barcode has invalid characters.")};
}

static void checkBarcodeFree(const QString &barcode, qint64 exceptMedicine)
{
    if (barcode.trimmed().isEmpty()) return;
    const qint64 a = scalar("SELECT id FROM medicines WHERE barcode=? AND id<>?", {barcode.trimmed(), exceptMedicine}, 0).toLongLong();
    const qint64 b = scalar("SELECT medicine_id FROM medicine_barcodes WHERE barcode=? AND medicine_id<>?", {barcode.trimmed(), exceptMedicine}, 0).toLongLong();
    if (a || b) throw ServiceError{QObject::tr("This barcode is already used by another medicine.")};
}

Result addMedicine(const MedicineInput &m)
{
    return transaction([&] {
        require("medicines.edit");
        validate(m);
        checkBarcodeFree(m.barcode, 0);
        const QString now = Db::nowIso();
        const qint64 id = insert(
            "INSERT INTO medicines(brand_name, brand_name_ur, generic_name, generic_name_ur, active_ingredients, composition, "
            "strength, dosage_form, manufacturer, category, pack_size, base_unit, units_per_pack, barcode, registration_no, "
            "purchase_price, sale_price, tax_rate_bp, supplier_id, rack_location, min_stock, rx_required, locally_edited, "
            "created_at, updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,1,?,?)",
            {m.brandName.trimmed(), nullIf(m.brandNameUr), nullIf(m.genericName), nullIf(m.genericNameUr), nullIf(m.ingredients),
             nullIf(m.composition), nullIf(m.strength), nullIf(m.dosageForm), nullIf(m.manufacturer), nullIf(m.category),
             nullIf(m.packSize), m.baseUnit.trimmed().isEmpty() ? QString("unit") : m.baseUnit.trimmed(), m.unitsPerPack,
             nullIf(m.barcode), nullIf(m.registrationNo), m.purchasePrice, m.salePrice, m.taxRateBp, nullIfZero(m.supplierId),
             nullIf(m.rackLocation), m.minStock, m.rxRequired ? 1 : 0, now, now});
        if (m.unitsPerPack > 1)
            exec("INSERT OR REPLACE INTO medicine_units(medicine_id, unit_name, factor) VALUES(?,?,?)", {id, "pack", m.unitsPerPack});
        if (!m.manufacturer.trimmed().isEmpty()) exec("INSERT OR IGNORE INTO manufacturers(name) VALUES(?)", {m.manufacturer.trimmed()});
        if (!m.category.trimmed().isEmpty()) exec("INSERT OR IGNORE INTO medicine_categories(name) VALUES(?)", {m.category.trimmed()});
        audit("medicine_added", "medicine", id, m.brandName);
        return Result::success(id);
    });
}

Result updateMedicine(qint64 id, const MedicineInput &m)
{
    return transaction([&] {
        require("medicines.edit");
        validate(m);
        checkBarcodeFree(m.barcode, id);
        const QString old = scalar("SELECT sale_price || '/' || COALESCE(purchase_price,'') FROM medicines WHERE id=?", {id}).toString();
        exec("UPDATE medicines SET brand_name=?, brand_name_ur=?, generic_name=?, generic_name_ur=?, active_ingredients=?, "
             "composition=?, strength=?, dosage_form=?, manufacturer=?, category=?, pack_size=?, base_unit=?, units_per_pack=?, "
             "barcode=?, registration_no=?, purchase_price=?, sale_price=?, tax_rate_bp=?, supplier_id=?, rack_location=?, "
             "min_stock=?, rx_required=?, locally_edited=1, updated_at=? WHERE id=?",
             {m.brandName.trimmed(), nullIf(m.brandNameUr), nullIf(m.genericName), nullIf(m.genericNameUr), nullIf(m.ingredients),
              nullIf(m.composition), nullIf(m.strength), nullIf(m.dosageForm), nullIf(m.manufacturer), nullIf(m.category),
              nullIf(m.packSize), m.baseUnit.trimmed().isEmpty() ? QString("unit") : m.baseUnit.trimmed(), m.unitsPerPack,
              nullIf(m.barcode), nullIf(m.registrationNo), m.purchasePrice, m.salePrice, m.taxRateBp, nullIfZero(m.supplierId),
              nullIf(m.rackLocation), m.minStock, m.rxRequired ? 1 : 0, Db::nowIso(), id});
        if (m.unitsPerPack > 1)
            exec("INSERT OR REPLACE INTO medicine_units(medicine_id, unit_name, factor) VALUES(?,?,?)", {id, "pack", m.unitsPerPack});
        audit("medicine_updated", "medicine", id, "prices before " + old);
        return Result::success(id);
    });
}

Result archiveMedicine(qint64 id, bool archived)
{
    return transaction([&] {
        require("medicines.delete");
        exec("UPDATE medicines SET archived=?, updated_at=? WHERE id=?", {archived ? 1 : 0, Db::nowIso(), id});
        audit(archived ? "medicine_archived" : "medicine_restored", "medicine", id);
        return Result::success(id);
    });
}

Result deleteMedicine(qint64 id)
{
    return transaction([&] {
        require("medicines.delete");
        const int refs = scalar("SELECT (SELECT COUNT(*) FROM medicine_batches WHERE medicine_id=?) + "
                                "(SELECT COUNT(*) FROM sale_items WHERE medicine_id=?) + "
                                "(SELECT COUNT(*) FROM purchase_items WHERE medicine_id=?) + "
                                "(SELECT COUNT(*) FROM prescription_items WHERE medicine_id=?)", {id, id, id, id}).toInt();
        if (refs > 0) throw ServiceError{QObject::tr("This medicine has stock or history. Archive it instead of deleting.")};
        exec("DELETE FROM medicine_review_flags WHERE medicine_id=?", {id});
        exec("DELETE FROM medicines WHERE id=?", {id});
        audit("medicine_deleted", "medicine", id);
        return Result::success(id);
    });
}

Result setUnits(qint64 medicineId, const QList<QPair<QString, int>> &units)
{
    return transaction([&] {
        require("medicines.edit");
        exec("DELETE FROM medicine_units WHERE medicine_id=?", {medicineId});
        for (const auto &u : units) {
            if (u.first.trimmed().isEmpty() || u.second < 1) throw ServiceError{QObject::tr("Each unit needs a name and a factor of 1 or more.")};
            exec("INSERT INTO medicine_units(medicine_id, unit_name, factor) VALUES(?,?,?)", {medicineId, u.first.trimmed(), u.second});
        }
        audit("units_changed", "medicine", medicineId);
        return Result::success();
    });
}

Result addBarcode(qint64 medicineId, const QString &barcode, const QString &unitName)
{
    return transaction([&] {
        require("medicines.edit");
        checkBarcodeFree(barcode, medicineId);
        exec("INSERT OR REPLACE INTO medicine_barcodes(barcode, medicine_id, unit_name) VALUES(?,?,?)",
             {barcode.trimmed(), medicineId, nullIf(unitName)});
        return Result::success();
    });
}

qint64 findByBarcode(const QString &barcode, int *unitFactor)
{
    if (unitFactor) *unitFactor = 1;
    QSqlQuery q(pharmacyDb());
    q.prepare("SELECT id FROM medicines WHERE barcode=? AND archived=0");
    q.addBindValue(barcode.trimmed());
    q.exec();
    if (q.next()) return q.value(0).toLongLong();
    q.prepare("SELECT b.medicine_id, COALESCE(u.factor,1) FROM medicine_barcodes b JOIN medicines m ON m.id=b.medicine_id "
              "LEFT JOIN medicine_units u ON u.medicine_id=b.medicine_id AND u.unit_name=b.unit_name "
              "WHERE b.barcode=? AND m.archived=0");
    q.addBindValue(barcode.trimmed());
    q.exec();
    if (q.next()) { if (unitFactor) *unitFactor = q.value(1).toInt(); return q.value(0).toLongLong(); }
    return 0;
}

qint64 findOrCreateBatch(qint64 medicineId, const QString &batchNo, const QString &mfg, const QString &expiry,
                         qint64 supplierId, Paisa unitCost, const QVariant &salePrice)
{
    if (batchNo.trimmed().isEmpty()) throw ServiceError{QObject::tr("Batch number is required.")};
    const QDate exp = QDate::fromString(expiry, Qt::ISODate);
    if (!exp.isValid()) throw ServiceError{QObject::tr("Expiry date is required.")};
    if (!mfg.isEmpty() && QDate::fromString(mfg, Qt::ISODate) >= exp)
        throw ServiceError{QObject::tr("Manufacturing date must be before expiry date.")};
    QSqlQuery q = exec("SELECT id, expiry_date FROM medicine_batches WHERE medicine_id=? AND batch_no=?", {medicineId, batchNo.trimmed()});
    if (q.next()) {
        if (q.value(1).toString() != expiry)
            throw ServiceError{QObject::tr("Batch %1 already exists with a different expiry date.").arg(batchNo)};
        return q.value(0).toLongLong();
    }
    return insert("INSERT INTO medicine_batches(medicine_id, batch_no, mfg_date, expiry_date, supplier_id, unit_cost, sale_price, created_at) "
                  "VALUES(?,?,?,?,?,?,?,?)",
                  {medicineId, batchNo.trimmed(), nullIf(mfg), expiry, nullIfZero(supplierId), unitCost, salePrice, Db::nowIso()});
}

void move(qint64 batchId, const QString &bucket, int qty, const QString &reason, const QString &refType, qint64 refId, const QString &note)
{
    if (qty == 0) return;
    static const QHash<QString, QString> col = {{"available", "qty_available"}, {"quarantine", "qty_quarantine"}, {"damaged", "qty_damaged"}};
    const QString c = col.value(bucket);
    if (c.isEmpty()) throw ServiceError{"bad bucket"};
    QSqlQuery b = exec("SELECT medicine_id, " + c + ", unit_cost FROM medicine_batches WHERE id=?", {batchId});
    if (!b.next()) throw ServiceError{QObject::tr("Batch not found.")};
    if (b.value(1).toInt() + qty < 0)
        throw ServiceError{QObject::tr("Not enough stock in this batch. Negative stock is not allowed.")};
    exec("UPDATE medicine_batches SET " + c + "=" + c + "+? WHERE id=?", {qty, batchId});
    if (qty > 0 && bucket == "available" && (reason == "purchase" || reason == "opening"))
        exec("UPDATE medicine_batches SET qty_received=qty_received+? WHERE id=?", {qty, batchId});
    insert("INSERT INTO stock_movements(ts, medicine_id, batch_id, bucket, qty, reason, ref_type, ref_id, unit_cost, user_id, note) "
           "VALUES(?,?,?,?,?,?,?,?,?,?,?)",
           {Db::nowIso(), b.value(0), batchId, bucket, qty, reason, nullIf(refType), nullIfZero(refId), b.value(2),
            nullIfZero(Session::instance().userId()), nullIf(note)});
}

QVector<BatchAllocation> allocateFefo(qint64 medicineId, int qty, const QString &today)
{
    QVector<BatchAllocation> out;
    if (qty <= 0) throw ServiceError{QObject::tr("Quantity must be more than zero.")};
    // Expired, recalled and quarantined stock is never offered. Earliest expiry first.
    QSqlQuery q = exec("SELECT id, qty_available, unit_cost, batch_no, expiry_date FROM medicine_batches "
                       "WHERE medicine_id=? AND status='active' AND qty_available>0 AND expiry_date>? "
                       "ORDER BY expiry_date, id", {medicineId, today});
    int remaining = qty;
    while (remaining > 0 && q.next()) {
        const int take = qMin(remaining, q.value(1).toInt());
        out.append({q.value(0).toLongLong(), take, q.value(2).toLongLong(), q.value(3).toString(), q.value(4).toString()});
        remaining -= take;
    }
    if (remaining > 0) {
        const QString name = scalar("SELECT brand_name FROM medicines WHERE id=?", {medicineId}).toString();
        const int expired = scalar("SELECT COALESCE(SUM(qty_available),0) FROM medicine_batches WHERE medicine_id=? AND expiry_date<=?",
                                   {medicineId, today}).toInt();
        if (expired > 0 && qty - remaining == 0)
            throw ServiceError{QObject::tr("%1: only expired stock is left. Expired medicine cannot be sold.").arg(name)};
        throw ServiceError{QObject::tr("%1: only %2 unexpired units in stock.").arg(name).arg(qty - remaining)};
    }
    return out;
}

Result openingStock(qint64 medicineId, const QString &batchNo, const QString &expiry, int qty, Paisa unitCost)
{
    return transaction([&] {
        require("inventory.adjust");
        if (qty <= 0) throw ServiceError{QObject::tr("Quantity must be more than zero.")};
        const qint64 b = findOrCreateBatch(medicineId, batchNo, {}, expiry, 0, unitCost, {});
        move(b, "available", qty, "opening", "opening", 0, QObject::tr("Opening stock"));
        audit("opening_stock", "batch", b, QString::number(qty));
        return Result::success(b);
    });
}

Result adjust(qint64 batchId, const QString &kind, int qty, const QString &reason)
{
    return transaction([&] {
        require("inventory.adjust");
        if (qty <= 0) throw ServiceError{QObject::tr("Quantity must be more than zero.")};
        if (reason.trimmed().isEmpty()) throw ServiceError{QObject::tr("A reason is required for every stock change.")};
        const qint64 adj = insert("INSERT INTO stock_adjustments(ts, user_id, batch_id, kind, qty, reason) VALUES(?,?,?,?,?,?)",
                                  {Db::nowIso(), nullIfZero(Session::instance().userId()), batchId, kind, qty, reason.trimmed()});
        if (kind == "damage") {
            move(batchId, "available", -qty, "damage", "adjustment", adj, reason);
            move(batchId, "damaged", qty, "damage", "adjustment", adj, reason);
        } else if (kind == "correction_in") {
            move(batchId, "available", qty, "correction", "adjustment", adj, reason);
        } else if (kind == "correction_out") {
            move(batchId, "available", -qty, "correction", "adjustment", adj, reason);
        } else if (kind == "dispose_damaged") {
            move(batchId, "damaged", -qty, "disposal", "adjustment", adj, reason);
        } else if (kind == "dispose_expired") {
            const QString exp = scalar("SELECT expiry_date FROM medicine_batches WHERE id=?", {batchId}).toString();
            if (exp > todayIso()) throw ServiceError{QObject::tr("This batch has not expired yet.")};
            move(batchId, "available", -qty, "expired_disposal", "adjustment", adj, reason);
        } else {
            throw ServiceError{"unknown adjustment"};
        }
        audit("stock_adjusted", "batch", batchId, kind + " " + QString::number(qty) + ": " + reason);
        return Result::success(adj);
    });
}

Result setBatchStatus(qint64 batchId, const QString &status, const QString &note)
{
    return transaction([&] {
        require("inventory.adjust");
        if (status != "active" && status != "recalled") throw ServiceError{"bad status"};
        exec("UPDATE medicine_batches SET status=?, status_note=? WHERE id=?", {status, nullIf(note), batchId});
        audit("batch_status", "batch", batchId, status + " " + note);
        return Result::success();
    });
}

Result inspectReturn(qint64 itemId, bool restock, const QString &note)
{
    return transaction([&] {
        require("inventory.inspect");
        QSqlQuery q = exec("SELECT batch_id, qty, disposition, return_id FROM sales_return_items WHERE id=?", {itemId});
        if (!q.next()) throw ServiceError{"not found"};
        if (q.value(2).toString() != "pending") throw ServiceError{QObject::tr("This return was already inspected.")};
        const qint64 batch = q.value(0).toLongLong();
        const int qty = q.value(1).toInt();
        if (restock) {
            const QString exp = scalar("SELECT expiry_date FROM medicine_batches WHERE id=?", {batch}).toString();
            if (exp <= todayIso()) throw ServiceError{QObject::tr("Expired stock cannot be restocked.")};
        }
        move(batch, "quarantine", -qty, "inspection", "sales_return", q.value(3).toLongLong(), note);
        move(batch, restock ? "available" : "damaged", qty, "inspection", "sales_return", q.value(3).toLongLong(), note);
        exec("UPDATE sales_return_items SET disposition=?, inspected_by=?, inspected_at=? WHERE id=?",
             {restock ? "restocked" : "damaged", nullIfZero(Session::instance().userId()), Db::nowIso(), itemId});
        audit("return_inspected", "sales_return_item", itemId, restock ? "restocked" : "damaged");
        return Result::success();
    });
}

qint64 startCount(const QString &note)
{
    const Result r = transaction([&] {
        require("inventory.adjust");
        const qint64 id = insert("INSERT INTO stock_counts(started_at, user_id, status, note) VALUES(?,?,'open',?)",
                                 {Db::nowIso(), nullIfZero(Session::instance().userId()), nullIf(note)});
        exec("INSERT INTO stock_count_items(count_id, batch_id, expected) SELECT ?, id, qty_available FROM medicine_batches "
             "WHERE qty_available>0", {id});
        return Result::success(id);
    });
    return r.ok ? r.id : 0;
}

Result recordCount(qint64 countId, qint64 batchId, int counted)
{
    return transaction([&] {
        require("inventory.adjust");
        if (counted < 0) throw ServiceError{QObject::tr("Counted quantity cannot be negative.")};
        exec("INSERT INTO stock_count_items(count_id, batch_id, expected, counted) VALUES(?,?, "
             "(SELECT qty_available FROM medicine_batches WHERE id=?), ?) "
             "ON CONFLICT(count_id, batch_id) DO UPDATE SET counted=excluded.counted", {countId, batchId, batchId, counted});
        return Result::success();
    });
}

Result finishCount(qint64 countId)
{
    return transaction([&] {
        require("inventory.adjust");
        if (scalar("SELECT status FROM stock_counts WHERE id=?", {countId}).toString() != "open")
            throw ServiceError{QObject::tr("This stock count is already closed.")};
        // Reconcile against the live quantity so sales during the count are respected.
        QSqlQuery q = exec("SELECT i.batch_id, i.expected, i.counted, b.qty_available FROM stock_count_items i "
                           "JOIN medicine_batches b ON b.id=i.batch_id WHERE i.count_id=? AND i.counted IS NOT NULL", {countId});
        QList<std::tuple<qint64, int>> diffs;
        while (q.next()) {
            const int soldDuring = q.value(1).toInt() - q.value(3).toInt();
            const int target = q.value(2).toInt() - soldDuring;
            const int diff = qMax(0, target) - q.value(3).toInt();
            if (diff != 0) diffs.append({q.value(0).toLongLong(), diff});
        }
        for (const auto &[batch, diff] : diffs)
            move(batch, "available", diff, "stock_count", "stock_count", countId, QObject::tr("Stock count difference"));
        exec("UPDATE stock_counts SET status='closed', finished_at=? WHERE id=?", {Db::nowIso(), countId});
        audit("stock_count_closed", "stock_count", countId, QString::number(diffs.size()) + " differences");
        return Result::success(countId);
    });
}

int saleableStock(qint64 medicineId, const QString &today)
{
    try {
    return scalar("SELECT COALESCE(SUM(qty_available),0) FROM medicine_batches WHERE medicine_id=? AND status='active' "
                  "AND expiry_date>?", {medicineId, today}, 0).toInt();
    } catch (const ServiceError &) { return 0; }
}

int totalStock(qint64 medicineId)
{
    try {
    return scalar("SELECT COALESCE(SUM(qty_available+qty_quarantine),0) FROM medicine_batches WHERE medicine_id=?", {medicineId}, 0).toInt();
    } catch (const ServiceError &) { return 0; }
}
}
