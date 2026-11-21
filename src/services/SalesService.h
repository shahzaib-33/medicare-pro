#pragma once
#include <QJsonObject>
#include <QVector>
#include "Common.h"

namespace mcp {

struct CartLine {
    qint64 medicineId = 0;
    QString name;           // display only
    int qty = 0;            // base units
    Paisa unitPrice = 0;    // per base unit
    Paisa discount = 0;     // line discount amount
    int taxBp = 0;
    bool rxRequired = false;
};

struct PaymentLine { QString method; Paisa amount = 0; QString reference; };

struct Cart {
    QVector<CartLine> lines;
    Paisa billDiscount = 0;
    qint64 customerId = 0;
    qint64 prescriptionId = 0;
    QVector<PaymentLine> payments;
    QString notes;
    QJsonObject toJson() const;
    static Cart fromJson(const QJsonObject &o);
};

struct CartTotals {
    Paisa subtotal = 0, lineDiscount = 0, billDiscount = 0, tax = 0, total = 0;
    Paisa paid = 0, credit = 0, change = 0, due = 0;
    QVector<Paisa> lineTotals;
};

struct ReturnRequestLine { qint64 saleItemId = 0; int qty = 0; };

namespace PaymentMethods {
// cash, bank_transfer, card, jazzcash, easypaisa, raast, customer_credit
const QStringList &all();
QString label(const QString &code);
}

namespace SalesService {
CartTotals compute(const Cart &c);
// Loads price, tax and prescription flag for a medicine into a cart line.
bool fillLine(qint64 medicineId, int qty, CartLine *out, QString *err);

// Posts the sale in one transaction: invoice, FEFO batches, stock, payments, customer ledger, cash drawer.
Result post(const Cart &c, bool offline = false);

Result hold(const Cart &c, const QString &label);
Result resume(qint64 heldId, Cart *out);
Result discardHeld(qint64 heldId);

int returnableQty(qint64 saleItemId);
// Returned stock goes to quarantine until inspected. Refund is cash or customer credit.
Result returnItems(qint64 saleId, const QVector<ReturnRequestLine> &lines, const QString &refundMethod, const QString &reason);

Result logPrint(qint64 saleId, const QString &format, int *copyNumber);
}

namespace CustomerService {
Result save(qint64 id, const QString &name, const QString &phone, const QString &address, const QString &notes,
            Paisa creditLimit, bool active);
Result receivePayment(qint64 customerId, Paisa amount, const QString &method, const QString &reference);
Result openingBalance(qint64 customerId, Paisa amount);
Paisa balance(qint64 customerId);   // what the customer owes
}

namespace ShiftService {
qint64 currentShift(qint64 userId);
Result open(Paisa openingCash);
Paisa expectedCash(qint64 shiftId);
Result close(Paisa actualCash, const QString &notes);
Result cashMovement(const QString &kind, Paisa amount, const QString &note);   // deposit or payout
}

namespace ExpenseService {
Result add(qint64 categoryId, const QString &date, const QString &description, Paisa amount, const QString &method,
           const QString &reference, const QString &notes);
Result reverse(qint64 expenseId, const QString &reason);
qint64 categoryId(const QString &name);   // creates when missing
}

namespace PrescriptionService {
struct Item { qint64 medicineId = 0; QString medicineText; int qty = 0; QString directions; };
Result create(const QString &refNo, qint64 customerId, const QString &patient, const QString &doctor,
              const QString &issued, const QString &notes, const QVector<Item> &items);
Result review(qint64 id, bool approve, const QString &note);
}
}
