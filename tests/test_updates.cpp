#include "TestUtil.h"
#include "refdata/CatalogImporter.h"
#include "refdata/CatalogStats.h"
#include "refdata/MedicineSearch.h"
#include "refdata/PharmacyCatalog.h"
#include "update/ReferenceUpdater.h"
#include "update/Signature.h"
#include "update/UpdateClient.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>

using namespace mcp;

static ReferencePackage deltaPackage(int base, int target)
{
    ReferencePackage p;
    p.baseVersion = base;
    p.targetVersion = target;
    p.publishedAt = "2026-11-20";
    p.description = "Test delta";
    p.sources = {{"test-source", "Test dataset", {}, "Unit test", {}, {}}};
    MedicineRecord n;
    n.refKey = "REG:TEST0100";
    n.brandName = "Newbrand";
    n.genericName = "Cetirizine";
    n.ingredients = {"Cetirizine"};
    n.strength = "10mg";
    n.dosageForm = "Tablet";
    n.registrationNo = "TEST-0100";
    n.registrationStatus = "active";
    n.sourceCode = "test-source";
    p.upserts.append(n);
    MedicineRecord changed;     // Panadol gets a new pack size from the source
    changed.refKey = "REG:TEST0001";
    changed.brandName = "Panadol";
    changed.brandNameUr = QString::fromUtf8("پیناڈول");
    changed.genericName = "Paracetamol";
    changed.ingredients = {"Paracetamol"};
    changed.strength = "500mg";
    changed.dosageForm = "Tablet";
    changed.manufacturer = "Test Pharma A";
    changed.registrationNo = "TEST-0001";
    changed.registrationStatus = "active";
    changed.packSize = "20 tablets";
    changed.sourceCode = "test-source";
    p.upserts.append(changed);
    p.statusChanges.append({"REG:TEST0004", "cancelled", "Registration cancelled (test)", "test-source"});
    return p;
}

class TestUpdates : public QObject {
    Q_OBJECT
    QByteArray priv, pub;
private slots:
    void initTestCase()
    {
        QString err;
        QVERIFY(Signature::generateKeyPair(&priv, &pub, &err));
    }
    void signatures()
    {
        QString err;
        const QByteArray msg = "hello";
        const QByteArray sig = Signature::sign(priv, msg, &err);
        QCOMPARE(sig.size(), 64);
        QVERIFY(Signature::verify(pub, msg, sig));
        QVERIFY(!Signature::verify(pub, "hellO", sig));
        QByteArray otherPriv, otherPub;
        Signature::generateKeyPair(&otherPriv, &otherPub, &err);
        QVERIFY(!Signature::verify(otherPub, msg, sig));
    }
    void manifestChecks()
    {
        TestEnv env;
        QJsonObject item{{"id", "ref-2"}, {"type", "reference_delta"}, {"version", "2"}, {"base_version", "1"},
                         {"url", "https://updates.example.test/ref/2.pkg"}, {"sha256", QString(64, 'a')},
                         {"size", 10}, {"signature", QString(QByteArray(64, 1).toBase64())}};
        QJsonObject m{{"format", "medicarepro.manifest/1"}, {"publisher", "SoftzenAgency"}, {"sequence", 5},
                      {"generated_at", "2026-11-20T00:00:00Z"}, {"items", QJsonArray{item}}};
        const QByteArray body = QJsonDocument(m).toJson();
        QString err;
        const QByteArray sig = Signature::sign(priv, body, &err).toBase64();
        UpdateManifest out;
        QVERIFY2(UpdateClient::verifyManifest(body, sig, pub, 0, &out, &err), qPrintable(err));
        QCOMPARE(out.items.size(), 1);
        // tampered body
        QByteArray bad = body;
        bad.replace("SoftzenAgency", "SoftzenAgencX");
        QVERIFY(!UpdateClient::verifyManifest(bad, sig, pub, 0, &out, &err));
        // replay of an older manifest
        QVERIFY(!UpdateClient::verifyManifest(body, sig, pub, 6, &out, &err));
        // plain http is refused
        QJsonObject m2 = m;
        QJsonObject i2 = item;
        i2["url"] = "http://updates.example.test/ref/2.pkg";
        m2["items"] = QJsonArray{i2};
        const QByteArray body2 = QJsonDocument(m2).toJson();
        QVERIFY(!UpdateClient::verifyManifest(body2, Signature::sign(priv, body2, &err).toBase64(), pub, 0, &out, &err));
    }
    void packageFileVerification()
    {
        TestEnv env;
        const QByteArray pkg = ReferenceUpdater::encodePackage(deltaPackage(1, 2), true);
        const QString path = env.dir.path() + "/p.pkg";
        QFile f(path); f.open(QIODevice::WriteOnly); f.write(pkg); f.close();
        UpdateItem it;
        it.type = "reference_delta"; it.version = "2"; it.size = pkg.size();
        it.sha256 = Signature::sha256(pkg);
        QString err;
        it.signature = Signature::sign(priv, Signature::packageStatement(it.type, it.version, it.sha256), &err);
        QVERIFY2(UpdateClient::verifyPackageFile(it, path, pub, &err), qPrintable(err));
        // Same file claimed as a different version must fail.
        UpdateItem other = it; other.version = "3";
        QVERIFY(!UpdateClient::verifyPackageFile(other, path, pub, &err));
        // Corrupted download must fail.
        QFile g(path); g.open(QIODevice::ReadWrite); g.seek(10); g.write("X"); g.close();
        QVERIFY(!UpdateClient::verifyPackageFile(it, path, pub, &err));
    }
    void deltaInstallPreservesPharmacyData()
    {
        TestEnv env;
        QVERIFY(CatalogImporter::importCsv(env.ref(), env.writeCsv("a.csv", testCatalogCsv()), testImportOptions()).ok);
        MedicineSearch s(env.ref());
        const auto hit = s.search("Panadol").hits.first();
        MedicineRecord rec;
        QVERIFY(s.loadRecord(hit.id, &rec));
        LocalMedicineSettings l;
        l.purchasePrice = qint64(8000); l.salePrice = qint64(9550); l.barcode = "8964000000017"; l.packSize = "10 x 10";
        l.openingBatches = {{"B1", QDate::currentDate().addYears(1), 50}};
        const auto add = PharmacyCatalog::addFromReference(env.ph(), rec, 1, l);
        QVERIFY2(add.ok, qPrintable(add.error));
        // Brufen is in the pharmacy too, and will be cancelled by the update.
        MedicineRecord bru;
        QVERIFY(s.loadRecord(s.search("Brufen").hits.first().id, &bru));
        QVERIFY(PharmacyCatalog::addFromReference(env.ph(), bru, 1, {}).ok);

        ReferenceUpdater up(Db::Reference, Db::Pharmacy);
        QStringList problems;
        ReferencePackage p;
        QVERIFY(ReferenceUpdater::parsePackage(ReferenceUpdater::encodePackage(deltaPackage(1, 2), true), &p, &problems));
        const auto pv = up.preview(p);
        QCOMPARE(pv.added, 1);
        QCOMPARE(pv.updated, 1);
        QCOMPARE(pv.deactivated, 1);

        const auto res = up.install(p, "sha");
        QVERIFY2(res.ok, qPrintable(res.error));
        QCOMPARE(res.added, 1);
        QCOMPARE(res.deactivated, 1);
        QVERIFY(res.flagged >= 2);
        QCOMPARE(CatalogImporter::currentVersion(env.ref()), 2);
        QVERIFY(!MedicineSearch(env.ref()).search("Newbrand").hits.isEmpty());

        // Pharmacy prices, barcode, pack size and stock are untouched.
        QSqlQuery q(env.ph());
        q.exec("SELECT purchase_price, sale_price, barcode, pack_size, registration_status FROM medicines WHERE brand_name='Panadol'");
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toLongLong(), qint64(8000));
        QCOMPARE(q.value(1).toLongLong(), qint64(9550));
        QCOMPARE(q.value(2).toString(), QString("8964000000017"));
        QCOMPARE(q.value(3).toString(), QString("10 x 10"));
        QCOMPARE(PharmacyCatalog::stockOf(env.ph(), add.medicineId), 50);
        // The local Brufen record is not silently changed. It is flagged.
        q.exec("SELECT registration_status FROM medicines WHERE brand_name='Brufen'");
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(), QString("active"));
        q.exec("SELECT COUNT(*) FROM medicine_review_flags WHERE reason='registration_status_changed' AND new_value='cancelled'");
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1);
        q.exec("SELECT status, added FROM medicine_update_history ORDER BY id DESC LIMIT 1");
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(), QString("installed"));

        // Installing the same version again is refused.
        QVERIFY(!up.install(p, "sha").ok);
    }
    void wrongBaseVersionRefused()
    {
        TestEnv env;
        QVERIFY(CatalogImporter::importCsv(env.ref(), env.writeCsv("a.csv", testCatalogCsv()), testImportOptions()).ok);
        ReferencePackage p = deltaPackage(5, 6);
        ReferenceUpdater up(Db::Reference, Db::Pharmacy);
        const auto res = up.install(p, "sha");
        QVERIFY(!res.ok);
        QVERIFY(res.error.contains("version 5"));
    }
    void failedInstallRollsBack()
    {
        TestEnv env;
        QVERIFY(CatalogImporter::importCsv(env.ref(), env.writeCsv("a.csv", testCatalogCsv()), testImportOptions()).ok);
        ReferenceUpdater up(Db::Reference, Db::Pharmacy);
        const auto res = up.install(deltaPackage(1, 2), "sha", true);
        QVERIFY(!res.ok);
        QVERIFY(res.rolledBack);
        QCOMPARE(CatalogImporter::currentVersion(env.ref()), 1);
        QCOMPARE(CatalogStats::load(env.ref()).totalMedicines, 6);
        QVERIFY(MedicineSearch(env.ref()).search("Newbrand").hits.isEmpty());
        QSqlQuery q(env.ph());
        q.exec("SELECT status FROM medicine_update_history ORDER BY id DESC LIMIT 1");
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(), QString("rolled_back"));
    }
    void restoreBackupAfterInstall()
    {
        TestEnv env;
        QVERIFY(CatalogImporter::importCsv(env.ref(), env.writeCsv("a.csv", testCatalogCsv()), testImportOptions()).ok);
        ReferenceUpdater up(Db::Reference, Db::Pharmacy);
        const auto res = up.install(deltaPackage(1, 2), "sha");
        QVERIFY(res.ok);
        QCOMPARE(CatalogImporter::currentVersion(env.ref()), 2);
        QString err;
        QVERIFY2(up.restoreBackup(res.backupPath, &err), qPrintable(err));
        QCOMPARE(CatalogImporter::currentVersion(Db::get(Db::Reference)), 1);
        QVERIFY(MedicineSearch(Db::get(Db::Reference)).search("Newbrand").hits.isEmpty());
    }
    void badPackagesRejected()
    {
        QStringList problems;
        ReferencePackage p;
        QVERIFY(!ReferenceUpdater::parsePackage("not json", &p, &problems));
        ReferencePackage bad = deltaPackage(1, 2);
        bad.upserts[0].sourceCode = "unknown-source";
        problems.clear();
        QVERIFY(!ReferenceUpdater::parsePackage(ReferenceUpdater::encodePackage(bad, false), &p, &problems));
        ReferencePackage dup = deltaPackage(1, 2);
        dup.upserts.append(dup.upserts[0]);
        problems.clear();
        QVERIFY(!ReferenceUpdater::parsePackage(ReferenceUpdater::encodePackage(dup, false), &p, &problems));
    }
};

QTEST_GUILESS_MAIN(TestUpdates)
#include "test_updates.moc"
