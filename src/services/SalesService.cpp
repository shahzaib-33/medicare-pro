#include "SalesService.h"
#include "AuthService.h"
#include "InventoryService.h"
#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>

namespace mcp {

namespace PaymentMethods {
const QStringList &all()
{
    static const QStringList m = {"cash", "bank_transfer", "card", "jazzcash", "easypaisa", "raast", "customer_credit"};
    return m;
}
QString label(const QString &c)
{
    if (c == "cash") return QObject::tr("Cash");
    if (c == "bank_transfer") return QObject::tr("Bank transfer");
    if (c == "card") return QObject::tr("Debit or credit card");
    if (c == "jazzcash") return QObject::tr("JazzCash");
    if (c == "easypaisa") return QObject::tr("Easypaisa");
    if (c == "raast") return QObject::tr("Raast");
    if (c == "customer_credit") return QObject::tr("Customer credit");
    return c;
}
}

QJsonObject Cart::toJson() const
{
    QJsonArray ls;
    for (const auto &l : lines)
        ls.append(QJsonObject{{"m", l.medicineId}, {"n", l.name}, {"q", l.qty}, {"p", QString::number(l.unitPrice)},
                              {"d", QString::number(l.discount)}, {"t", l.taxBp}, {"rx", l.rxRequired}});
    return QJsonObject{{"lines", ls}, {"bd", QString::number(billDiscount)}, {"c", customerId}, {"rx", prescriptionId}, {"notes", notes}};
}

Cart Cart::fromJson(const QJsonObject &o)
{
    Cart c;
    for (const QJsonValue &v : o.value("lines").toArray()) {
        const QJsonObject l = v.toObject();
        c.lines.append({l.value("m").toInteger(), l.value("n").toString(), l.value("q").toInt(), l.value("p").toString().toLongLong(),
                        l.value("d").toString().toLongLong(), l.value("t").toInt(), l.value("rx").toBool()});
    }
    c.billDiscount = o.value("bd").toString().toLongLong();
    c.customerId = o.value("c").toInteger();
    c.prescriptionId = o.value("rx").toInteger();
    c.notes = o.value("notes").toString();
    return c;
}

namespace SalesService {

CartTotals compute(const Cart &c)
{
    CartTotals t;
    for (const auto &l : c.lines) {
        const Paisa gross = Money::times(l.unitPrice, l.qty);
        const Paisa net = gross - l.discount;
        const Paisa tax = Money::percentOf(net, l.taxBp);
        t.subtotal += gross;
        t.lineDiscount += l.discount;
        t.tax += tax;
        t.lineTotals.append(net + tax);
    }
    t.billDiscount = c.billDiscount;
    t.total = t.subtotal - t.lineDiscount - t.billDiscount + t.tax;
    Paisa cash = 0, other = 0;
    for (const auto &p : c.payments) {
        if (p.method == "customer_credit") t.credit += p.amount;
        else if (p.method == "cash") cash += p.amount;
        else other += p.amount;
    }
    t.paid = cash + other;
    const Paisa covered = t.paid + t.credit;
    t.change = covered > t.total ? qMin(cash, covered - t.total) : 0;
    t.due = qMax<Paisa>(0, t.total - covered);
    return t;
}

bool fillLine(qint64 medicineId, int qty, CartLine *l, QString *err)
{
    try {
        QSqlQuery q = exec("SELECT brand_name, COALESCE(strength,''), sale_price, tax_rate_bp, rx_required, archived FROM medicines WHERE id=?", {medicineId});
        if (!q.next()) { *err = QObject::tr("Medicine not found."); return false; }
        if (q.value(5).toBool()) { *err = QObject::tr("This medicine is archived."); return false; }
        if (q.value(2).isNull() || q.value(2).toLongLong() <= 0) { *err = QObject::tr("Set a sale price for %1 first.").arg(q.value(0).toString()); return false; }
        l->medicineId = medicineId;
        l->name = (q.value(0).toString() + ' ' + q.value(1).toString()).trimmed();
        l->qty = qty;
        l->unitPrice = q.value(2).toLongLong();
        l->taxBp = q.value(3).toInt();
        l->rxRequired = q.value(4).toBool();
        l->discount = 0;
        return true;
    } catch (const ServiceError &e) { *err = e.message; return false; }
}

static QString nextInvoiceNo()
{
    const QString prefix = "S" + QDate::currentDate().toString("yyMMdd") + "-";
    const int n = scalar("SELECT COUNT(*) FROM sales WHERE invoice_no LIKE ?", {prefix + "%"}).toInt() + 1;
    return prefix + QString("%1").arg(n, 4, 10, QChar('0'));
}

static void customerLedger(qint64 customerId, const QString &type, const QString &refType, qint64 refId, Paisa debit, Paisa credit, const QString &note)
{
    insert("INSERT INTO customer_ledger(customer_id, ts, entry_type, ref_type, ref_id, debit, credit, note, user_id) VALUES(?,?,?,?,?,?,?,?,?)",
           {customerId, Db::nowIso(), type, refType, nullIfZero(refId), debit, credit, nullIf(note), nullIfZero(Session::instance().userId())});
}

static void cashMove(qint64 shiftId, const QString &kind, Paisa amount, const QString &refType, qint64 refId, const QString &note = {})
{
    if (!shiftId || amount == 0) return;
    insert("INSERT INTO cash_movements(shift_id, ts, kind, amount, ref_type, ref_id, note) VALUES(?,?,?,?,?,?,?)",
           {shiftId, Db::nowIso(), kind, amount, nullIf(refType), nullIfZero(refId), nullIf(note)});
}

Result post(const Cart &c, bool offline)
{
    return transaction([&] {
        require("pos.sell");
        if (c.lines.isEmpty()) throw ServiceError{QObject::tr("The bill is empty.")};
        const CartTotals t = compute(c);
        for (const auto &l : c.lines) {
            if (l.qty <= 0) throw ServiceError{QObject::tr("Quantities must be more than zero.")};
            if (l.discount < 0 || l.discount > Money::times(l.unitPrice, l.qty)) throw ServiceError{QObject::tr("Line discount is not valid.")};
            const Paisa listPrice = scalar("SELECT sale_price FROM medicines WHERE id=?", {l.medicineId}).toLongLong();
            if (l.unitPrice != listPrice && !Session::instance().can("pos.price_override"))
                throw ServiceError{QObject::tr("You may not change the sale price.")};
            if (l.discount > 0 && !Session::instance().can("pos.discount")) throw ServiceError{QObject::tr("You may not give discounts.")};
        }
        if (c.billDiscount < 0 || c.billDiscount > t.subtotal - t.lineDiscount + t.tax) throw ServiceError{QObject::tr("Bill discount is not valid.")};
        if (c.billDiscount > 0 && !Session::instance().can("pos.discount")) throw ServiceError{QObject::tr("You may not give discounts.")};

        // Prescription medicines need an approved prescription, or a pharmacist making the sale.
        bool needsRx = false;
        for (const auto &l : c.lines) needsRx |= l.rxRequired;
        if (needsRx) {
            const QString st = c.prescriptionId ? scalar("SELECT status FROM prescriptions WHERE id=?", {c.prescriptionId}).toString() : QString();
            if (st != "approved" && !Session::instance().can("prescriptions.approve"))
                throw ServiceError{QObject::tr("This bill has prescription medicines. Link an approved prescription or ask the pharmacist.")};
        }

        // Payments
        Paisa cash = 0, nonCash = 0;
        for (const auto &p : c.payments) {
            if (p.amount <= 0) throw ServiceError{QObject::tr("Payment amounts must be more than zero.")};
            if (!PaymentMethods::all().contains(p.method)) throw ServiceError{"unknown payment method"};
            if (p.method == "cash") cash += p.amount; else if (p.method != "customer_credit") nonCash += p.amount;
        }
        if (t.credit > 0) {
            if (!c.customerId) throw ServiceError{QObject::tr("Select a customer for a credit sale.")};
            if (!Session::instance().can("pos.credit_sale")) throw ServiceError{QObject::tr("You may not sell on credit.")};
            const Paisa limit = scalar("SELECT credit_limit FROM customers WHERE id=?", {c.customerId}).toLongLong();
            const Paisa owed = CustomerService::balance(c.customerId);
            if (limit > 0 && owed + t.credit > limit) throw ServiceError{QObject::tr("Customer credit limit exceeded.")};
        }
        if (t.due > 0) throw ServiceError{QObject::tr("Payment is short by %1.").arg(Money::format(t.due))};
        if (nonCash + t.credit > t.total) throw ServiceError{QObject::tr("Card, wallet and credit payments cannot be more than the bill.")};

        const qint64 shift = ShiftService::currentShift(Session::instance().userId());
        const QString invNo = nextInvoiceNo();
        const qint64 sale = insert("INSERT INTO sales(invoice_no, ts, customer_id, user_id, shift_id, prescription_id, subtotal, discount, "
                                   "tax, total, paid, change_given, status, offline, notes) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,'completed',?,?)",
                                   {invNo, Db::nowIso(), nullIfZero(c.customerId), nullIfZero(Session::instance().userId()), nullIfZero(shift),
                                    nullIfZero(c.prescriptionId), t.subtotal, t.lineDiscount + t.billDiscount, t.tax, t.total,
                                    t.paid + t.credit - t.change, t.change, offline ? 1 : 0, nullIf(c.notes)});
        Paisa costTotal = 0;
        for (int i = 0; i < c.lines.size(); ++i) {
            const auto &l = c.lines[i];
            const auto alloc = InventoryService::allocateFefo(l.medicineId, l.qty);
            Paisa lineCost = 0;
            for (const auto &a : alloc) lineCost += Money::times(a.unitCost, a.qty);
            const qint64 item = insert("INSERT INTO sale_items(sale_id, medicine_id, qty, unit_price, discount, tax, line_total, cost_total) "
                                       "VALUES(?,?,?,?,?,?,?,?)",
                                       {sale, l.medicineId, l.qty, l.unitPrice, l.discount,
                                        Money::percentOf(Money::times(l.unitPrice, l.qty) - l.discount, l.taxBp), t.lineTotals[i], lineCost});
            for (const auto &a : alloc) {
                insert("INSERT INTO sale_batches(sale_item_id, batch_id, qty, unit_cost) VALUES(?,?,?,?)", {item, a.batchId, a.qty, a.unitCost});
                InventoryService::move(a.batchId, "available", -a.qty, "sale", "sale", sale, invNo);
            }
            costTotal += lineCost;
        }
        exec("UPDATE sales SET cost_total=? WHERE id=?", {costTotal, sale});

        // Record payments. Cash is recorded net of change.
        for (const auto &p : c.payments) {
            Paisa amount = p.amount;
            if (p.method == "cash") amount -= t.change;
            if (amount <= 0) continue;
            insert("INSERT INTO sale_payments(sale_id, method, amount, reference) VALUES(?,?,?,?)", {sale, p.method, amount, nullIf(p.reference)});
        }
        if (t.credit > 0) customerLedger(c.customerId, "credit_sale", "sale", sale, t.credit, 0, invNo);
        cashMove(shift, "sale", cash - t.change, "sale", sale);
        if (c.prescriptionId) exec("UPDATE prescriptions SET status='dispensed' WHERE id=? AND status='approved'", {c.prescriptionId});
        audit("sale_posted", "sale", sale, invNo + " " + Money::format(t.total));
        return Result::success(sale);
    });
}

Result hold(const Cart &c, const QString &label)
{
    return transaction([&] {
        require("pos.sell");
        if (c.lines.isEmpty()) throw ServiceError{QObject::tr("The bill is empty.")};
        const qint64 id = insert("INSERT INTO held_bills(ts, user_id, label, payload) VALUES(?,?,?,?)",
                                 {Db::nowIso(), nullIfZero(Session::instance().userId()), nullIf(label),
                                  QString::fromUtf8(QJsonDocument(c.toJson()).toJson(QJsonDocument::Compact))});
        return Result::success(id);
    });
}

Result resume(qint64 heldId, Cart *out)
{
    return transaction([&] {
        require("pos.sell");
        const QString payload = scalar("SELECT payload FROM held_bills WHERE id=?", {heldId}).toString();
        if (payload.isEmpty()) throw ServiceError{QObject::tr("Held bill not found.")};
        *out = Cart::fromJson(QJsonDocument::fromJson(payload.toUtf8()).object());
        // Prices may have changed while the bill was on hold. Use current prices.
        for (auto &l : out->lines) {
            CartLine fresh;
            QString err;
            if (fillLine(l.medicineId, l.qty, &fresh, &err)) { fresh.discount = qMin(l.discount, Money::times(fresh.unitPrice, fresh.qty)); l = fresh; }
        }
        exec("DELETE FROM held_bills WHERE id=?", {heldId});
        return Result::success(heldId);
    });
}

Result discardHeld(qint64 heldId)
{
    return transaction([&] {
        require("pos.sell");
        exec("DELETE FROM held_bills WHERE id=?", {heldId});
        audit("held_bill_cancelled", "held_bill", heldId);
        return Result::success();
    });
}

int returnableQty(qint64 saleItemId)
{
    try {
        return scalar("SELECT si.qty - COALESCE((SELECT SUM(r.qty) FROM sales_return_items r WHERE r.sale_item_id=si.id),0) "
                      "FROM sale_items si WHERE si.id=?", {saleItemId}, 0).toInt();
    } catch (const ServiceError &) { return 0; }
}

Result returnItems(qint64 saleId, const QVector<ReturnRequestLine> &lines, const QString &refundMethod, const QString &reason)
{
    return transaction([&] {
        require("sales.return");
        if (lines.isEmpty()) throw ServiceError{QObject::tr("Select items to return.")};
        if (reason.trimmed().isEmpty()) throw ServiceError{QObject::tr("A reason is required.")};
        if (refundMethod != "cash" && refundMethod != "customer_credit" && !PaymentMethods::all().contains(refundMethod))
            throw ServiceError{"bad refund method"};
        QSqlQuery s = exec("SELECT total, customer_id, invoice_no, status FROM sales WHERE id=?", {saleId});
        if (!s.next()) throw ServiceError{"sale not found"};
        if (s.value(3).toString() != "completed") throw ServiceError{QObject::tr("This sale cannot be returned.")};
        const Paisa saleTotal = s.value(0).toLongLong();
        const qint64 customer = s.value(1).toLongLong();
        if (refundMethod == "customer_credit" && !customer) throw ServiceError{QObject::tr("This sale has no customer account.")};

        // Net value of each item after its share of the bill discount.
        QSqlQuery all = exec("SELECT id, line_total FROM sale_items WHERE sale_id=? ORDER BY id", {saleId});
        QList<qint64> ids; QList<Paisa> weights;
        while (all.next()) { ids << all.value(0).toLongLong(); weights << all.value(1).toLongLong(); }
        const QList<Paisa> netShare = Money::allocate(saleTotal, weights);

        const qint64 ret = insert("INSERT INTO sales_returns(sale_id, ts, user_id, total, refund_method, reason, shift_id) VALUES(?,?,?,0,?,?,?)",
                                  {saleId, Db::nowIso(), nullIfZero(Session::instance().userId()), refundMethod, reason.trimmed(),
                                   nullIfZero(ShiftService::currentShift(Session::instance().userId()))});
        Paisa refund = 0, cost = 0;
        for (const auto &rl : lines) {
            const int idx = ids.indexOf(rl.saleItemId);
            if (idx < 0) throw ServiceError{"item not in sale"};
            if (rl.qty <= 0 || rl.qty > returnableQty(rl.saleItemId))
                throw ServiceError{QObject::tr("Return quantity is more than what was sold.")};
            const int soldQty = scalar("SELECT qty FROM sale_items WHERE id=?", {rl.saleItemId}).toInt();
            const int before = soldQty - returnableQty(rl.saleItemId);
            // Cumulative rounding: all partial returns together never refund more than the item's net value.
            const auto shareOf = [&](int units) { return Paisa((long double)netShare[idx] * units / soldQty + 0.5L); };
            const Paisa amount = shareOf(before + rl.qty) - shareOf(before);
            // Put units back into the batches they came from, newest allocation first, into quarantine.
            int remaining = rl.qty;
            QSqlQuery b = exec("SELECT sb.batch_id, sb.qty - COALESCE((SELECT SUM(r.qty) FROM sales_return_items r "
                               "WHERE r.sale_item_id=sb.sale_item_id AND r.batch_id=sb.batch_id),0), sb.unit_cost "
                               "FROM sale_batches sb WHERE sb.sale_item_id=? ORDER BY sb.id DESC", {rl.saleItemId});
            QList<std::tuple<qint64, int, Paisa>> parts;
            while (remaining > 0 && b.next()) {
                const int take = qMin(remaining, b.value(1).toInt());
                if (take > 0) { parts.append({b.value(0).toLongLong(), take, b.value(2).toLongLong()}); remaining -= take; }
            }
            if (remaining > 0) throw ServiceError{"batch trace mismatch"};
            Paisa allocated = 0;
            for (int i = 0; i < parts.size(); ++i) {
                const auto &[batch, q, unitCost] = parts[i];
                const Paisa part = (i == parts.size() - 1) ? amount - allocated : Paisa((long double)amount * q / rl.qty);
                allocated += part;
                insert("INSERT INTO sales_return_items(return_id, sale_item_id, batch_id, qty, amount, unit_cost) VALUES(?,?,?,?,?,?)",
                       {ret, rl.saleItemId, batch, q, part, unitCost});
                InventoryService::move(batch, "quarantine", q, "sale_return", "sales_return", ret, reason);
                cost += Money::times(unitCost, q);
            }
            refund += amount;
        }
        exec("UPDATE sales_returns SET total=?, cost_total=? WHERE id=?", {refund, cost, ret});
        if (refundMethod == "customer_credit") customerLedger(customer, "return", "sales_return", ret, 0, refund, s.value(2).toString());
        else if (refundMethod == "cash") cashMove(ShiftService::currentShift(Session::instance().userId()), "refund", -refund, "sales_return", ret);
        audit("sale_returned", "sales_return", ret, s.value(2).toString() + " " + Money::format(refund));
        return Result::success(ret);
    });
}

Result logPrint(qint64 saleId, const QString &format, int *copyNumber)
{
    return transaction([&] {
        const int prev = scalar("SELECT COUNT(*) FROM receipt_prints WHERE sale_id=?", {saleId}).toInt();
        if (prev > 0) require("sales.reprint");
        insert("INSERT INTO receipt_prints(sale_id, ts, user_id, copy, format) VALUES(?,?,?,?,?)",
               {saleId, Db::nowIso(), nullIfZero(Session::instance().userId()), prev, format});
        if (prev > 0) audit("receipt_reprinted", "sale", saleId, format + " copy " + QString::number(prev));
        *copyNumber = prev;
        return Result::success();
    });
}
}

namespace CustomerService {
Result save(qint64 id, const QString &name, const QString &phone, const QString &address, const QString &notes, Paisa limit, bool active)
{
    return transaction([&] {
        require("customers.edit");
        if (name.trimmed().isEmpty()) throw ServiceError{QObject::tr("Customer name is required.")};
        if (limit < 0) throw ServiceError{QObject::tr("Credit limit cannot be negative.")};
        if (id) exec("UPDATE customers SET name=?, phone=?, address=?, notes=?, credit_limit=?, active=? WHERE id=?",
                     {name.trimmed(), nullIf(phone), nullIf(address), nullIf(notes), limit, active ? 1 : 0, id});
        else id = insert("INSERT INTO customers(name, phone, address, notes, credit_limit, active, created_at) VALUES(?,?,?,?,?,?,?)",
                         {name.trimmed(), nullIf(phone), nullIf(address), nullIf(notes), limit, active ? 1 : 0, Db::nowIso()});
        audit("customer_saved", "customer", id);
        return Result::success(id);
    });
}

Result receivePayment(qint64 customerId, Paisa amount, const QString &method, const QString &reference)
{
    return transaction([&] {
        require("customers.receive");
        if (amount <= 0) throw ServiceError{QObject::tr("Amount must be more than zero.")};
        if (method == "customer_credit") throw ServiceError{"bad method"};
        const qint64 id = insert("INSERT INTO customer_ledger(customer_id, ts, entry_type, ref_type, debit, credit, note, user_id) "
                                 "VALUES(?,?,'payment','customer_payment',0,?,?,?)",
                                 {customerId, Db::nowIso(), amount, method + " " + reference, nullIfZero(Session::instance().userId())});
        if (method == "cash") {
            const qint64 shift = ShiftService::currentShift(Session::instance().userId());
            if (shift) insert("INSERT INTO cash_movements(shift_id, ts, kind, amount, ref_type, ref_id) VALUES(?,?,'customer_payment',?,'customer_ledger',?)",
                              {shift, Db::nowIso(), amount, id});
        }
        audit("customer_payment", "customer", customerId, Money::format(amount));
        return Result::success(id);
    });
}

Result openingBalance(qint64 customerId, Paisa amount)
{
    return transaction([&] {
        require("customers.edit");
        if (scalar("SELECT COUNT(*) FROM customer_ledger WHERE customer_id=? AND entry_type='opening'", {customerId}).toInt())
            throw ServiceError{QObject::tr("Opening balance is already recorded.")};
        insert("INSERT INTO customer_ledger(customer_id, ts, entry_type, debit, credit, note, user_id) VALUES(?,?,'opening',?,?,?,?)",
               {customerId, Db::nowIso(), amount > 0 ? amount : 0, amount < 0 ? -amount : 0, QObject::tr("Opening balance"),
                nullIfZero(Session::instance().userId())});
        return Result::success();
    });
}

Paisa balance(qint64 customerId)
{
    try { return scalar("SELECT COALESCE(SUM(debit-credit),0) FROM customer_ledger WHERE customer_id=?", {customerId}, 0).toLongLong(); }
    catch (const ServiceError &) { return 0; }
}
}

namespace ShiftService {
qint64 currentShift(qint64 userId)
{
    if (!userId) return 0;
    try { return scalar("SELECT id FROM cash_shifts WHERE user_id=? AND status='open' ORDER BY id DESC LIMIT 1", {userId}, 0).toLongLong(); }
    catch (const ServiceError &) { return 0; }
}

Result open(Paisa openingCash)
{
    return transaction([&] {
        require("shifts.own");
        if (openingCash < 0) throw ServiceError{QObject::tr("Opening cash cannot be negative.")};
        if (currentShift(Session::instance().userId())) throw ServiceError{QObject::tr("You already have an open shift.")};
        const qint64 id = insert("INSERT INTO cash_shifts(user_id, opened_at, opening_cash, status) VALUES(?,?,?,'open')",
                                 {Session::instance().userId(), Db::nowIso(), openingCash});
        audit("shift_opened", "shift", id, Money::format(openingCash));
        return Result::success(id);
    });
}

Paisa expectedCash(qint64 shiftId)
{
    try {
        return scalar("SELECT s.opening_cash + COALESCE((SELECT SUM(amount) FROM cash_movements WHERE shift_id=s.id),0) "
                      "FROM cash_shifts s WHERE s.id=?", {shiftId}, 0).toLongLong();
    } catch (const ServiceError &) { return 0; }
}

Result close(Paisa actual, const QString &notes)
{
    return transaction([&] {
        require("shifts.own");
        const qint64 id = currentShift(Session::instance().userId());
        if (!id) throw ServiceError{QObject::tr("You have no open shift.")};
        if (actual < 0) throw ServiceError{QObject::tr("Cash counted cannot be negative.")};
        const Paisa expected = expectedCash(id);
        exec("UPDATE cash_shifts SET closed_at=?, expected_cash=?, actual_cash=?, variance=?, status='closed', notes=? WHERE id=?",
             {Db::nowIso(), expected, actual, actual - expected, nullIf(notes), id});
        audit("shift_closed", "shift", id, "expected " + Money::format(expected) + " actual " + Money::format(actual));
        return Result::success(id);
    });
}

Result cashMovement(const QString &kind, Paisa amount, const QString &note)
{
    return transaction([&] {
        require("shifts.own");
        const qint64 id = currentShift(Session::instance().userId());
        if (!id) throw ServiceError{QObject::tr("Open a shift first.")};
        if (amount <= 0 || note.trimmed().isEmpty()) throw ServiceError{QObject::tr("Enter an amount and a note.")};
        if (kind != "deposit" && kind != "payout") throw ServiceError{"bad kind"};
        insert("INSERT INTO cash_movements(shift_id, ts, kind, amount, note) VALUES(?,?,?,?,?)",
               {id, Db::nowIso(), kind, kind == "payout" ? -amount : amount, note.trimmed()});
        audit("cash_" + kind, "shift", id, Money::format(amount) + " " + note);
        return Result::success();
    });
}
}

namespace ExpenseService {
qint64 categoryId(const QString &name)
{
    if (name.trimmed().isEmpty()) return 0;
    try {
        exec("INSERT OR IGNORE INTO expense_categories(name) VALUES(?)", {name.trimmed()});
        return scalar("SELECT id FROM expense_categories WHERE name=?", {name.trimmed()}).toLongLong();
    } catch (const ServiceError &) {
        return 0;
    }
}

Result add(qint64 categoryId, const QString &date, const QString &description, Paisa amount, const QString &method,
           const QString &reference, const QString &notes)
{
    return transaction([&] {
        require("expenses.edit");
        if (!categoryId) throw ServiceError{QObject::tr("Select a category.")};
        if (description.trimmed().isEmpty()) throw ServiceError{QObject::tr("Description is required.")};
        if (amount <= 0) throw ServiceError{QObject::tr("Amount must be more than zero.")};
        if (!QDate::fromString(date, Qt::ISODate).isValid()) throw ServiceError{QObject::tr("Date is required.")};
        const qint64 shift = method == "cash" ? ShiftService::currentShift(Session::instance().userId()) : 0;
        const qint64 id = insert("INSERT INTO expenses(category_id, expense_date, description, amount, method, reference, notes, user_id, "
                                 "shift_id, created_at) VALUES(?,?,?,?,?,?,?,?,?,?)",
                                 {categoryId, date, description.trimmed(), amount, method, nullIf(reference), nullIf(notes),
                                  nullIfZero(Session::instance().userId()), nullIfZero(shift), Db::nowIso()});
        if (shift) insert("INSERT INTO cash_movements(shift_id, ts, kind, amount, ref_type, ref_id) VALUES(?,?,'expense',?,'expense',?)",
                          {shift, Db::nowIso(), -amount, id});
        audit("expense_added", "expense", id, Money::format(amount));
        return Result::success(id);
    });
}

Result reverse(qint64 expenseId, const QString &reason)
{
    return transaction([&] {
        require("expenses.edit");
        if (reason.trimmed().isEmpty()) throw ServiceError{QObject::tr("A reason is required.")};
        QSqlQuery q = exec("SELECT category_id, amount, method, status, description FROM expenses WHERE id=?", {expenseId});
        if (!q.next()) throw ServiceError{"not found"};
        if (q.value(3).toString() != "posted") throw ServiceError{QObject::tr("This expense is already reversed.")};
        exec("UPDATE expenses SET status='reversed' WHERE id=?", {expenseId});
        const qint64 id = insert("INSERT INTO expenses(category_id, expense_date, description, amount, method, notes, user_id, status, "
                                 "reversal_of, created_at) VALUES(?,?,?,?,?,?,?,'reversal',?,?)",
                                 {q.value(0), todayIso(), "Reversal: " + q.value(4).toString(), q.value(1), q.value(2), reason,
                                  nullIfZero(Session::instance().userId()), expenseId, Db::nowIso()});
        audit("expense_reversed", "expense", expenseId, reason);
        return Result::success(id);
    });
}
}

namespace PrescriptionService {
Result create(const QString &refNo, qint64 customerId, const QString &patient, const QString &doctor, const QString &issued,
              const QString &notes, const QVector<Item> &items)
{
    return transaction([&] {
        require("prescriptions.edit");
        if (refNo.trimmed().isEmpty()) throw ServiceError{QObject::tr("Prescription reference is required.")};
        if (items.isEmpty()) throw ServiceError{QObject::tr("Add at least one medicine.")};
        const qint64 id = insert("INSERT INTO prescriptions(ref_no, customer_id, patient_name, doctor_name, issued_date, notes, created_by, created_at) "
                                 "VALUES(?,?,?,?,?,?,?,?)",
                                 {refNo.trimmed(), nullIfZero(customerId), nullIf(patient), nullIf(doctor), nullIf(issued), nullIf(notes),
                                  nullIfZero(Session::instance().userId()), Db::nowIso()});
        for (const auto &i : items)
            insert("INSERT INTO prescription_items(prescription_id, medicine_id, medicine_text, qty, directions) VALUES(?,?,?,?,?)",
                   {id, nullIfZero(i.medicineId), nullIf(i.medicineText), i.qty, nullIf(i.directions)});
        audit("prescription_recorded", "prescription", id);
        return Result::success(id);
    });
}

Result review(qint64 id, bool approve, const QString &note)
{
    return transaction([&] {
        require("prescriptions.approve");
        if (scalar("SELECT status FROM prescriptions WHERE id=?", {id}).toString() != "pending")
            throw ServiceError{QObject::tr("This prescription was already reviewed.")};
        exec("UPDATE prescriptions SET status=?, reviewed_by=?, reviewed_at=?, review_note=? WHERE id=?",
             {approve ? "approved" : "rejected", Session::instance().userId(), Db::nowIso(), nullIf(note), id});
        audit(approve ? "prescription_approved" : "prescription_rejected", "prescription", id, note);
        return Result::success(id);
    });
}
}
}
