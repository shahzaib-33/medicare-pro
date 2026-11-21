#pragma once
#include <QDate>
#include <QVector>
#include "Common.h"

namespace mcp {

struct BatchAllocation { qint64 batchId; int qty; Paisa unitCost; QString batchNo; QString expiry; };

struct MedicineInput {
    QString brandName, brandNameUr, genericName, genericNameUr, ingredients, composition, strength, dosageForm,
        manufacturer, category, packSize, baseUnit = "unit", barcode, registrationNo, rackLocation;
    int unitsPerPack = 1;
    Paisa purchasePrice = 0, salePrice = 0;   // per base unit
    int taxRateBp = 0;
    qint64 supplierId = 0;
    int minStock = 0;
    bool rxRequired = false;
};

// All stock changes go through this service. Each change writes a stock_movements row
// with a reason and reference. Call the move functions inside a transaction.
namespace InventoryService {
// Medicine master
Result addMedicine(const MedicineInput &m);
Result updateMedicine(qint64 id, const MedicineInput &m);
Result archiveMedicine(qint64 id, bool archived);
Result deleteMedicine(qint64 id);          // only when nothing references it
Result setUnits(qint64 medicineId, const QList<QPair<QString, int>> &units);   // e.g. strip 10, box 100
Result addBarcode(qint64 medicineId, const QString &barcode, const QString &unitName);
qint64 findByBarcode(const QString &barcode, int *unitFactor = nullptr);

// Low level, used inside service transactions. Throws ServiceError.
qint64 findOrCreateBatch(qint64 medicineId, const QString &batchNo, const QString &mfg, const QString &expiry,
                         qint64 supplierId, Paisa unitCost, const QVariant &salePrice);
void move(qint64 batchId, const QString &bucket, int qty, const QString &reason, const QString &refType, qint64 refId,
          const QString &note = {});
QVector<BatchAllocation> allocateFefo(qint64 medicineId, int qty, const QString &today = todayIso());

// Higher level operations with their own transaction and permission checks.
Result openingStock(qint64 medicineId, const QString &batchNo, const QString &expiry, int qty, Paisa unitCost);
Result adjust(qint64 batchId, const QString &kind, int qty, const QString &reason);
// kind: damage (available -> damaged), correction_in, correction_out, dispose_damaged, dispose_expired
Result setBatchStatus(qint64 batchId, const QString &status, const QString &note);  // active, recalled
Result inspectReturn(qint64 returnItemId, bool restock, const QString &note);

qint64 startCount(const QString &note);
Result recordCount(qint64 countId, qint64 batchId, int counted);
Result finishCount(qint64 countId);

int saleableStock(qint64 medicineId, const QString &today = todayIso());
int totalStock(qint64 medicineId);
}
}
