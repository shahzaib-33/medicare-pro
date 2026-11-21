// refpack: SoftzenAgency publishing tool for MediCare Pro updates.
// Runs on the publisher's PC only. Never ship the private key with the app.
//
//   refpack keygen <outdir>
//   refpack import  --db master.db --csv file.csv --source-code C --source-name N [--source-url U]
//                   [--access-method M] [--verified | --sample] [--coverage "note"]
//   refpack export  --db master.db --base N --out file.pkg [--full] [--description D]
//   refpack sign    --key private.pem --type T --version V --file F --url URL [--title T] [--description D]
//                   [--base-version B] [--components a,b] [--major]
//   refpack manifest --key private.pem --manifest manifest.json
#include "app/AppPaths.h"
#include "app/PerformanceProfile.h"
#include "db/Database.h"
#include "refdata/CatalogImporter.h"
#include "refdata/MedicineSearch.h"
#include "update/ReferenceUpdater.h"
#include "update/Signature.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDate>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QTextStream>

using namespace mcp;

static QTextStream out(stdout);
static QTextStream errs(stderr);

static bool writeFile(const QString &p, const QByteArray &d)
{
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(d);
    return true;
}

static QByteArray readFile(const QString &p)
{
    QFile f(p);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static bool openMaster(const QString &path)
{
    QString err;
    AppPaths::setDataRoot(QFileInfo(path).absolutePath());
    const auto perf = PerformanceProfile::detect(PerformanceProfile::Mode::Standard);
    if (!Db::open(Db::Reference, path, perf, &err) || !Db::migrateReference(Db::get(Db::Reference), &err)) {
        errs << "Cannot open " << path << ": " << err << Qt::endl;
        return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser p;
    p.addHelpOption();
    p.addPositionalArgument("command", "keygen | import | export | sign | manifest");
    for (const char *o : {"db", "csv", "source-code", "source-name", "source-url", "access-method", "coverage",
                          "base", "out", "description", "key", "type", "version", "file", "url", "title",
                          "base-version", "components", "manifest"})
        p.addOption(QCommandLineOption(o, o, "value"));
    p.addOption(QCommandLineOption("verified", "Mark records as verified official data"));
    p.addOption(QCommandLineOption("full", "Export every record"));
    p.addOption(QCommandLineOption("sample", "Mark records as demonstration sample data"));
    p.addOption(QCommandLineOption("major", "Major software update"));
    p.process(app);
    const QStringList pos = p.positionalArguments();
    if (pos.isEmpty()) p.showHelp(1);
    const QString cmd = pos.first();
    QString err;

    if (cmd == "keygen") {
        const QString dir = pos.value(1, ".");
        QByteArray priv, pub;
        if (!Signature::generateKeyPair(&priv, &pub, &err)) { errs << err << Qt::endl; return 1; }
        writeFile(dir + "/update_private.pem", priv);
        writeFile(dir + "/update_public.pem", pub);
        out << "Wrote update_private.pem (keep offline) and update_public.pem (copy to resources/keys)" << Qt::endl;
        return 0;
    }
    if (cmd == "import") {
        if (!openMaster(p.value("db"))) return 1;
        ImportOptions o;
        o.source = {p.value("source-code"), p.value("source-name"), p.value("source-url"), p.value("access-method"),
                    {}, QDate::currentDate().toString(Qt::ISODate)};
        o.markVerified = p.isSet("verified");
        o.markSample = p.isSet("sample");
        o.coverageNote = p.value("coverage");
        const auto rep = CatalogImporter::importCsv(Db::get(Db::Reference), p.value("csv"), o);
        out << "rows " << rep.rowsRead << ", added " << rep.added << ", updated " << rep.updated << ", unchanged "
            << rep.unchanged << ", skipped " << rep.skipped << ", duplicates " << rep.duplicates << Qt::endl;
        for (const auto &i : rep.issues) out << "  line " << i.line << ": " << i.message << Qt::endl;
        if (!rep.ok) { errs << rep.error << Qt::endl; return 1; }
        out << "master version is now " << rep.newVersion << Qt::endl;
        return 0;
    }
    if (cmd == "export") {
        if (!openMaster(p.value("db"))) return 1;
        QSqlDatabase db = Db::get(Db::Reference);
        const int base = p.isSet("full") ? 0 : p.value("base").toInt();
        ReferencePackage pkg;
        pkg.full = p.isSet("full");
        pkg.baseVersion = base;
        pkg.targetVersion = CatalogImporter::currentVersion(db);
        pkg.publishedAt = QDate::currentDate().toString(Qt::ISODate);
        pkg.description = p.value("description");
        QSqlQuery cv(db);
        cv.exec("SELECT coverage_note FROM medicine_database_versions ORDER BY version DESC LIMIT 1");
        if (cv.next()) pkg.coverageNote = cv.value(0).toString();
        if (!pkg.full && base >= pkg.targetVersion) { errs << "Nothing new after version " << base << Qt::endl; return 1; }
        QSqlQuery s(db);
        s.exec("SELECT code, name, url, access_method, terms_note, retrieved_at FROM medicine_data_sources");
        while (s.next())
            pkg.sources.append({s.value(0).toString(), s.value(1).toString(), s.value(2).toString(),
                                s.value(3).toString(), s.value(4).toString(), s.value(5).toString()});
        MedicineSearch reader(db);
        QSqlQuery q(db);
        q.prepare("SELECT id FROM national_medicine_catalog WHERE version_changed > ? ORDER BY id");
        q.addBindValue(base);
        q.exec();
        while (q.next()) {
            MedicineRecord r;
            if (reader.loadRecord(q.value(0).toLongLong(), &r)) pkg.upserts.append(r);
        }
        const QByteArray data = ReferenceUpdater::encodePackage(pkg, true);
        if (!writeFile(p.value("out"), data)) { errs << "Cannot write output" << Qt::endl; return 1; }
        out << "Package " << base << " -> " << pkg.targetVersion << " with " << pkg.upserts.size() << " records, "
            << data.size() << " bytes" << Qt::endl;
        return 0;
    }
    if (cmd == "sign") {
        const QByteArray priv = readFile(p.value("key"));
        qint64 size = 0;
        const QString sha = Signature::sha256File(p.value("file"), &size);
        if (sha.isEmpty()) { errs << "Cannot read file" << Qt::endl; return 1; }
        const QByteArray sig = Signature::sign(priv, Signature::packageStatement(p.value("type"), p.value("version"), sha), &err);
        if (sig.isEmpty()) { errs << err << Qt::endl; return 1; }
        QJsonObject item{{"id", p.value("type") + "-" + p.value("version")}, {"type", p.value("type")},
                         {"version", p.value("version")}, {"title", p.value("title")},
                         {"description", p.value("description")},
                         {"published_at", QDate::currentDate().toString(Qt::ISODate)},
                         {"url", p.value("url")}, {"sha256", sha}, {"size", size},
                         {"signature", QString::fromLatin1(sig.toBase64())}, {"major", p.isSet("major")}};
        if (p.isSet("base-version")) item["base_version"] = p.value("base-version");
        if (p.isSet("components")) item["components"] = QJsonArray::fromStringList(p.value("components").split(','));
        out << QJsonDocument(item).toJson();
        return 0;
    }
    if (cmd == "manifest") {
        const QByteArray body = readFile(p.value("manifest"));
        const QByteArray sig = Signature::sign(readFile(p.value("key")), body, &err);
        if (sig.isEmpty()) { errs << err << Qt::endl; return 1; }
        writeFile(p.value("manifest") + ".sig", sig.toBase64());
        out << "Wrote " << p.value("manifest") << ".sig" << Qt::endl;
        return 0;
    }
    p.showHelp(1);
}
