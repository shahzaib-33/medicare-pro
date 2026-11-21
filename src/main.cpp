#include "app/AppPaths.h"
#include "app/PerformanceProfile.h"
#include "app/Version.h"
#include "db/Database.h"
#include "net/ConnectivityMonitor.h"
#include "services/AuthService.h"
#include "services/BackupService.h"
#include "ui/AppContext.h"
#include "ui/LoginDialog.h"
#include "ui/CatalogBrowserDialog.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include "update/SoftwareUpdater.h"
#include "update/UpdateClient.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMessageBox>
#include <QSettings>
#include <QSqlQuery>
#include <QThread>
#include <QTimer>
#include <QTranslator>

using namespace mcp;

static QTranslator *g_translator = nullptr;

static void applyLanguageAndTheme(QApplication &app, const QString &lang, const QString &theme)
{
    if (g_translator) { app.removeTranslator(g_translator); delete g_translator; g_translator = nullptr; }
    if (lang == "ur") {
        g_translator = new QTranslator(&app);
        // A verified translation update installed from the update center wins over the built in one.
        QSettings s(AppPaths::settingsFile(), QSettings::IniFormat);
        const QString updated = s.value("content/translations").toString();
        if (!(QFile::exists(updated) && g_translator->load(updated)))
            (void)g_translator->load(":/i18n/medicarepro_ur.qm");
        app.installTranslator(g_translator);
    }
    Theme::apply(app, Theme::fromString(theme), lang == "ur");
}

static bool openDatabases(const PerformanceProfile &perf, QString *err)
{
    // First start: copy the preloaded reference catalog shipped by the installer.
    if (!QFile::exists(AppPaths::referenceDb()) && QFile::exists(AppPaths::bundledReferenceDb()))
        QFile::copy(AppPaths::bundledReferenceDb(), AppPaths::referenceDb());
    QFile::setPermissions(AppPaths::referenceDb(), QFile::ReadOwner | QFile::WriteOwner);
    if (!Db::open(Db::Pharmacy, AppPaths::pharmacyDb(), perf, err) || !Db::migratePharmacy(Db::get(Db::Pharmacy), err))
        return false;
    if (!Db::open(Db::Reference, AppPaths::referenceDb(), perf, err) || !Db::migrateReference(Db::get(Db::Reference), err)) {
        // A damaged reference catalog must never stop the pharmacy. Start with an empty one.
        Db::close(Db::Reference);
        QFile::rename(AppPaths::referenceDb(), AppPaths::referenceDb() + ".damaged");
        return Db::open(Db::Reference, AppPaths::referenceDb(), perf, err) && Db::migrateReference(Db::get(Db::Reference), err);
    }
    return true;
}

static UpdateConfig loadUpdateConfig()
{
    UpdateConfig c;
    QFile f(":/config/update.json");
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        c.manifestUrl = o.value("manifest_url").toString();
        for (const QJsonValue &h : o.value("allowed_hosts").toArray()) c.allowedHosts << h.toString();
    }
    QFile k(":/keys/update_public.pem");
    if (k.open(QIODevice::ReadOnly)) c.publicKeyPem = k.readAll();
    if (!c.publicKeyPem.contains("BEGIN PUBLIC KEY")) c.publicKeyPem.clear();
    QSettings s(AppPaths::settingsFile(), QSettings::IniFormat);
    c.autoCheck = s.value("updates/autoCheck", true).toBool();
    c.checkIntervalHours = s.value("updates/intervalHours", 24).toInt();
    c.autoInstallReference = s.value("updates/autoInstallReference", true).toBool();
    return c;
}

int main(int argc, char *argv[])
{
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    // Fonts are built into the exe (SIL Open Font License), so every PC shows the same clean text in English and Urdu.
    for (const QFileInfo &fi : QDir(":/fonts").entryInfoList({"*.ttf", "*.otf"}))
        QFontDatabase::addApplicationFont(fi.absoluteFilePath());
    app.setOrganizationName("SoftzenAgency");
    app.setApplicationName("MediCarePro");
    app.setApplicationVersion(appVersion());
    app.setWindowIcon(QIcon(":/icons/medicare.png"));

    // One running copy per data folder protects the database.
    QLockFile lock(AppPaths::dataRoot() + "/medicarepro.lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock(200)) {
        QMessageBox::information(nullptr, "MediCare Pro", QObject::tr("MediCare Pro is already running."));
        return 0;
    }

    QSettings settings(AppPaths::settingsFile(), QSettings::IniFormat);
    const QString modeName = settings.value("perf/mode", "auto").toString();
    const auto mode = modeName == "lite" ? PerformanceProfile::Mode::Lite
                    : modeName == "standard" ? PerformanceProfile::Mode::Standard : PerformanceProfile::Mode::Auto;
    AppContext ctx;
    ctx.perf = PerformanceProfile::detect(mode);
    if (ctx.perf.lite()) {
        // Light mode: no animated effects, which keeps older PCs responsive.
        for (auto e : {Qt::UI_AnimateMenu, Qt::UI_FadeMenu, Qt::UI_AnimateCombo, Qt::UI_AnimateTooltip,
                       Qt::UI_FadeTooltip, Qt::UI_AnimateToolBox})
            QApplication::setEffectEnabled(e, false);
    }
    applyLanguageAndTheme(app, settings.value("ui/language", "en").toString(), settings.value("ui/theme", "xp").toString());

    QString err;
    if (!openDatabases(ctx.perf, &err)) {
        QMessageBox::critical(nullptr, "MediCare Pro",
            QObject::tr("The pharmacy database could not be opened.\n%1\n\nBackups are in:\n%2")
                .arg(err, QDir::toNativeSeparators(AppPaths::backupDir())));
        return 1;
    }

    AuthService::seedRolesAndPermissions();
    // Daily automatic backup of the pharmacy data. Runs before anyone signs in.
    BackupService::autoBackupIfDue();

    ConnectivityMonitor network;
    network.setForcedOffline(settings.value("net/forceOffline", false).toBool());
    UpdateClient updates(Db::Pharmacy, loadUpdateConfig());
    ctx.network = &network;
    ctx.updates = &updates;

    const QString shots = qEnvironmentVariable("MEDICARE_SCREENSHOT");
    // Sign in. Developer screenshots sign in with normal credentials from the environment.
    auto signIn = [&]() -> bool {
        if (!shots.isEmpty())
            return AuthService::login(qEnvironmentVariable("MEDICARE_SCREENSHOT_USER"), qEnvironmentVariable("MEDICARE_SCREENSHOT_PASSWORD")).ok;
        LoginDialog dlg(AuthService::isFirstRun() ? LoginDialog::Mode::FirstRun : LoginDialog::Mode::Login);
        return dlg.exec() == QDialog::Accepted && Session::instance().loggedIn();
    };
    if (!signIn()) return 0;

    MainWindow w(&ctx);
    w.rebuild();
    QObject::connect(&w, &MainWindow::languageChangeRequested, &app, [&](const QString &lang) {
        applyLanguageAndTheme(app, lang, QSettings(AppPaths::settingsFile(), QSettings::IniFormat).value("ui/theme", "xp").toString());
        w.rebuild();
    });
    QObject::connect(&w, &MainWindow::themeChangeRequested, &app, [&](const QString &theme) {
        applyLanguageAndTheme(app, QSettings(AppPaths::settingsFile(), QSettings::IniFormat).value("ui/language", "en").toString(), theme);
        w.rebuild();   // the classic and modern themes use different window layouts
    });
    QObject::connect(&w, &MainWindow::signOutRequested, &app, [&] {
        AuthService::logout();
        w.hide();
        if (!signIn()) { app.quit(); return; }
        w.rebuild();
        w.showMaximized();
    }, Qt::QueuedConnection);
    QObject::connect(&w, &MainWindow::lockRequested, &app, [&] {
        // The window is hidden while locked so nobody can read the screen.
        w.hide();
        LoginDialog dlg(LoginDialog::Mode::Unlock);
        const bool ok = dlg.exec() == QDialog::Accepted;
        if (!ok || dlg.switchUserRequested()) {
            emit w.signOutRequested();
            return;
        }
        w.showMaximized();
        w.applyAutoLock();
    }, Qt::QueuedConnection);
    if (!shots.isEmpty()) { w.resize(1366, 768); w.show(); }
    else w.showMaximized();

    // Confirm a previous software update after the window is visible.
    QTimer::singleShot(500, &w, [&] {
        SoftwareUpdater su(Db::Pharmacy, Db::Reference);
        const auto rep = su.checkAfterStart();
        if (!rep.hadPendingUpdate) return;
        if (rep.succeeded) QMessageBox::information(&w, "MediCare Pro", rep.message);
        else QMessageBox::warning(&w, "MediCare Pro", rep.message);
    });
    // Developer option: MEDICARE_SCREENSHOT=folder saves one image per page, then exits.
    if (!shots.isEmpty()) {
        QTimer::singleShot(1500, &w, [&] {
            for (int page = 0; page < Pages::Count; ++page) {
                w.showPage(page);
                if (page == Pages::Catalog)
                    if (auto *c = w.findChild<CatalogBrowserDialog *>())
                        c->setQuery(qEnvironmentVariable("MEDICARE_SCREENSHOT_QUERY", "Panadol"));
                for (int i = 0; i < 5; ++i) { QApplication::processEvents(); QThread::msleep(60); }
                w.grab().save(QStringLiteral("%1/page%2.png").arg(shots).arg(page, 2, 10, QChar('0')));
            }
            app.quit();
        });
    }
    const int rc = app.exec();
    Db::close(Db::Reference);
    Db::close(Db::Pharmacy);
    return rc;
}
