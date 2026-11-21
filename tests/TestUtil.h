#pragma once
#include <QTemporaryDir>
#include <QTest>
#include "app/AppPaths.h"
#include "app/PerformanceProfile.h"
#include "db/Database.h"
#include "refdata/CatalogImporter.h"
#include "services/AuthService.h"

// Fresh data folder with both databases for each test.
struct TestEnv {
    QTemporaryDir dir;
    mcp::PerformanceProfile perf = mcp::PerformanceProfile::detect(mcp::PerformanceProfile::Mode::Lite);
    TestEnv()
    {
        mcp::AppPaths::setDataRoot(dir.path());
        QString err;
        if (!mcp::Db::open(mcp::Db::Pharmacy, mcp::AppPaths::pharmacyDb(), perf, &err)
            || !mcp::Db::migratePharmacy(mcp::Db::get(mcp::Db::Pharmacy), &err)
            || !mcp::Db::open(mcp::Db::Reference, mcp::AppPaths::referenceDb(), perf, &err)
            || !mcp::Db::migrateReference(mcp::Db::get(mcp::Db::Reference), &err))
            qFatal("db setup failed: %s", qPrintable(err));
        mcp::AuthService::seedRolesAndPermissions();
        // Every test runs as a real owner account so audit rows and permission checks are exercised.
        const auto owner = mcp::AuthService::createFirstOwner("owner", "Test Owner", "Owner1234");
        if (!owner.ok) qFatal("owner setup failed: %s", qPrintable(owner.error));
        const auto login = mcp::AuthService::login("owner", "Owner1234");
        if (!login.ok) qFatal("login failed: %s", qPrintable(login.error));
    }
    ~TestEnv()
    {
        mcp::Session::instance().clear();
        mcp::Db::close(mcp::Db::Pharmacy);
        mcp::Db::close(mcp::Db::Reference);
    }
    QSqlDatabase ph() const { return mcp::Db::get(mcp::Db::Pharmacy); }
    QSqlDatabase ref() const { return mcp::Db::get(mcp::Db::Reference); }

    // Writes a CSV file. Test data only. These are placeholder records for testing the software,
    // marked as test data, with made up TEST registration numbers.
    QString writeCsv(const QString &name, const QString &content) const
    {
        const QString p = dir.path() + '/' + name;
        QFile f(p);
        f.open(QIODevice::WriteOnly);
        f.write(content.toUtf8());
        return p;
    }
};

inline QString testCatalogCsv()
{
    return QStringLiteral(
        "Brand Name,Urdu Brand Name,Generic Name,Strength,Dosage Form,Manufacturer,Registration No,Status,Barcode\n"
        "Panadol,پیناڈول,Paracetamol,500mg,Tablet,Test Pharma A,TEST-0001,Active,\n"
        "Panadol Extra,,Paracetamol + Caffeine,500mg/65mg,Tablet,Test Pharma A,TEST-0002,Active,\n"
        "Calpol,,Paracetamol,120mg/5ml,Syrup,Test Pharma B,TEST-0003,Active,\n"
        "Brufen,,Ibuprofen,400mg,Tablet,Test Pharma C,TEST-0004,Active,\n"
        "Augmentin,,Amoxicillin + Clavulanic Acid,625mg,Tablet,Test Pharma A,TEST-0005,Active,\n"
        "\"Risek\",,Omeprazole,20 mg,Capsule,Test Pharma D,TEST-0006,Registered,\n");
}

inline mcp::ImportOptions testImportOptions()
{
    mcp::ImportOptions o;
    o.source = {"test-source", "Test dataset for automated tests", {}, "Unit test fixture", {}, {}};
    o.coverageNote = "Test fixture only";
    return o;
}
