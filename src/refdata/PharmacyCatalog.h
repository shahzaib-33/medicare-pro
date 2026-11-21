#pragma once
#include <QDate>
#include <QSqlDatabase>
#include <QVariant>
#include <QVector>
#include "MedicineRecord.h"
#include "core/Money.h"

namespace mcp {

struct OpeningBatch {
    QString batchNo;
    QDate expiry;
    int quantity = 0;
};

// Values the pharmacist enters. These belong to the pharmacy only. Prices are paisa per base unit.
struct LocalMedicineSettings {
    QVariant purchasePrice;   // null = not set
    QVariant salePrice;
    QString packSize;
    int unitsPerPack = 1;
    QString barcode;
    qint64 supplierId = 0;
    QString rackLocation;
    int minStock = 0;
    bool rxRequired = false;
    int taxRateBp = 0;
    QVector<OpeningBatch> openingBatches;  // empty = no stock is created
};

struct AddResult { bool ok = false; qint64 medicineId = 0; QString error; bool alreadyExists = false; };

// The pharmacy's own medicine master in pharmacy.db.
namespace PharmacyCatalog {
qint64 findByReference(QSqlDatabase pharmacyDb, const QString &refKey);

// Copies reference information into the pharmacy master.
// Creates stock only for batches the pharmacist entered explicitly, through the stock movement log.
AddResult addFromReference(QSqlDatabase pharmacyDb, const MedicineRecord &ref, int referenceVersion,
                           const LocalMedicineSettings &local);

// After a reference update: compares linked local products with the new reference data.
// Never changes local prices, stock, barcodes or suppliers. Only records review flags.
// Official pack price from the update becomes the per unit sale price of each linked medicine.
// Only records changed in this version are used, so a price the owner set by hand stays until the next official change.
int applyReferencePrices(QSqlDatabase db, QSqlDatabase refDb, int newVersion);
int flagChangedProducts(QSqlDatabase pharmacyDb, QSqlDatabase refDb, int newVersion);

int openReviewFlags(QSqlDatabase pharmacyDb);

int stockOf(QSqlDatabase pharmacyDb, qint64 medicineId);   // available units in all batches
}
}
