#include "SettingsPage.h"
#include "app/AppPaths.h"
#include "net/ConnectivityMonitor.h"
#include "update/UpdateClient.h"
#include "LoginDialog.h"
#include "Ui.h"
#include "services/AuthService.h"
#include "services/SettingsService.h"
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace mcp {

SettingsPage::SettingsPage(AppContext *ctx, QWidget *parent) : QWidget(parent)
{
    QSettings s(AppPaths::settingsFile(), QSettings::IniFormat);
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(24, 20, 24, 20);
    auto *title = new QLabel(tr("Settings"), this);
    title->setProperty("role", "h1");
    v->addWidget(title);

    auto *shop = new QGroupBox(tr("Pharmacy and receipts"), this);
    auto *sf = new QFormLayout(shop);
    const PharmacyProfile prof = SettingsService::profile();
    auto *pName = new QLineEdit(prof.name, shop);
    auto *pNameUr = new QLineEdit(prof.nameUr, shop);
    auto *pAddr = new QLineEdit(prof.address, shop);
    auto *pPhone = new QLineEdit(prof.phone, shop);
    auto *pLic = new QLineEdit(prof.licenseNo, shop);
    auto *pNtn = new QLineEdit(prof.ntn, shop);
    auto *pFooter = new QLineEdit(prof.receiptFooter, shop);
    auto *pFooterUr = new QLineEdit(prof.receiptFooterUr, shop);
    auto *pWidth = new QComboBox(shop);
    pWidth->addItem(tr("58 mm roll"), "58");
    pWidth->addItem(tr("80 mm roll"), "80");
    pWidth->addItem(tr("A4 page"), "A4");
    pWidth->setCurrentIndex(qMax(0, pWidth->findData(prof.receiptWidth)));
    auto *pLang = new QComboBox(shop);
    pLang->addItem("English", "en");
    pLang->addItem(QString::fromUtf8("اردو"), "ur");
    pLang->setCurrentIndex(qMax(0, pLang->findData(prof.receiptLanguage)));
    auto *pPrint = new QCheckBox(tr("Print receipt automatically after each sale"), shop);
    pPrint->setChecked(prof.printAfterSale);
    auto *pLock = new QSpinBox(shop);
    pLock->setRange(0, 240);
    pLock->setSuffix(tr(" minutes"));
    pLock->setSpecialValueText(tr("Off"));
    pLock->setValue(prof.autoLockMinutes);
    sf->addRow(tr("Pharmacy name"), pName);
    sf->addRow(tr("Pharmacy name in Urdu"), pNameUr);
    sf->addRow(tr("Address"), pAddr);
    sf->addRow(tr("Phone"), pPhone);
    sf->addRow(tr("Drug sale license number"), pLic);
    sf->addRow(tr("NTN"), pNtn);
    sf->addRow(tr("Receipt footer"), pFooter);
    sf->addRow(tr("Receipt footer in Urdu"), pFooterUr);
    sf->addRow(tr("Receipt paper"), pWidth);
    sf->addRow(tr("Receipt language"), pLang);
    sf->addRow(QString(), pPrint);
    sf->addRow(tr("Lock screen when idle for"), pLock);
    auto *saveShop = new QPushButton(tr("Save pharmacy settings"), shop);
    saveShop->setProperty("role", "primary");
    saveShop->setEnabled(Session::instance().can("settings.edit"));
    auto *changePw = new QPushButton(tr("Change my password"), shop);
    auto *shopButtons = new QHBoxLayout;
    shopButtons->addWidget(changePw);
    shopButtons->addStretch();
    shopButtons->addWidget(saveShop);
    sf->addRow(shopButtons);
    v->addWidget(shop);
    connect(saveShop, &QPushButton::clicked, this, [=, this] {
        PharmacyProfile p = SettingsService::profile();
        p.name = pName->text(); p.nameUr = pNameUr->text(); p.address = pAddr->text(); p.phone = pPhone->text();
        p.licenseNo = pLic->text(); p.ntn = pNtn->text(); p.receiptFooter = pFooter->text(); p.receiptFooterUr = pFooterUr->text();
        p.receiptWidth = pWidth->currentData().toString(); p.receiptLanguage = pLang->currentData().toString();
        p.printAfterSale = pPrint->isChecked(); p.autoLockMinutes = pLock->value();
        if (ui::check(this, SettingsService::saveProfile(p))) {
            ui::info(this, tr("Saved."));
            emit profileChanged();
        }
    });
    connect(changePw, &QPushButton::clicked, this, [this] {
        if (ChangePasswordDialog(false, this).exec() == QDialog::Accepted) ui::info(this, tr("Password changed."));
    });

    auto *look = new QGroupBox(tr("Language and appearance"), this);
    auto *lf = new QFormLayout(look);
    auto *lang = new QComboBox(look);
    lang->addItem("English", "en");
    lang->addItem(QString::fromUtf8("اردو"), "ur");
    lang->setCurrentIndex(lang->findData(s.value("ui/language", "en")));
    auto *theme = new QComboBox(look);
    theme->addItem(tr("Windows 2003"), "xp");
    theme->addItem(tr("Classic (Windows 98)"), "classic");
    theme->addItem(tr("Light"), "light");
    theme->addItem(tr("Dark"), "dark");
    theme->setCurrentIndex(theme->findData(s.value("ui/theme", "xp")));
    lf->addRow(tr("Language"), lang);
    lf->addRow(tr("Theme"), theme);
    v->addWidget(look);

    auto *perf = new QGroupBox(tr("Performance"), this);
    auto *pf = new QFormLayout(perf);
    auto *mode = new QComboBox(perf);
    mode->addItem(tr("Automatic (recommended)"), "auto");
    mode->addItem(tr("Light mode for older or low end PCs"), "lite");
    mode->addItem(tr("Standard mode"), "standard");
    mode->setCurrentIndex(mode->findData(s.value("perf/mode", "auto")));
    auto *hw = new QLabel(ctx->perf.describe(), perf);
    hw->setProperty("role", "muted");
    auto *note = new QLabel(tr("Changes apply after restart."), perf);
    note->setProperty("role", "muted");
    pf->addRow(tr("Mode"), mode);
    pf->addRow(tr("This PC"), hw);
    pf->addRow(QString(), note);
    v->addWidget(perf);

    auto *upd = new QGroupBox(tr("Online updates"), this);
    auto *uf = new QFormLayout(upd);
    auto *autoCheck = new QCheckBox(tr("Check for updates automatically"), upd);
    autoCheck->setChecked(s.value("updates/autoCheck", true).toBool());
    auto *hours = new QSpinBox(upd);
    hours->setRange(1, 24 * 30);
    hours->setSuffix(tr(" hours"));
    hours->setValue(s.value("updates/intervalHours", 24).toInt());
    auto *offline = new QCheckBox(tr("Work offline (never use the internet)"), upd);
    offline->setChecked(s.value("net/forceOffline", false).toBool());
    auto *server = new QLabel(ctx->updates && !ctx->updates->config().manifestUrl.isEmpty()
                                  ? ctx->updates->config().manifestUrl : tr("Not configured"), upd);
    server->setProperty("role", "muted");
    uf->addRow(autoCheck);
    uf->addRow(tr("Check every"), hours);
    uf->addRow(offline);
    uf->addRow(tr("Update server"), server);
    v->addWidget(upd);
    v->addStretch();

    connect(lang, &QComboBox::currentIndexChanged, this, [this, lang] {
        QSettings st(AppPaths::settingsFile(), QSettings::IniFormat);
        st.setValue("ui/language", lang->currentData());
        emit languageChanged(lang->currentData().toString());
    });
    connect(theme, &QComboBox::currentIndexChanged, this, [this, theme] {
        QSettings st(AppPaths::settingsFile(), QSettings::IniFormat);
        st.setValue("ui/theme", theme->currentData());
        emit themeChanged(theme->currentData().toString());
    });
    connect(mode, &QComboBox::currentIndexChanged, this, [mode] {
        QSettings st(AppPaths::settingsFile(), QSettings::IniFormat);
        st.setValue("perf/mode", mode->currentData());
    });
    auto saveUpdates = [ctx, autoCheck, hours] {
        QSettings st(AppPaths::settingsFile(), QSettings::IniFormat);
        st.setValue("updates/autoCheck", autoCheck->isChecked());
        st.setValue("updates/intervalHours", hours->value());
        if (ctx->updates) {
            auto c = ctx->updates->config();
            c.autoCheck = autoCheck->isChecked();
            c.checkIntervalHours = hours->value();
            ctx->updates->setConfig(c);
        }
    };
    connect(autoCheck, &QCheckBox::toggled, this, saveUpdates);
    connect(hours, &QSpinBox::valueChanged, this, saveUpdates);
    connect(offline, &QCheckBox::toggled, this, [ctx](bool on) {
        QSettings st(AppPaths::settingsFile(), QSettings::IniFormat);
        st.setValue("net/forceOffline", on);
        if (ctx->network) ctx->network->setForcedOffline(on);
    });
}
}
