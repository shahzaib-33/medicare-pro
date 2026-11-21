#include "PurchaseService.h"
#include "AuthService.h"
#include "InventoryService.h"
#include "SalesService.h"
#include <QDate>

namespace mcp {
// Cash paid to a supplier leaves the cash drawer of the current shift.
static void cashOut(Paisa amount, const QString &method, const QString &refType, qint64 refId)
{
    if (method != "cash" || amount <= 0) return;
    const qint64 shift = ShiftService::currentShift(Session::instance().userId());
    if (shift) insert("INSERT INTO cash_movements(shift_id, ts, kind, amount, ref_type, ref_id) VALUES(?,?,'supplier_payment',?,?,?)",
                      {shift, Db::nowIso(), -amount, refType, refId});
}
}

namespace mcp {

namespace PurchaseService {

PurchaseTotals compute(const PurchaseDraft &d)
{
    PurchaseTotals t;
    for (const auto &l : d.lines) {
        const Paisa gross = Money::times(l.unitCost, l.qty);
        const Paisa net = gross - l.discount;
        const Paisa tax = Money::percentOf(net, l.taxBp);
        t.subtotal += gross;
        t.discount += l.discount;
        t.tax += tax;
        t.lineTotals.append(net + tax);
    }
    t.discount += d.invoiceDiscount;
    t.total = t.subtotal - t.discount + t.tax;
    return t;
}

static void ledger(qint64 supplierId, const QString &type, const QString &refType, qint64 refId, Paisa debit, Paisa credit, const QString &note)
{
    insert("INSERT INTO supplier_ledger(supplier_id, ts, entry_type, ref_type, ref_id, debit, credit, note, user_id) VALUES(?,?,?,?,?,?,?,?,?)",
           {supplierId, Db::nowIso(), type, refType, nullIfZero(refId), debit, credit, nullIf(note), nullIfZero(Session::instance().userId())});
}

Result post(const PurchaseDraft &d)
{
    return transaction([&] {
        require("purchases.post");
        if (!d.supplierId) throw ServiceError{QObject::tr("Select a supplier.")};
        if (d.invoiceNo.trimmed().isEmpty()) throw ServiceError{QObject::tr("Invoice number is required.")};
        if (d.lines.isEmpty()) throw ServiceError{QObject::tr("Add at least one medicine.")};
        const QDate invDate = QDate::fromString(d.invoiceDate, Qt::ISODate);
        if (!invDate.isValid()) throw ServiceError{QObject::tr("Invoice date is required.")};
        if (scalar("SELECT COUNT(*) FROM purchase_invoices WHERE supplier_id=? AND invoice_no=?", {d.supplierId, d.invoiceNo.trimmed()}).toInt())
            throw ServiceError{QObject::tr("Invoice %1 from this supplier is already posted.").arg(d.invoiceNo)};
        for (const auto &l : d.lines) {
            if (l.qty <= 0 || l.bonusQty < 0) throw ServiceError{QObject::tr("Quantities must be more than zero.")};
            if (l.unitCost < 0 || l.discount < 0 || l.discount > Money::times(l.unitCost, l.qty))
                throw ServiceError{QObject::tr("Check cost and discount values.")};
            if (QDate::fromString(l.expiryDate, Qt::ISODate) <= invDate)
                throw ServiceError{QObject::tr("Batch %1 is already expired on the invoice date.").arg(l.batchNo)};
        }
        const PurchaseTotals t = compute(d);
        if (t.total < 0) throw ServiceError{QObject::tr("Invoice total cannot be negative.")};
        if (d.paidNow < 0 || d.paidNow > t.total) throw ServiceError{QObject::tr("Payment cannot exceed the invoice total.")};

        const qint64 inv = insert("INSERT INTO purchase_invoices(supplier_id, invoice_no, invoice_date, subtotal, discount, tax, total, "
                                  "paid, notes, user_id, created_at) VALUES(?,?,?,?,?,?,?,?,?,?,?)",
                                  {d.supplierId, d.invoiceNo.trimmed(), d.invoiceDate, t.subtotal, t.discount, t.tax, t.total,
                                   d.paidNow, nullIf(d.notes), nullIfZero(Session::instance().userId()), Db::nowIso()});
        for (int i = 0; i < d.lines.size(); ++i) {
            const auto &l = d.lines[i];
            // Cost per unit including bonus units, so stock valuation is correct.
            const Paisa effCost = (l.qty + l.bonusQty) > 0 ? t.lineTotals[i] / (l.qty + l.bonusQty) : l.unitCost;
            const qint64 batch = InventoryService::findOrCreateBatch(l.medicineId, l.batchNo, l.mfgDate, l.expiryDate,
                                                                     d.supplierId, effCost, l.salePrice);
            exec("UPDATE medicine_batches SET unit_cost=? WHERE id=? AND qty_received=0", {effCost, batch});
            insert("INSERT INTO purchase_items(invoice_id, medicine_id, batch_id, qty, bonus_qty, unit_cost, discount, tax, line_total) "
                   "VALUES(?,?,?,?,?,?,?,?,?)",
                   {inv, l.medicineId, batch, l.qty, l.bonusQty, l.unitCost, l.discount,
                    Money::percentOf(Money::times(l.unitCost, l.qty) - l.discount, l.taxBp), t.lineTotals[i]});
            InventoryService::move(batch, "available", l.qty + l.bonusQty, "purchase", "purchase_invoice", inv, d.invoiceNo);
            exec("UPDATE medicines SET purchase_price=?, supplier_id=COALESCE(supplier_id, ?), updated_at=? WHERE id=?",
                 {effCost, d.supplierId, Db::nowIso(), l.medicineId});
            // In Pakistan the new price applies to all stock on the shelf, old and new.
            // A sale price on the purchase line becomes the medicine price for every batch.
            const Paisa newPrice = l.salePrice.isValid() ? l.salePrice.toLongLong() : 0;
            if (newPrice > 0) {
                const Paisa oldPrice = scalar("SELECT COALESCE(sale_price,0) FROM medicines WHERE id=?", {l.medicineId}).toLongLong();
                if (oldPrice != newPrice) {
                    exec("UPDATE medicines SET sale_price=?, updated_at=? WHERE id=?", {newPrice, Db::nowIso(), l.medicineId});
                    exec("UPDATE medicine_batches SET sale_price=? WHERE medicine_id=?", {newPrice, l.medicineId});
                    audit("medicine_price_changed", "medicine", l.medicineId,
                          Money::format(oldPrice) + " -> " + Money::format(newPrice) + " (" + d.invoiceNo + ")");
                }
            }
        }
        ledger(d.supplierId, "purchase", "purchase_invoice", inv, 0, t.total, d.invoiceNo);
        if (d.paidNow > 0) {
            insert("INSERT INTO purchase_payments(supplier_id, invoice_id, ts, amount, method, reference, user_id) VALUES(?,?,?,?,?,?,?)",
                   {d.supplierId, inv, Db::nowIso(), d.paidNow, d.paymentMethod, nullIf(d.paymentReference), nullIfZero(Session::instance().userId())});
            ledger(d.supplierId, "payment", "purchase_invoice", inv, d.paidNow, 0, d.paymentMethod);
            cashOut(d.paidNow, d.paymentMethod, "purchase_invoice", inv);
        }
        audit("purchase_posted", "purchase_invoice", inv, d.invoiceNo + " " + Money::format(t.total));
        return Result::success(inv);
    });
}

Result pay(qint64 supplierId, qint64 invoiceId, Paisa amount, const QString &method, const QString &reference)
{
    return transaction([&] {
        require("suppliers.pay");
        if (amount <= 0) throw ServiceError{QObject::tr("Amount must be more than zero.")};
        if (invoiceId) {
            QSqlQuery q = exec("SELECT total, paid, status FROM purchase_invoices WHERE id=? AND supplier_id=?", {invoiceId, supplierId});
            if (!q.next()) throw ServiceError{"invoice not found"};
            if (q.value(2).toString() != "posted") throw ServiceError{QObject::tr("This invoice is reversed.")};
            if (q.value(1).toLongLong() + amount > q.value(0).toLongLong())
                throw ServiceError{QObject::tr("Payment is more than the amount due on this invoice.")};
            exec("UPDATE purchase_invoices SET paid=paid+? WHERE id=?", {amount, invoiceId});
        }
        const qint64 id = insert("INSERT INTO purchase_payments(supplier_id, invoice_id, ts, amount, method, reference, user_id) VALUES(?,?,?,?,?,?,?)",
                                 {supplierId, nullIfZero(invoiceId), Db::nowIso(), amount, method, nullIf(reference), nullIfZero(Session::instance().userId())});
        ledger(supplierId, "payment", "purchase_payment", id, amount, 0, method + " " + reference);
        cashOut(amount, method, "purchase_payment", id);
        audit("supplier_paid", "supplier", supplierId, Money::format(amount));
        return Result::success(id);
    });
}

Result returnToSupplier(qint64 supplierId, qint64 invoiceId, const QVector<ReturnLine> &lines, const QString &reason)
{
    return transaction([&] {
        require("purchases.return");
        if (lines.isEmpty()) throw ServiceError{QObject::tr("Add at least one item to return.")};
        if (reason.trimmed().isEmpty()) throw ServiceError{QObject::tr("A reason is required.")};
        const qint64 ret = insert("INSERT INTO purchase_returns(supplier_id, invoice_id, ts, total, reason, user_id) VALUES(?,?,?,0,?,?)",
                                  {supplierId, nullIfZero(invoiceId), Db::nowIso(), reason.trimmed(), nullIfZero(Session::instance().userId())});
        Paisa total = 0;
        for (const auto &l : lines) {
            if (l.qty <= 0) throw ServiceError{QObject::tr("Quantities must be more than zero.")};
            const Paisa cost = scalar("SELECT unit_cost FROM medicine_batches WHERE id=?", {l.batchId}).toLongLong();
            const Paisa lineTotal = Money::times(cost, l.qty);
            InventoryService::move(l.batchId, l.bucket, -l.qty, "purchase_return", "purchase_return", ret, reason);
            insert("INSERT INTO purchase_return_items(return_id, batch_id, bucket, qty, unit_cost, line_total) VALUES(?,?,?,?,?,?)",
                   {ret, l.batchId, l.bucket, l.qty, cost, lineTotal});
            total += lineTotal;
        }
        exec("UPDATE purchase_returns SET total=? WHERE id=?", {total, ret});
        ledger(supplierId, "return", "purchase_return", ret, total, 0, reason);
        audit("purchase_return", "purchase_return", ret, Money::format(total));
        return Result::success(ret);
    });
}

Result reverse(qint64 invoiceId, const QString &reason)
{
    return transaction([&] {
        require("purchases.reverse");
        if (reason.trimmed().isEmpty()) throw ServiceError{QObject::tr("A reason is required.")};
        QSqlQuery inv = exec("SELECT supplier_id, total, paid, status, invoice_no FROM purchase_invoices WHERE id=?", {invoiceId});
        if (!inv.next()) throw ServiceError{"not found"};
        if (inv.value(3).toString() != "posted") throw ServiceError{QObject::tr("This invoice is already reversed.")};
        QSqlQuery items = exec("SELECT batch_id, qty + bonus_qty FROM purchase_items WHERE invoice_id=?", {invoiceId});
        QList<QPair<qint64, int>> list;
        while (items.next()) list.append({items.value(0).toLongLong(), items.value(1).toInt()});
        for (const auto &[batch, qty] : list)
            InventoryService::move(batch, "available", -qty, "purchase_reversal", "purchase_invoice", invoiceId, reason);
        exec("UPDATE purchase_invoices SET status='reversed', notes=COALESCE(notes,'') || ' Reversed: ' || ? WHERE id=?", {reason, invoiceId});
        // Opposite ledger entry. Payments already made stay as supplier credit.
        ledger(inv.value(0).toLongLong(), "reversal", "purchase_invoice", invoiceId, inv.value(1).toLongLong(), 0, reason);
        audit("purchase_reversed", "purchase_invoice", invoiceId, inv.value(4).toString() + ": " + reason);
        return Result::success(invoiceId);
    });
}
}

namespace SupplierService {
Result save(qint64 id, const QString &name, const QString &phone, const QString &address, const QString &notes, bool active)
{
    return transaction([&] {
        require("suppliers.edit");
        if (name.trimmed().isEmpty()) throw ServiceError{QObject::tr("Supplier name is required.")};
        if (id) exec("UPDATE suppliers SET name=?, phone=?, address=?, notes=?, active=? WHERE id=?",
                     {name.trimmed(), nullIf(phone), nullIf(address), nullIf(notes), active ? 1 : 0, id});
        else id = insert("INSERT INTO suppliers(name, phone, address, notes, active, created_at) VALUES(?,?,?,?,?,?)",
                         {name.trimmed(), nullIf(phone), nullIf(address), nullIf(notes), active ? 1 : 0, Db::nowIso()});
        audit("supplier_saved", "supplier", id, name);
        return Result::success(id);
    });
}

Result openingBalance(qint64 supplierId, Paisa amount)
{
    return transaction([&] {
        require("suppliers.edit");
        if (scalar("SELECT COUNT(*) FROM supplier_ledger WHERE supplier_id=? AND entry_type='opening'", {supplierId}).toInt())
            throw ServiceError{QObject::tr("Opening balance is already recorded. Use an adjustment instead.")};
        insert("INSERT INTO supplier_ledger(supplier_id, ts, entry_type, debit, credit, note, user_id) VALUES(?,?,'opening',?,?,?,?)",
               {supplierId, Db::nowIso(), amount < 0 ? -amount : 0, amount > 0 ? amount : 0, QObject::tr("Opening balance"),
                nullIfZero(Session::instance().userId())});
        return Result::success();
    });
}

Paisa balance(qint64 supplierId)
{
    try { return scalar("SELECT COALESCE(SUM(credit-debit),0) FROM supplier_ledger WHERE supplier_id=?", {supplierId}, 0).toLongLong(); }
    catch (const ServiceError &) { return 0; }
}
}
}
