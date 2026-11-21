#pragma once
#include "Common.h"

namespace mcp {

// Pharmacy profile and business settings stored inside pharmacy.db, so they travel with backups.
struct PharmacyProfile {
    QString name, nameUr, address, phone, licenseNo, ntn, receiptFooter, receiptFooterUr;
    QString receiptWidth = "80";      // 58, 80, A4
    QString receiptLanguage = "en";   // en, ur
    int autoLockMinutes = 10;          // 0 = off
    int expiryWarnDays = 90;
    bool printAfterSale = true;
};

namespace SettingsService {
QString get(const QString &key, const QString &def = {});
Result set(const QString &key, const QString &value);   // needs settings.edit
PharmacyProfile profile();
Result saveProfile(const PharmacyProfile &p);
}
}
