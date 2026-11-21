#pragma once
#include <QString>
#include <QStringList>
#include <QVariant>

namespace mcp {
// One reference medicine as imported or delivered by an update package.
// Empty fields mean "not available". Nothing is ever guessed or filled in.
struct MedicineRecord {
    QString refKey;              // stable key: registration number, or source key
    QString brandName;
    QString brandNameUr;         // only when verified or manually provided
    QString genericName;
    QString genericNameUr;
    QStringList ingredients;
    QString composition;
    QString strength;
    QString dosageForm;
    QString manufacturer;
    QString mahName;             // marketing authorization holder
    QString registrationNo;
    QString registrationDate;
    QString registrationStatus = "unknown"; // active, suspended, cancelled, discontinued, unknown
    QString category;
    QString packSize;
    QString barcode;
    QString rxClass;
    QVariant referencePrice;     // null unless an official price was supplied
    QString priceSource;
    QString sourceCode;          // medicine_data_sources.code
    QString verificationStatus = "unverified"; // verified, unverified, sample
    QString verifiedAt;

    QString contentHash() const;
};

struct CatalogHit {
    qint64 id = 0;
    QString refKey;
    QString brandName;
    QString brandNameUr;
    QString genericName;
    QString strength;
    QString dosageForm;
    QString manufacturer;
    QString registrationNo;
    QString registrationStatus;
    QString verificationStatus;
    QString matchedBy;   // brand, generic, urdu, ingredient, manufacturer, registration, barcode, phonetic
    int score = 0;
};
}
