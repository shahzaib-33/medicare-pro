// Workflow tests from the master specification, section 27.
// Every test runs on fresh databases as a logged in owner account.
#include "TestUtil.h"
#include "core/Money.h"
#include "net/ConnectivityMonitor.h"
#include "refdata/MedicineSearch.h"
#include "refdata/PharmacyCatalog.h"
#include "services/BackupService.h"
#include "services/InventoryService.h"
#include "services/PurchaseService.h"
#include "services/ReceiptBuilder.h"
#include "services/ReportService.h"
#include "services/SettingsService.h"
#include "services/SalesService.h"
#include "update/ReferenceUpdater.h"
#include <QSqlQuery>

using namespace mcp;

static QString inDays(int d) { return QDate::currentDate().addDays(d).toString(Qt::ISODate); }

static qint64 newMedicine(const QString &name, Paisa cost, Paisa price, int taxBp = 0, bool rx = false)
{
    MedicineInput m;
    m.brandName = name;
    m.genericName = name + " generic";
    m.purchasePrice = cost;
    m.salePrice = price;
    m.taxRateBp = taxBp;
    m.rxRequired = rx;
    const Result r = InventoryService::addMedicine(m);
    if (!r.ok) qFatal("addMedicine: %s", qPrintable(r.error));
    return r.id;
}

static qint64 newSupplier(const QString &name = "Test Distributor")
{
    const Result r = SupplierService::save(0, name, "0300-0000000", "Test address", {}, true);
    if (!r.ok) qFatal("supplier: %s", qPrintable(r.error));
    return r.id;
}

static Result purchase(qint64 supplier, const QString &invoice, qint64 med, const QString &batch, const QString &expiry, int qty,
                       Paisa unitCost, Paisa paid = 0)
{
    PurchaseDraft d;
    d.supplierId = supplier;
    d.invoiceNo = invoice;
    d.invoiceDate = todayIso();
    PurchaseLine l;
    l.medicineId = med;
    l.batchNo = batch;
    l.expiryDate = expiry;
    l.qty = qty;
    l.unitCost = unitCost;
    d.lines = {l};
    d.paidNow = paid;
    return PurchaseService::post(d);
}

static Result cashSale(qint64 med, int qty, Paisa discount = 0, Paisa *totalOut = nullptr)
{
    Cart c;
    CartLine line;
    QString err;
    if (!SalesService::fillLine(med, qty, &line, &err)) return Result::fail(err);
    line.discount = discount;
    c.lines = {line};
    const Paisa total = SalesService::compute(c).total;
    if (totalOut) *totalOut = total;
    c.payments = {{"cash", total, {}}};
    return SalesService::post(c);
}

static int available(qint64 med) { return PharmacyCatalog::stockOf(Db::get(Db::Pharmacy), med); }

class TestWorkflows : public QObject {
    Q_OBJECT
private slots:
    // Tests 1, 2 and 3: purchase 100, sell 10, return 2.
    void purchaseSellReturn()
    {
        TestEnv env;
        const qint64 sup = newSupplier();
        const qint64 med = newMedicine("Testamol", 150, 250);
        const Result p = purchase(sup, "INV-1", med, "B1", inDays(400), 100, 150);
        QVERIFY2(p.ok, qPrintable(p.error));
        QCOMPARE(available(med), 100);
        QCOMPARE(SupplierService::balance(sup), Paisa(15000));

        // Same supplier invoice number twice is refused.
        QVERIFY(!purchase(sup, "INV-1", med, "B1", inDays(400), 5, 150).ok);
        QCOMPARE(available(med), 100);

        const Result s = cashSale(med, 10);
        QVERIFY2(s.ok, qPrintable(s.error));
        QCOMPARE(available(med), 90);

        const qint64 item = scalar("SELECT id FROM sale_items WHERE sale_id=?", {s.id}).toLongLong();
        const Result r = SalesService::returnItems(s.id, {{item, 2}}, "cash", "Customer returned unopened strips");
        QVERIFY2(r.ok, qPrintable(r.error));
        // Returned units wait in quarantine until a pharmacist inspects them.
        QCOMPARE(available(med), 90);
        QCOMPARE(scalar("SELECT qty_quarantine FROM medicine_batches WHERE medicine_id=?", {med}).toInt(), 2);
        const qint64 retItem = scalar("SELECT id FROM sales_return_items WHERE return_id=?", {r.id}).toLongLong();
        QVERIFY(InventoryService::inspectReturn(retItem, true, "Seal intact").ok);
        QCOMPARE(available(med), 92);
        QCOMPARE(scalar("SELECT qty_quarantine FROM medicine_batches WHERE medicine_id=?", {med}).toInt(), 0);
        // Cannot return more than what is left.
        QVERIFY(!SalesService::returnItems(s.id, {{item, 9}}, "cash", "too many").ok);

        // Every change is in the movement log and adds up to the batch quantity.
        QCOMPARE(scalar("SELECT SUM(qty) FROM stock_movements WHERE medicine_id=? AND bucket='available'", {med}).toInt(), 92);
    }

    // Test 4: expired stock is never sold.
    void expiredSaleBlocked()
    {
        TestEnv env;
        const qint64 sup = newSupplier();
        const qint64 med = newMedicine("Oldcillin", 100, 200);
        // Receive a batch that expires tomorrow, then pretend the date moved on by selling "as of" a later day.
        QVERIFY(purchase(sup, "INV-E", med, "EXP1", inDays(1), 20, 100).ok);
        bool threw = false;
        try { InventoryService::allocateFefo(med, 1, inDays(2)); } catch (const ServiceError &e) {
            threw = true;
            QVERIFY(e.message.contains("expired"));
        }
        QVERIFY(threw);
        // A batch already expired today is also excluded from sale.
        exec("UPDATE medicine_batches SET expiry_date=? WHERE batch_no='EXP1'", {todayIso()});
        QVERIFY(!cashSale(med, 1).ok);
        QCOMPARE(scalar("SELECT qty_available FROM medicine_batches WHERE batch_no='EXP1'").toInt(), 20);
        // Purchases with a past expiry are refused.
        QVERIFY(!purchase(sup, "INV-E2", med, "EXP2", inDays(-5), 5, 100).ok);
        // Expired stock can only be written off with a reason.
        const qint64 batch = scalar("SELECT id FROM medicine_batches WHERE batch_no='EXP1'").toLongLong();
        QVERIFY(InventoryService::adjust(batch, "dispose_expired", 20, "Sent for destruction").ok);
        QCOMPARE(scalar("SELECT qty_available FROM medicine_batches WHERE id=?", {batch}).toInt(), 0);
    }

    // Test 5: first expiry first out.
    void fefo()
    {
        TestEnv env;
        const qint64 sup = newSupplier();
        const qint64 med = newMedicine("Fefomycin", 100, 180);
        QVERIFY(purchase(sup, "F-1", med, "LATE", inDays(500), 10, 100).ok);
        QVERIFY(purchase(sup, "F-2", med, "SOON", inDays(60), 4, 110).ok);
        QVERIFY(purchase(sup, "F-3", med, "MID", inDays(200), 6, 105).ok);
        const Result s = cashSale(med, 7);
        QVERIFY2(s.ok, qPrintable(s.error));
        auto qtyOf = [](const QString &b) { return scalar("SELECT qty_available FROM medicine_batches WHERE batch_no=?", {b}).toInt(); };
        QCOMPARE(qtyOf("SOON"), 0);
        QCOMPARE(qtyOf("MID"), 3);
        QCOMPARE(qtyOf("LATE"), 10);
        // Cost of goods uses the real batch costs: 4 x 1.10 + 3 x 1.05.
        QCOMPARE(scalar("SELECT cost_total FROM sales WHERE id=?", {s.id}).toLongLong(), Paisa(4 * 110 + 3 * 105));
        // A recalled batch is skipped.
        const qint64 mid = scalar("SELECT id FROM medicine_batches WHERE batch_no='MID'").toLongLong();
        QVERIFY(InventoryService::setBatchStatus(mid, "recalled", "Manufacturer recall").ok);
        QVERIFY(cashSale(med, 2).ok);
        QCOMPARE(qtyOf("MID"), 3);
        QCOMPARE(qtyOf("LATE"), 8);
        // No negative stock.
        QVERIFY(!cashSale(med, 9).ok);
        QCOMPARE(qtyOf("LATE"), 8);
    }

    // Test 6: financial calculations in whole paisa.
    void financialCalculations()
    {
        TestEnv env;
        QCOMPARE(Money::format(123456789), QString("Rs 1,234,567.89"));
        Paisa p = 0;
        QVERIFY(Money::parse("1,250.5", &p));
        QCOMPARE(p, Paisa(125050));
        QCOMPARE(Money::percentOf(1005, 1700), Paisa(171));   // 17% of 10.05 = 1.7085 -> 1.71
        const auto parts = Money::allocate(1000, {1, 1, 1});
        QCOMPARE(parts[0] + parts[1] + parts[2], Paisa(1000));

        const qint64 sup = newSupplier();
        const qint64 med = newMedicine("Taxacid", 300, 333, 1700);
        QVERIFY(purchase(sup, "T-1", med, "T1", inDays(300), 50, 300, 5000).ok);
        QCOMPARE(SupplierService::balance(sup), Paisa(15000 - 5000));

        Cart c;
        CartLine l;
        QString err;
        QVERIFY(SalesService::fillLine(med, 3, &l, &err));
        l.discount = 99;                 // 9.99 - 0.99 = 9.00 net
        c.lines = {l};
        c.billDiscount = 53;
        const CartTotals t = SalesService::compute(c);
        QCOMPARE(t.subtotal, Paisa(999));
        QCOMPARE(t.tax, Paisa(153));     // 17% of 9.00
        QCOMPARE(t.total, Paisa(999 - 99 - 53 + 153));
        // Split payment: card plus cash with change.
        c.payments = {{"card", 500, "AUTH1"}, {"cash", 1000, {}}};
        const CartTotals paid = SalesService::compute(c);
        QCOMPARE(paid.change, Paisa(1500 - t.total));
        const Result s = SalesService::post(c);
        QVERIFY2(s.ok, qPrintable(s.error));
        QCOMPARE(scalar("SELECT SUM(amount) FROM sale_payments WHERE sale_id=?", {s.id}).toLongLong(), t.total);
        QCOMPARE(scalar("SELECT total - cost_total FROM sales WHERE id=?", {s.id}).toLongLong(), t.total - 900);

        // Short payment is refused and nothing is saved.
        const int before = scalar("SELECT COUNT(*) FROM sales").toInt();
        Cart shortCart = c;
        shortCart.payments = {{"cash", 100, {}}};
        QVERIFY(!SalesService::post(shortCart).ok);
        QCOMPARE(scalar("SELECT COUNT(*) FROM sales").toInt(), before);

        // Credit sale needs a customer and respects the limit.
        const Result cust = CustomerService::save(0, "Credit Customer", "0301", {}, {}, 1000, true);
        QVERIFY(cust.ok);
        Cart credit;
        QVERIFY(SalesService::fillLine(med, 2, &l, &err));
        credit.lines = {l};
        const Paisa ct = SalesService::compute(credit).total;
        credit.payments = {{"customer_credit", ct, {}}};
        QVERIFY(!SalesService::post(credit).ok);          // no customer chosen
        credit.customerId = cust.id;
        QVERIFY(SalesService::post(credit).ok);
        QCOMPARE(CustomerService::balance(cust.id), ct);
        QVERIFY(!SalesService::post(credit).ok);          // over the 10.00 limit now
        QVERIFY(CustomerService::receivePayment(cust.id, ct, "cash", {}).ok);
        QCOMPARE(CustomerService::balance(cust.id), Paisa(0));
    }

    // Hold and resume, shifts and expenses.
    void holdShiftExpense()
    {
        TestEnv env;
        const qint64 sup = newSupplier();
        const qint64 med = newMedicine("Holdex", 100, 200);
        QVERIFY(purchase(sup, "H-1", med, "H1", inDays(300), 30, 100).ok);
        QVERIFY(ShiftService::open(5000).ok);
        Cart c;
        CartLine l;
        QString err;
        QVERIFY(SalesService::fillLine(med, 3, &l, &err));
        c.lines = {l};
        const Result h = SalesService::hold(c, "Customer went to car");
        QVERIFY(h.ok);
        QCOMPARE(available(med), 30);    // holding does not take stock
        Cart back;
        QVERIFY(SalesService::resume(h.id, &back).ok);
        QCOMPARE(back.lines.size(), 1);
        QCOMPARE(back.lines[0].qty, 3);
        back.payments = {{"cash", 1000, {}}};
        QVERIFY(SalesService::post(back).ok);
        QVERIFY(ExpenseService::add(ExpenseService::categoryId("Tea"), todayIso(), "Tea for staff", 150, "cash", {}, {}).ok);
        QVERIFY(ShiftService::cashMovement("payout", 1000, "Bank deposit").ok);
        const qint64 shift = ShiftService::currentShift(Session::instance().userId());
        QCOMPARE(ShiftService::expectedCash(shift), Paisa(5000 + 600 - 150 - 1000));
        QVERIFY(ShiftService::close(4400, "Counted").ok);
        QCOMPARE(scalar("SELECT variance FROM cash_shifts WHERE id=?", {shift}).toLongLong(), Paisa(-50));
    }

    // Purchase return and reversal.
    void purchaseReturnAndReverse()
    {
        TestEnv env;
        const qint64 sup = newSupplier();
        const qint64 med = newMedicine("Retura", 200, 300);
        const Result p = purchase(sup, "R-1", med, "R1", inDays(300), 40, 200);
        QVERIFY(p.ok);
        const qint64 batch = scalar("SELECT id FROM medicine_batches WHERE batch_no='R1'").toLongLong();
        QVERIFY(PurchaseService::returnToSupplier(sup, p.id, {{batch, "available", 5}}, "Damaged cartons").ok);
        QCOMPARE(available(med), 35);
        QCOMPARE(SupplierService::balance(sup), Paisa(40 * 200 - 5 * 200));
        const Result p2 = purchase(sup, "R-2", med, "R2", inDays(320), 10, 200);
        QVERIFY(PurchaseService::reverse(p2.id, "Entered by mistake").ok);
        QCOMPARE(available(med), 35);
        QCOMPARE(scalar("SELECT status FROM purchase_invoices WHERE id=?", {p2.id}).toString(), QString("reversed"));
        QCOMPARE(SupplierService::balance(sup), Paisa(35 * 200));
    }

    // Permissions are enforced in the services, not only in the screens.
    void permissionsAndLogin()
    {
        TestEnv env;
        QVERIFY(AuthService::createUser("cashier1", "Cashier One", "Cash1234", "cashier").ok);
        QVERIFY(!AuthService::createUser("weak", "Weak", "abc", "cashier").ok);
        const qint64 med = newMedicine("Permitol", 100, 200);
        AuthService::logout();
        for (int i = 0; i < 5; ++i) QVERIFY(!AuthService::login("cashier1", "wrong-pass1").ok);
        QVERIFY(!AuthService::login("cashier1", "Cash1234").ok);   // locked for a few minutes
        exec("UPDATE users SET locked_until=NULL WHERE username='cashier1'");
        QVERIFY(AuthService::login("cashier1", "Cash1234").ok);
        QVERIFY(Session::instance().can("pos.sell"));
        QVERIFY(!Session::instance().can("inventory.adjust"));
        QVERIFY(!InventoryService::openingStock(med, "X", inDays(100), 10, 100).ok);
        QVERIFY(!InventoryService::deleteMedicine(med).ok);
        QVERIFY(scalar("SELECT COUNT(*) FROM login_history WHERE event='locked'").toInt() >= 1);
        QVERIFY(scalar("SELECT COUNT(*) FROM audit_logs").toInt() > 0);
    }

    // Test 7: offline mode. Everything local works without the internet.
    void offlineMode()
    {
        TestEnv env;
        ConnectivityMonitor net;
        net.setForcedOffline(true);
        QVERIFY(!net.isOnline());
        const qint64 sup = newSupplier();
        const qint64 med = newMedicine("Offlinex", 100, 200);
        QVERIFY(purchase(sup, "O-1", med, "O1", inDays(300), 10, 100).ok);
        Cart c;
        CartLine l;
        QString err;
        QVERIFY(SalesService::fillLine(med, 2, &l, &err));
        c.lines = {l};
        c.payments = {{"cash", 400, {}}};
        const Result s = SalesService::post(c, !net.isOnline());
        QVERIFY(s.ok);
        QCOMPARE(scalar("SELECT offline FROM sales WHERE id=?", {s.id}).toInt(), 1);
        QCOMPARE(available(med), 8);
    }

    // Test 8: a reference update never changes local prices, stock or sales.
    void referenceUpdateKeepsLocalData()
    {
        TestEnv env;
        QVERIFY(CatalogImporter::importCsv(env.ref(), env.writeCsv("a.csv", testCatalogCsv()), testImportOptions()).ok);
        MedicineSearch search(env.ref());
        MedicineRecord rec;
        QVERIFY(search.loadRecord(search.search("Brufen").hits.first().id, &rec));
        LocalMedicineSettings local;
        local.purchasePrice = qint64(500);
        local.salePrice = qint64(650);
        local.openingBatches = {{"BR1", QDate::currentDate().addYears(1), 40}};
        const auto added = PharmacyCatalog::addFromReference(env.ph(), rec, 1, local);
        QVERIFY2(added.ok, qPrintable(added.error));
        QVERIFY(cashSale(added.medicineId, 5).ok);

        ReferencePackage pkg;
        pkg.baseVersion = 1;
        pkg.targetVersion = 2;
        pkg.publishedAt = "2026-11-21";
        rec.registrationStatus = "cancelled";
        rec.strength = "400 mg";
        pkg.upserts = {rec};
        pkg.sources = {{"test-source", "Test dataset", {}, "Unit test", {}, {}}};
        ReferencePackage parsed;
        QStringList problems;
        QVERIFY(ReferenceUpdater::parsePackage(ReferenceUpdater::encodePackage(pkg, true), &parsed, &problems));
        ReferenceUpdater updater(Db::Reference, Db::Pharmacy);
        const auto res = updater.install(parsed, "test");
        QVERIFY2(res.ok, qPrintable(res.error));
        QCOMPARE(scalar("SELECT sale_price FROM medicines WHERE id=?", {added.medicineId}).toLongLong(), Paisa(650));
        QCOMPARE(scalar("SELECT purchase_price FROM medicines WHERE id=?", {added.medicineId}).toLongLong(), Paisa(500));
        QCOMPARE(available(added.medicineId), 35);
        QCOMPARE(scalar("SELECT COUNT(*) FROM sales").toInt(), 1);
        QVERIFY(scalar("SELECT COUNT(*) FROM medicine_review_flags WHERE medicine_id=?", {added.medicineId}).toInt() >= 1);
    }

    // New purchase price applies to all stock, old and new, up and down.
    void purchaseSalePriceAppliesToAllStock()
    {
        TestEnv env;
        const qint64 sup = newSupplier();
        const qint64 med = newMedicine("Pricol", 300, 500);
        QVERIFY(purchase(sup, "PR-1", med, "OLD1", inDays(200), 50, 300).ok);
        auto buy = [&](const QString &inv, const QString &batch, Paisa sale) {
            PurchaseDraft d;
            d.supplierId = sup; d.invoiceNo = inv; d.invoiceDate = todayIso();
            PurchaseLine l;
            l.medicineId = med; l.batchNo = batch; l.expiryDate = inDays(400); l.qty = 20; l.unitCost = 600;
            l.salePrice = sale;
            d.lines = {l};
            return PurchaseService::post(d);
        };
        QVERIFY(buy("PR-2", "NEW1", 800).ok);
        QCOMPARE(scalar("SELECT sale_price FROM medicines WHERE id=?", {med}).toLongLong(), Paisa(800));
        CartLine line; QString err;
        QVERIFY(SalesService::fillLine(med, 1, &line, &err));
        QCOMPARE(line.unitPrice, Paisa(800));   // old batch OLD1 is sold first, at the new price
        QCOMPARE(scalar("SELECT COUNT(*) FROM medicine_batches WHERE medicine_id=? AND sale_price<>800", {med}).toInt(), 0);
        QVERIFY(cashSale(med, 2).ok);
        QVERIFY(buy("PR-3", "NEW2", 450).ok);
        QCOMPARE(scalar("SELECT sale_price FROM medicines WHERE id=?", {med}).toLongLong(), Paisa(450));
        QVERIFY(scalar("SELECT COUNT(*) FROM audit_logs WHERE action='medicine_price_changed' AND entity_id=?", {med}).toInt() == 2);
    }

    // A daily online update with a new official price changes the sale price, up or down.
    void referenceUpdateAppliesOfficialPrice()
    {
        TestEnv env;
        QVERIFY(CatalogImporter::importCsv(env.ref(), env.writeCsv("a.csv", testCatalogCsv()), testImportOptions()).ok);
        MedicineSearch search(env.ref());
        MedicineRecord rec;
        QVERIFY(search.loadRecord(search.search("Brufen").hits.first().id, &rec));
        LocalMedicineSettings local;
        local.purchasePrice = qint64(500);
        local.salePrice = qint64(650);
        local.openingBatches = {{"BR1", QDate::currentDate().addYears(1), 40}};
        const auto added = PharmacyCatalog::addFromReference(env.ph(), rec, 1, local);
        QVERIFY2(added.ok, qPrintable(added.error));
        const int perPack = scalar("SELECT units_per_pack FROM medicines WHERE id=?", {added.medicineId}).toInt();

        auto install = [&](int from, int to, double packPrice) {
            ReferencePackage pkg;
            pkg.baseVersion = from; pkg.targetVersion = to; pkg.publishedAt = "2026-11-21";
            rec.referencePrice = packPrice; rec.priceSource = "Test notification";
            pkg.upserts = {rec};
            pkg.sources = {{"test-source", "Test dataset", {}, "Unit test", {}, {}}};
            ReferencePackage parsed; QStringList problems;
            if (!ReferenceUpdater::parsePackage(ReferenceUpdater::encodePackage(pkg, true), &parsed, &problems)) return InstallResult{};
            ReferenceUpdater updater(Db::Reference, Db::Pharmacy);
            updater.setApplyPrices(true);
            return updater.install(parsed, "test");
        };
        auto up = install(1, 2, 80.0 * perPack);
        QVERIFY2(up.ok, qPrintable(up.error));
        QCOMPARE(up.pricesChanged, 1);
        QCOMPARE(scalar("SELECT sale_price FROM medicines WHERE id=?", {added.medicineId}).toLongLong(), Paisa(8000));
        auto down = install(2, 3, 50.0 * perPack);
        QVERIFY2(down.ok, qPrintable(down.error));
        QCOMPARE(scalar("SELECT sale_price FROM medicines WHERE id=?", {added.medicineId}).toLongLong(), Paisa(5000));
        QCOMPARE(available(added.medicineId), 40);
    }

    // Test 9: backup and restore keep the data exactly.
    void backupRestoreIntegrity()
    {
        TestEnv env;
        const qint64 sup = newSupplier();
        const qint64 med = newMedicine("Backupol", 100, 200);
        QVERIFY(purchase(sup, "BK-1", med, "BK1", inDays(300), 50, 100).ok);
        QVERIFY(cashSale(med, 5).ok);
        QString path;
        const Result b = BackupService::create("manual", {}, &path);
        QVERIFY2(b.ok, qPrintable(b.error));
        QVERIFY(BackupService::verify(path).ok);
        const Paisa salesTotal = scalar("SELECT SUM(total) FROM sales").toLongLong();

        // Work continues after the backup.
        QVERIFY(cashSale(med, 20).ok);
        QCOMPARE(available(med), 25);

        const Result r = BackupService::restore(path);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(available(med), 45);
        QCOMPARE(scalar("SELECT SUM(total) FROM sales").toLongLong(), salesTotal);
        QCOMPARE(scalar("PRAGMA integrity_check").toString(), QString("ok"));
        // A safety copy of the data before restore exists.
        bool safety = false;
        for (const auto &i : BackupService::list()) safety |= i.kind == "pre_restore";
        QVERIFY(safety);

        // A damaged backup is refused.
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        f.seek(f.size() / 2);
        f.write("damage");
        f.close();
        QVERIFY(!BackupService::verify(path).ok);
        QVERIFY(!BackupService::restore(path).ok);
        QCOMPARE(available(med), 45);
    }

    // Receipts: English and Urdu, every paper size, reprints are marked and logged.
    void receiptsAndReports()
    {
        TestEnv env;
        PharmacyProfile p = SettingsService::profile();
        p.name = "Test Pharmacy";
        p.nameUr = QString::fromUtf8("ٹیسٹ فارمیسی");
        QVERIFY(SettingsService::saveProfile(p).ok);
        const qint64 sup = newSupplier();
        const qint64 med = newMedicine("Receiptol", 100, 250);
        QVERIFY(purchase(sup, "RC-1", med, "RC1", inDays(300), 10, 100).ok);
        const Result s = cashSale(med, 2);
        QVERIFY(s.ok);
        const QString invoice = scalar("SELECT invoice_no FROM sales WHERE id=?", {s.id}).toString();
        for (const QString &w : {"58", "80", "A4"}) {
            const QString en = ReceiptBuilder::saleHtml(s.id, w, "en", 0);
            QVERIFY(en.contains("Test Pharmacy"));
            QVERIFY(en.contains(invoice));
            QVERIFY(en.contains("5.00"));
            QVERIFY(!en.contains("DUPLICATE"));
        }
        QVERIFY(ReceiptBuilder::saleHtml(s.id, "80", "ur", 0).contains(QString::fromUtf8("ٹیسٹ فارمیسی")));
        int copy = -1;
        QVERIFY(SalesService::logPrint(s.id, "80", &copy).ok);
        QCOMPARE(copy, 0);
        QVERIFY(SalesService::logPrint(s.id, "80", &copy).ok);
        QCOMPARE(copy, 1);
        QVERIFY(ReceiptBuilder::saleHtml(s.id, "80", "en", copy).contains("DUPLICATE COPY 1"));
        QVERIFY(scalar("SELECT COUNT(*) FROM audit_logs WHERE action='receipt_reprinted'").toInt() == 1);

        // Reports use the same integer money and agree with the sales table.
        const ReportTable daily = ReportService::run("sales_daily", todayIso(), todayIso());
        QCOMPARE(daily.rows.size(), 1);
        QCOMPARE(daily.rows[0][5].toLongLong(), Paisa(500));
        QCOMPARE(daily.rows[0][7].toLongLong(), Paisa(300));    // 5.00 sale - 2.00 cost
        QVERIFY(daily.toCsv().contains("\"5.00\""));
        const ReportTable pl = ReportService::run("profit_loss", todayIso(), todayIso());
        QCOMPARE(pl.rows.last()[1].toLongLong(), Paisa(300));
        const auto k = ReportService::kpis(todayIso(), todayIso());
        QCOMPARE(k.sales, Paisa(500));
        QCOMPARE(k.invoices, 1);
        // CSV cells that look like formulas are neutralised.
        ReportTable t;
        t.headers = {"a"};
        t.rows = {{QString("=HYPERLINK(1)")}};
        QVERIFY(t.toCsv().contains("'=HYPERLINK"));
    }
};

QTEST_GUILESS_MAIN(TestWorkflows)
#include "test_workflows.moc"
