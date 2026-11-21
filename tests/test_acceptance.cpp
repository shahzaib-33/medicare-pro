// Automated version of the final acceptance tests in the feature spec (section N).
// Steps that need real Windows hardware (installer, cable unplug) are covered by the
// manual checklist in docs/ACCEPTANCE_TESTS.md. Here the same logic runs headless.
#include "TestUtil.h"
#include "net/ConnectivityMonitor.h"
#include "refdata/CatalogImporter.h"
#include "refdata/CatalogStats.h"
#include "refdata/MedicineSearch.h"
#include "refdata/PharmacyCatalog.h"
#include "services/InventoryService.h"
#include "services/SalesService.h"
#include "update/ReferenceUpdater.h"
#include <QSqlQuery>

using namespace mcp;

class TestAcceptance : public QObject {
    Q_OBJECT
private slots:
    void fullFlow()
    {
        // 1-2. Fresh install with a preloaded catalog.
        TestEnv env;
        QCOMPARE(CatalogStats::load(env.ref()).totalMedicines, 0);
        QVERIFY(CatalogImporter::importCsv(env.ref(), env.writeCsv("seed.csv", testCatalogCsv()), testImportOptions()).ok);
        const auto stats = CatalogStats::load(env.ref());
        QVERIFY(stats.totalMedicines > 0);
        QVERIFY(!stats.lastUpdate.isEmpty());

        MedicineSearch search(env.ref());
        // 3. English
        QCOMPARE(search.search("Panadol").hits.first().brandName, QString("Panadol"));
        // 4. Urdu
        QCOMPARE(search.search(QString::fromUtf8("پیناڈول")).hits.first().brandName, QString("Panadol"));
        // 5. Generic
        QVERIFY(search.search("Paracetamol").hits.size() >= 3);

        // 6-7. Add to pharmacy with a local price and barcode. No stock is created.
        MedicineRecord rec;
        QVERIFY(search.loadRecord(search.search("Panadol").hits.first().id, &rec));
        LocalMedicineSettings local;
        local.purchasePrice = qint64(210);
        local.salePrice = qint64(250);
        local.barcode = "8960000000011";
        local.unitsPerPack = 10;
        const auto added = PharmacyCatalog::addFromReference(env.ph(), rec, 1, local);
        QVERIFY2(added.ok, qPrintable(added.error));
        QCOMPARE(PharmacyCatalog::stockOf(env.ph(), added.medicineId), 0);
        QSqlQuery q(env.ph());
        q.exec("SELECT COUNT(*) FROM medicine_batches");
        q.next();
        QCOMPARE(q.value(0).toInt(), 0);
        // Selling without stock must fail.
        QString err;
        auto cashSale = [&](int qty) {
            Cart c;
            CartLine line;
            if (!SalesService::fillLine(added.medicineId, qty, &line, &err)) return Result::fail(err);
            c.lines = {line};
            c.payments = {{"cash", SalesService::compute(c).total, {}}};
            return SalesService::post(c, true);
        };
        QVERIFY(!cashSale(1).ok);
        // Adding the same reference medicine twice is refused.
        QVERIFY(PharmacyCatalog::addFromReference(env.ph(), rec, 1, local).alreadyExists);

        // Pharmacist receives stock as a separate step.
        QVERIFY(InventoryService::openingStock(added.medicineId, "LOT-A",
                                               QDate::currentDate().addMonths(18).toString(Qt::ISODate), 100, 210).ok);

        // 8. Disconnect the internet.
        ConnectivityMonitor net;
        net.setForcedOffline(true);
        QVERIFY(!net.isOnline());
        // 9. Search still works offline.
        QVERIFY(!search.search(QString::fromUtf8("پیناڈول")).hits.isEmpty());
        // 10. Sale offline.
        { const auto sale = cashSale(4); QVERIFY2(sale.ok, qPrintable(sale.error)); }
        QCOMPARE(PharmacyCatalog::stockOf(env.ph(), added.medicineId), 96);

        // 11-14. Reconnect, download and install a reference update.
        net.setForcedOffline(false);
        ReferencePackage pkg;
        pkg.baseVersion = 1;
        pkg.targetVersion = 2;
        pkg.publishedAt = "2026-11-21";
        pkg.description = "Acceptance test update";
        pkg.sources = {{"test-source", "Test dataset", {}, "Unit test", {}, {}}};
        MedicineRecord n;
        n.refKey = "REG:TEST0200";
        n.brandName = "Testcef";
        n.genericName = "Cefixime";
        n.ingredients = {"Cefixime"};
        n.strength = "400mg";
        n.dosageForm = "Capsule";
        n.registrationNo = "TEST-0200";
        n.registrationStatus = "active";
        n.sourceCode = "test-source";
        pkg.upserts = {n};
        QStringList problems;
        ReferencePackage parsed;
        QVERIFY(ReferenceUpdater::parsePackage(ReferenceUpdater::encodePackage(pkg, true), &parsed, &problems));
        ReferenceUpdater updater(Db::Reference, Db::Pharmacy);
        const auto res = updater.install(parsed, "acceptance");
        QVERIFY2(res.ok, qPrintable(res.error));
        QVERIFY(!MedicineSearch(env.ref()).search("cefixime").hits.isEmpty());

        // 15. Prices and stock unchanged.
        q.exec("SELECT purchase_price, sale_price, barcode FROM medicines WHERE id=" + QString::number(added.medicineId));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toLongLong(), qint64(210));
        QCOMPARE(q.value(1).toLongLong(), qint64(250));
        QCOMPARE(q.value(2).toString(), QString("8960000000011"));
        QCOMPARE(PharmacyCatalog::stockOf(env.ph(), added.medicineId), 96);
        q.exec("SELECT COUNT(*) FROM sales");
        q.next();
        QCOMPARE(q.value(0).toInt(), 1);

        // 16. History.
        q.exec("SELECT status, to_version, added FROM medicine_update_history ORDER BY id DESC LIMIT 1");
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(), QString("installed"));
        QCOMPARE(q.value(1).toInt(), 2);
        QCOMPARE(q.value(2).toInt(), 1);

        // 17. Backup and rollback.
        QVERIFY(QFile::exists(res.backupPath));
        QVERIFY(updater.restoreBackup(res.backupPath, &err));
        QCOMPARE(CatalogImporter::currentVersion(Db::get(Db::Reference)), 1);
        QVERIFY(MedicineSearch(Db::get(Db::Reference)).search("cefixime").hits.isEmpty());
        QCOMPARE(PharmacyCatalog::stockOf(env.ph(), added.medicineId), 96);
    }
};

QTEST_GUILESS_MAIN(TestAcceptance)
#include "test_acceptance.moc"
