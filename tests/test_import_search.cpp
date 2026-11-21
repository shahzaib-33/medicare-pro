#include "TestUtil.h"
#include "refdata/CatalogImporter.h"
#include "refdata/CatalogStats.h"
#include "refdata/MedicineSearch.h"
#include <QElapsedTimer>
#include <QSqlQuery>

using namespace mcp;

class TestImportSearch : public QObject {
    Q_OBJECT
    bool hasBrand(const SearchResult &r, const QString &b)
    {
        for (const auto &h : r.hits) if (h.brandName == b) return true;
        return false;
    }
private slots:
    void importValidCsv()
    {
        TestEnv env;
        const auto rep = CatalogImporter::importCsv(env.ref(), env.writeCsv("a.csv", testCatalogCsv()), testImportOptions());
        QVERIFY2(rep.ok, qPrintable(rep.error));
        QCOMPARE(rep.added, 6);
        QCOMPARE(rep.newVersion, 1);
        QVERIFY(QFile::exists(rep.backupPath));
        const auto st = CatalogStats::load(env.ref());
        QCOMPARE(st.totalMedicines, 6);
        QCOMPARE(st.unverified, 6);   // not marked verified by the admin
        QVERIFY(st.coverageText().contains("may not include every medicine"));

        // Re-import same file: nothing changes.
        const auto rep2 = CatalogImporter::importCsv(env.ref(), env.writeCsv("a.csv", testCatalogCsv()), testImportOptions());
        QVERIFY(rep2.ok);
        QCOMPARE(rep2.added, 0);
        QCOMPARE(rep2.unchanged, 6);
    }
    void rejectsBadRowsAndDuplicates()
    {
        TestEnv env;
        const QString csv = "Brand Name,Registration No,Barcode,Status,Price\n"
                            "Good One,TEST-1,,Active,\n"
                            ",TEST-2,,Active,\n"
                            "Bad Barcode,TEST-3,12AB,Active,\n"
                            "Dup,TEST-1,,Active,\n"
                            "Bad Status,TEST-4,,Maybe,\n"
                            "Price No Source,TEST-5,,Active,100\n";
        const auto rep = CatalogImporter::importCsv(env.ref(), env.writeCsv("b.csv", csv), testImportOptions());
        QVERIFY(rep.ok);
        QCOMPARE(rep.added, 1);
        QCOMPARE(rep.skipped, 4);
        QCOMPARE(rep.duplicates, 1);
    }
    void sampleDataIsReplacedByRealImport()
    {
        TestEnv env;
        auto sample = testImportOptions();
        sample.markSample = true;
        sample.source.code = "sample";
        QVERIFY(CatalogImporter::importCsv(env.ref(), env.writeCsv("s.csv", "Brand Name\nDemo One\nDemo Two\n"), sample).ok);
        QCOMPARE(CatalogStats::load(env.ref()).sample, 2);
        const auto rep = CatalogImporter::importCsv(env.ref(), env.writeCsv("a.csv", testCatalogCsv()), testImportOptions());
        QVERIFY(rep.ok);
        QCOMPARE(rep.sampleRemoved, 2);
        const auto st = CatalogStats::load(env.ref());
        QCOMPARE(st.sample, 0);
        QCOMPARE(st.totalMedicines, 6);
        QVERIFY(MedicineSearch(env.ref()).search("demo").hits.isEmpty());
    }
    void dryRunWritesNothing()
    {
        TestEnv env;
        auto opt = testImportOptions();
        opt.dryRun = true;
        const auto rep = CatalogImporter::importCsv(env.ref(), env.writeCsv("a.csv", testCatalogCsv()), opt);
        QVERIFY(rep.ok);
        QCOMPARE(CatalogStats::load(env.ref()).totalMedicines, 0);
    }
    void bilingualSearch()
    {
        TestEnv env;
        QVERIFY(CatalogImporter::importCsv(env.ref(), env.writeCsv("a.csv", testCatalogCsv()), testImportOptions()).ok);
        MedicineSearch s(env.ref());

        auto r = s.search("Panadol");
        QVERIFY(!r.hits.isEmpty());
        QCOMPARE(r.hits.first().brandName, QString("Panadol"));

        r = s.search(QString::fromUtf8("پیناڈول"));
        QVERIFY(!r.hits.isEmpty());
        QCOMPARE(r.hits.first().brandName, QString("Panadol"));
        QCOMPARE(r.hits.first().matchedBy, QString("urdu"));

        // Arabic letter variant typed on some keyboards
        r = s.search(QString::fromUtf8("پيناڈول"));
        QCOMPARE(r.hits.first().brandName, QString("Panadol"));

        r = s.search("paracetamol");
        QVERIFY(hasBrand(r, "Panadol"));
        QVERIFY(hasBrand(r, "Panadol Extra"));
        QVERIFY(hasBrand(r, "Calpol"));
        QVERIFY(!hasBrand(r, "Brufen"));

        // Spelling mistakes
        r = s.search("parasetamol");
        QVERIFY(hasBrand(r, "Calpol"));
        r = s.search("panadole");
        QVERIFY(hasBrand(r, "Panadol"));

        // Urdu spelling of a brand without an Urdu name stored (cross script sound match)
        r = s.search(QString::fromUtf8("بروفین"));
        QVERIFY(hasBrand(r, "Brufen"));

        // Registration number, manufacturer, strength, composition words
        r = s.search("TEST-0004");
        QCOMPARE(r.hits.first().brandName, QString("Brufen"));
        QCOMPARE(r.hits.first().matchedBy, QString("registration"));
        r = s.search("test pharma d");
        QVERIFY(hasBrand(r, "Risek"));
        r = s.search("omeprazole 20mg");
        QVERIFY(hasBrand(r, "Risek"));
        r = s.search("clavulanic");
        QVERIFY(hasBrand(r, "Augmentin"));

        // Suggestions
        r = s.search("augmentim");
        QVERIFY(r.suggestions.contains("augmentin") || hasBrand(r, "Augmentin"));
    }
    void searchIsFastOnLargeCatalog()
    {
        TestEnv env;
        QString csv = "Brand Name,Generic Name,Strength,Dosage Form,Manufacturer,Registration No\n";
        const QStringList gens = {"Paracetamol", "Ibuprofen", "Amoxicillin", "Omeprazole", "Metformin",
                                  "Cetirizine", "Azithromycin", "Ciprofloxacin", "Losartan", "Atorvastatin"};
        for (int i = 0; i < 20000; ++i)
            csv += QStringLiteral("Testbrand%1,%2,%3mg,Tablet,Maker %4,LOAD-%1\n")
                       .arg(i).arg(gens[i % gens.size()]).arg(100 + i % 9 * 50).arg(i % 300);
        auto opt = testImportOptions();
        QElapsedTimer t;
        t.start();
        const auto rep = CatalogImporter::importCsv(env.ref(), env.writeCsv("big.csv", csv), opt);
        QVERIFY(rep.ok);
        qInfo() << "Imported 20000 rows in" << t.elapsed() << "ms";
        MedicineSearch s(env.ref());
        t.restart();
        for (const char *q : {"testbrand1234", "metformin 500mg", "maker 12", "LOAD-19999", "azitromycin"})
            QVERIFY(!s.search(q, 100).hits.isEmpty());
        const qint64 ms = t.elapsed();
        qInfo() << "5 searches on 20000 rows took" << ms << "ms";
        QVERIFY(ms < 3000);
    }
};

QTEST_GUILESS_MAIN(TestImportSearch)
#include "test_import_search.moc"
