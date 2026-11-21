#include "ReceiptBuilder.h"
#include "SalesService.h"
#include "SettingsService.h"
#include <QDateTime>

namespace mcp::ReceiptBuilder {

namespace {
// Receipt language is chosen per pharmacy, independent of the screen language.
QString L(const QString &lang, const char *en, const char *ur) { return lang == "ur" ? QString::fromUtf8(ur) : QString::fromUtf8(en); }

QString esc(const QString &s) { return s.toHtmlEscaped(); }

QString methodLabel(const QString &code, const QString &lang)
{
    if (lang != "ur") return PaymentMethods::label(code);
    static const QHash<QString, const char *> ur = {
        {"cash", "نقد"}, {"bank_transfer", "بینک ٹرانسفر"}, {"card", "کارڈ"}, {"jazzcash", "جاز کیش"},
        {"easypaisa", "ایزی پیسہ"}, {"raast", "راست"}, {"customer_credit", "ادھار"}};
    return ur.contains(code) ? QString::fromUtf8(ur.value(code)) : code;
}

QString localTime(const QString &isoUtc)
{
    QDateTime t = QDateTime::fromString(isoUtc, Qt::ISODate);
    t.setTimeSpec(Qt::UTC);
    return t.toLocalTime().toString("dd-MM-yyyy hh:mm AP");
}

struct Frame { QString open, close; int font; };

Frame frame(const QString &width, const QString &lang)
{
    const bool rtl = lang == "ur";
    const int font = width == "58" ? 7 : width == "80" ? 8 : 10;
    const QString dir = rtl ? "rtl" : "ltr";
    return {QString("<html><body dir='%1' style='font-family:\"Segoe UI\",\"Noto Nastaliq Urdu\",Arial; font-size:%2pt;'>")
                .arg(dir).arg(font),
            "</body></html>", font};
}

QString header(const PharmacyProfile &p, const QString &lang)
{
    QString h = "<div align='center'>";
    const QString name = (lang == "ur" && !p.nameUr.isEmpty()) ? p.nameUr : p.name;
    h += "<div style='font-size:130%; font-weight:bold;'>" + esc(name) + "</div>";
    if (!p.address.isEmpty()) h += "<div>" + esc(p.address) + "</div>";
    if (!p.phone.isEmpty()) h += "<div>" + L(lang, "Phone", "فون") + ": " + esc(p.phone) + "</div>";
    if (!p.licenseNo.isEmpty()) h += "<div>" + L(lang, "Drug sale license", "ڈرگ سیل لائسنس") + ": " + esc(p.licenseNo) + "</div>";
    if (!p.ntn.isEmpty()) h += "<div>" + L(lang, "NTN", "این ٹی این") + ": " + esc(p.ntn) + "</div>";
    h += "</div><hr/>";
    return h;
}

QString row2(const QString &a, const QString &b, bool bold = false)
{
    const QString w = bold ? " style='font-weight:bold;'" : QString();
    return "<tr><td" + w + ">" + a + "</td><td align='right'" + w + ">" + b + "</td></tr>";
}
}

QString saleHtml(qint64 saleId, const QString &width, const QString &lang, int copyNumber)
{
    try {
        const PharmacyProfile p = SettingsService::profile();
        QSqlQuery s = exec("SELECT s.invoice_no, s.ts, s.subtotal, s.discount, s.tax, s.total, s.paid, s.change_given, "
                           "COALESCE(u.full_name,''), COALESCE(c.name,''), s.offline FROM sales s "
                           "LEFT JOIN users u ON u.id=s.user_id LEFT JOIN customers c ON c.id=s.customer_id WHERE s.id=?", {saleId});
        if (!s.next()) return {};
        const Frame f = frame(width, lang);
        QString h = f.open + header(p, lang);
        if (copyNumber > 0)
            h += "<div align='center' style='font-weight:bold; font-size:120%;'>*** " + L(lang, "DUPLICATE COPY", "نقل کاپی") + " "
                 + QString::number(copyNumber) + " ***</div>";
        h += "<table width='100%' cellspacing='0' cellpadding='1'>";
        h += row2(L(lang, "Invoice", "رسید نمبر"), esc(s.value(0).toString()));
        h += row2(L(lang, "Date", "تاریخ"), localTime(s.value(1).toString()));
        h += row2(L(lang, "Cashier", "کیشیئر"), esc(s.value(8).toString()));
        if (!s.value(9).toString().isEmpty()) h += row2(L(lang, "Customer", "گاہک"), esc(s.value(9).toString()));
        h += "</table><hr/>";

        const bool narrow = width == "58";
        h += "<table width='100%' cellspacing='0' cellpadding='1'><tr style='font-weight:bold;'><td>" + L(lang, "Item", "دوا") + "</td>"
             "<td align='right'>" + L(lang, "Qty", "تعداد") + "</td>";
        if (!narrow) h += "<td align='right'>" + L(lang, "Rate", "ریٹ") + "</td>";
        h += "<td align='right'>" + L(lang, "Amount", "رقم") + "</td></tr>";
        QSqlQuery it = exec("SELECT CASE WHEN ?='ur' AND COALESCE(m.brand_name_ur,'')<>'' THEN m.brand_name_ur ELSE m.brand_name END, "
                            "COALESCE(m.strength,''), i.qty, i.unit_price, i.discount, i.line_total, "
                            "(SELECT GROUP_CONCAT(b.batch_no || ' ' || substr(b.expiry_date,1,7), ', ') FROM sale_batches sb "
                            " JOIN medicine_batches b ON b.id=sb.batch_id WHERE sb.sale_item_id=i.id) "
                            "FROM sale_items i JOIN medicines m ON m.id=i.medicine_id WHERE i.sale_id=? ORDER BY i.id", {lang, saleId});
        while (it.next()) {
            QString name = esc(it.value(0).toString() + " " + it.value(1).toString()).trimmed();
            if (width == "A4") name += "<br/><span style='font-size:85%;'>" + L(lang, "Batch", "بیچ") + ": " + esc(it.value(6).toString()) + "</span>";
            h += "<tr><td>" + name + "</td><td align='right'>" + QString::number(it.value(2).toInt()) + "</td>";
            if (!narrow) h += "<td align='right'>" + Money::format(it.value(3).toLongLong(), false) + "</td>";
            h += "<td align='right'>" + Money::format(it.value(5).toLongLong(), false) + "</td></tr>";
            if (it.value(4).toLongLong() > 0)
                h += "<tr><td colspan='" + QString::number(narrow ? 3 : 4) + "' style='font-size:85%;'>" + L(lang, "Discount", "رعایت") + " -"
                     + Money::format(it.value(4).toLongLong(), false) + "</td></tr>";
        }
        h += "</table><hr/><table width='100%' cellspacing='0' cellpadding='1'>";
        h += row2(L(lang, "Subtotal", "کل قیمت"), Money::format(s.value(2).toLongLong()));
        if (s.value(3).toLongLong() > 0) h += row2(L(lang, "Discount", "رعایت"), "-" + Money::format(s.value(3).toLongLong()));
        if (s.value(4).toLongLong() > 0) h += row2(L(lang, "Sales tax", "سیلز ٹیکس"), Money::format(s.value(4).toLongLong()));
        h += row2(L(lang, "TOTAL", "کل رقم"), Money::format(s.value(5).toLongLong()), true);
        QSqlQuery pay = exec("SELECT method, amount FROM sale_payments WHERE sale_id=? ORDER BY id", {saleId});
        while (pay.next()) {
            const QString m = pay.value(0).toString();
            Paisa amount = pay.value(1).toLongLong();
            if (m == "cash") amount += s.value(7).toLongLong();   // show cash tendered
            h += row2(methodLabel(m, lang), Money::format(amount));
        }
        if (s.value(7).toLongLong() > 0) h += row2(L(lang, "Change", "بقایا"), Money::format(s.value(7).toLongLong()));
        h += "</table><hr/>";
        const QString footer = (lang == "ur" && !p.receiptFooterUr.isEmpty()) ? p.receiptFooterUr : p.receiptFooter;
        if (!footer.isEmpty()) h += "<div align='center'>" + esc(footer) + "</div>";
        h += "<div align='center' style='font-size:80%;'>MediCare Pro by SoftzenAgency</div>";
        return h + f.close;
    } catch (const ServiceError &) {
        return {};
    }
}

QString returnHtml(qint64 returnId, const QString &width, const QString &lang)
{
    try {
        const PharmacyProfile p = SettingsService::profile();
        QSqlQuery r = exec("SELECT r.id, r.ts, r.total, r.refund_method, r.reason, s.invoice_no FROM sales_returns r "
                           "JOIN sales s ON s.id=r.sale_id WHERE r.id=?", {returnId});
        if (!r.next()) return {};
        const Frame f = frame(width, lang);
        QString h = f.open + header(p, lang);
        h += "<div align='center' style='font-weight:bold;'>" + L(lang, "SALES RETURN", "واپسی رسید") + "</div>";
        h += "<table width='100%'>";
        h += row2(L(lang, "Return no", "واپسی نمبر"), "R" + QString::number(returnId));
        h += row2(L(lang, "Original invoice", "اصل رسید"), esc(r.value(5).toString()));
        h += row2(L(lang, "Date", "تاریخ"), localTime(r.value(1).toString()));
        h += "</table><hr/><table width='100%'>";
        QSqlQuery it = exec("SELECT m.brand_name, ri.qty, ri.amount FROM sales_return_items ri JOIN sale_items si ON si.id=ri.sale_item_id "
                            "JOIN medicines m ON m.id=si.medicine_id WHERE ri.return_id=?", {returnId});
        while (it.next())
            h += "<tr><td>" + esc(it.value(0).toString()) + "</td><td align='right'>" + QString::number(it.value(1).toInt())
                 + "</td><td align='right'>" + Money::format(it.value(2).toLongLong(), false) + "</td></tr>";
        h += "</table><hr/><table width='100%'>";
        h += row2(L(lang, "Refund", "واپس رقم"), Money::format(r.value(2).toLongLong()), true);
        h += row2(L(lang, "Refund method", "طریقہ"), methodLabel(r.value(3).toString(), lang));
        h += row2(L(lang, "Reason", "وجہ"), esc(r.value(4).toString()));
        h += "</table>";
        return h + f.close;
    } catch (const ServiceError &) {
        return {};
    }
}
}
