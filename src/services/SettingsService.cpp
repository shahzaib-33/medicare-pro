#include "SettingsService.h"
#include "AuthService.h"

namespace mcp::SettingsService {

QString get(const QString &key, const QString &def)
{
    try {
        QSqlQuery q = exec("SELECT value FROM app_settings WHERE key=?", {key});
        return q.next() ? q.value(0).toString() : def;
    } catch (const ServiceError &) {
        return def;
    }
}

static void put(const QString &key, const QString &value)
{
    exec("INSERT INTO app_settings(key, value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value", {key, value});
}

Result set(const QString &key, const QString &value)
{
    return transaction([&] {
        require("settings.edit");
        put(key, value);
        audit("setting_changed", "setting", 0, key);
        return Result::success();
    });
}

PharmacyProfile profile()
{
    PharmacyProfile p;
    p.name = get("pharmacy.name", "My Pharmacy");
    p.nameUr = get("pharmacy.name_ur");
    p.address = get("pharmacy.address");
    p.phone = get("pharmacy.phone");
    p.licenseNo = get("pharmacy.license_no");
    p.ntn = get("pharmacy.ntn");
    p.receiptFooter = get("receipt.footer", "Thank you. Medicines once sold are returned only as per pharmacy policy.");
    p.receiptFooterUr = get("receipt.footer_ur", QString::fromUtf8("خریداری کا شکریہ۔ دوا کی واپسی فارمیسی کی پالیسی کے مطابق ہوگی۔"));
    p.receiptWidth = get("receipt.width", "80");
    p.receiptLanguage = get("receipt.language", "en");
    p.autoLockMinutes = get("security.autolock_minutes", "10").toInt();
    p.expiryWarnDays = get("inventory.expiry_warn_days", "90").toInt();
    p.printAfterSale = get("receipt.print_after_sale", "1") == "1";
    return p;
}

Result saveProfile(const PharmacyProfile &p)
{
    if (p.name.trimmed().isEmpty()) return Result::fail(QObject::tr("Pharmacy name is required."));
    if (!QStringList{"58", "80", "A4"}.contains(p.receiptWidth)) return Result::fail("bad receipt width");
    if (p.autoLockMinutes < 0 || p.autoLockMinutes > 240) return Result::fail(QObject::tr("Auto lock must be between 0 and 240 minutes."));
    return transaction([&] {
        require("settings.edit");
        put("pharmacy.name", p.name.trimmed());
        put("pharmacy.name_ur", p.nameUr.trimmed());
        put("pharmacy.address", p.address.trimmed());
        put("pharmacy.phone", p.phone.trimmed());
        put("pharmacy.license_no", p.licenseNo.trimmed());
        put("pharmacy.ntn", p.ntn.trimmed());
        put("receipt.footer", p.receiptFooter.trimmed());
        put("receipt.footer_ur", p.receiptFooterUr.trimmed());
        put("receipt.width", p.receiptWidth);
        put("receipt.language", p.receiptLanguage);
        put("security.autolock_minutes", QString::number(p.autoLockMinutes));
        put("inventory.expiry_warn_days", QString::number(qMax(7, p.expiryWarnDays)));
        put("receipt.print_after_sale", p.printAfterSale ? "1" : "0");
        audit("pharmacy_profile_saved", "setting");
        return Result::success();
    });
}
}
