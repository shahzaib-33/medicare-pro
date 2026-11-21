#pragma once
#include <QVector>
#include "Common.h"

namespace mcp {

struct PurchaseLine {
    qint64 medicineId = 0;
    QString batchNo, mfgDate, expiryDate;
    int qty = 0;            // base units paid for
    int bonusQty = 0;       // free units from supplier
    Paisa unitCost = 0;     // per base unit
    Paisa discount = 0;     // line discount amount
    int taxBp = 0;          // tax on (qty * cost - discount)
    QVariant salePrice;     // optional batch sale price per base unit
};

struct PurchaseDraft {
    qint64 supplierId = 0;
    QString invoiceNo, invoiceDate;
    QVector<PurchaseLine> lines;
    Paisa invoiceDiscount = 0;
    Paisa paidNow = 0;
    QString paymentMethod = "cash", paymentReference, notes;
};

struct PurchaseTotals { Paisa subtotal = 0, discount = 0, tax = 0, total = 0; QVector<Paisa> lineTotals; };

struct ReturnLine { qint64 batchId = 0; QString bucket = "available"; int qty = 0; };

namespace PurchaseService {
PurchaseTotals compute(const PurchaseDraft &d);
Result post(const PurchaseDraft &d);
Result pay(qint64 supplierId, qint64 invoiceId, Paisa amount, const QString &method, const QString &reference);
Result returnToSupplier(qint64 supplierId, qint64 invoiceId, const QVector<ReturnLine> &lines, const QString &reason);
// Reversal instead of editing a posted invoice. Only possible while its stock is still on hand.
Result reverse(qint64 invoiceId, const QString &reason);
}

namespace SupplierService {
Result save(qint64 id, const QString &name, const QString &phone, const QString &address, const QString &notes, bool active);
Result openingBalance(qint64 supplierId, Paisa amount);   // amount owed at start
Paisa balance(qint64 supplierId);   // what the pharmacy owes
}
}
