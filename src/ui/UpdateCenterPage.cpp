#include "UpdateCenterPage.h"
#include "app/AppPaths.h"
#include "app/Version.h"
#include "db/Database.h"
#include "net/ConnectivityMonitor.h"
#include "refdata/CatalogImporter.h"
#include "refdata/CatalogStats.h"
#include "refdata/PharmacyCatalog.h"
#include "update/SoftwareUpdater.h"
#include "update/Signature.h"
#include "update/UpdateClient.h"
#include "update/UpdateLog.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSqlQuery>
#include <QTableWidget>
#include <QVBoxLayout>

namespace mcp {

static QTableWidget *makeTable(const QStringList &headers, QWidget *parent)
{
    auto *t = new QTableWidget(0, headers.size(), parent);
    t->setHorizontalHeaderLabels(headers);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setAlternatingRowColors(true);
    t->verticalHeader()->hide();
    t->horizontalHeader()->setStretchLastSection(true);
    return t;
}

UpdateCenterPage::UpdateCenterPage(AppContext *ctx, Mode mode, QWidget *parent)
    : QWidget(parent), m_ctx(ctx), m_mode(mode)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(mode == Mode::Medicine ? buildMedicine() : buildInformation());
    outer->addWidget(scroll);

    if (m_ctx->updates) {
        connect(m_ctx->updates, &UpdateClient::checkStarted, this, [this] { setStatus(tr("Checking for updates...")); });
        connect(m_ctx->updates, &UpdateClient::checkFinished, this, [this](bool ok, const QString &msg) {
            setStatus(msg);
            m_check->setEnabled(true);
            if (ok) planReferenceUpdates();
            refresh();
            if (ok && m_mode == Mode::Medicine && !m_plan.isEmpty() && m_ctx->updates->config().autoInstallReference)
                downloadAll();
        });
        connect(m_ctx->updates, &UpdateClient::downloadProgress, this, [this](qint64 r, qint64 t) {
            m_progress->setMaximum(t > 0 ? int(t / 1024) : 0);
            m_progress->setValue(int(r / 1024));
        });
        connect(m_ctx->updates, &UpdateClient::downloadFinished, this,
                [this](const UpdateItem &item, const QString &path, bool ok, const QString &msg) {
            if (item.type == "software") {
                if (m_mode != Mode::Information) return;
                setStatus(msg);
                if (ok) { m_softwareFile = path; m_softwareItem = item; }
                refresh();
                return;
            }
            if (item.type == "translations" || item.type == "receipt_templates") {
                if (m_mode != Mode::Information) return;
                if (ok) {
                    const QString dir = AppPaths::dataRoot() + "/content/" + item.type;
                    QDir().mkpath(dir);
                    const QString target = dir + "/" + item.type + "-" + item.version + ".pkg";
                    QFile::remove(target);
                    QFile::rename(path, target);
                    QSettings s(AppPaths::settingsFile(), QSettings::IniFormat);
                    s.setValue("content/" + item.type, target);
                    setStatus(tr("%1 installed. It will be used after restart.").arg(item.title));
                } else setStatus(msg);
                return;
            }
            if (m_mode != Mode::Medicine) return;
            if (!ok) { setStatus(msg); m_downloadIndex = -1; refresh(); return; }
            m_downloaded.insert(item.id, path);
            downloadNext();
        });
    }
    if (m_ctx->network)
        connect(m_ctx->network, &ConnectivityMonitor::onlineChanged, this, &UpdateCenterPage::refresh);
    connect(AppEvents::instance(), &AppEvents::referenceChanged, this, &UpdateCenterPage::refresh);
    refresh();
}

QWidget *UpdateCenterPage::buildMedicine()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(24, 20, 24, 20);
    auto *title = new QLabel(tr("Medicine Database Updates"), w);
    title->setProperty("role", "h1");
    v->addWidget(title);

    auto *card = new QFrame(w);
    card->setProperty("role", "card");
    auto *g = new QGridLayout(card);
    g->setContentsMargins(18, 16, 18, 16);
    const QList<QPair<QString, QString>> stats = {
        {"version", tr("Database version")}, {"last", tr("Last successful update")},
        {"total", tr("Total reference medicines")}, {"new", tr("Newly available medicines")},
        {"updated", tr("Updated medicines")}, {"changed", tr("Changed records")},
        {"inactive", tr("Removed or inactive registrations")}, {"available", tr("Available version")}};
    for (int i = 0; i < stats.size(); ++i) {
        auto *val = new QLabel("-", card);
        val->setProperty("role", "stat");
        auto *cap = new QLabel(stats[i].second, card);
        cap->setProperty("role", "muted");
        g->addWidget(val, (i / 4) * 2, i % 4);
        g->addWidget(cap, (i / 4) * 2 + 1, i % 4);
        m_stat.insert(stats[i].first, val);
    }
    v->addWidget(card);

    m_status = new QLabel(w);
    m_status->setWordWrap(true);
    v->addWidget(m_status);
    m_progress = new QProgressBar(w);
    m_progress->setValue(0);
    v->addWidget(m_progress);

    auto *h = new QHBoxLayout;
    m_check = new QPushButton(tr("Check for Updates"), w);
    m_download = new QPushButton(tr("Download Updates"), w);
    m_install = new QPushButton(tr("Install Updates"), w);
    m_install->setProperty("role", "primary");
    m_changes = new QPushButton(tr("View Changes"), w);
    for (auto *b : {m_check, m_download, m_install, m_changes}) h->addWidget(b);
    h->addStretch();
    v->addLayout(h);
    m_auto = new QCheckBox(tr("Update automatically (download and install reference data when available)"), w);
    v->addWidget(m_auto);
    auto *prices = new QCheckBox(tr("Use new official prices as sale prices (up or down, for all stock)"), w);
    v->addWidget(prices);
    {
        QSettings s0(AppPaths::settingsFile(), QSettings::IniFormat);
        prices->setChecked(s0.value("updates/applyPrices", true).toBool());
    }
    connect(prices, &QCheckBox::toggled, this, [](bool on) {
        QSettings(AppPaths::settingsFile(), QSettings::IniFormat).setValue("updates/applyPrices", on);
    });

    auto *admin = new QGroupBox(tr("Administrator tools"), w);
    auto *ah = new QHBoxLayout(admin);
    auto *imp = new QPushButton(tr("Import Official CSV File"), admin);
    auto *restore = new QPushButton(tr("Restore Previous Version"), admin);
    ah->addWidget(imp);
    ah->addWidget(restore);
    ah->addStretch();
    v->addWidget(admin);

    auto *ht = new QLabel(tr("Update history"), w);
    ht->setProperty("role", "h2");
    v->addWidget(ht);
    m_history = makeTable({tr("Started"), tr("Type"), tr("From"), tr("To"), tr("Status"), tr("Added"), tr("Updated"),
                           tr("Inactive"), tr("Flagged"), tr("Message")}, w);
    m_history->setMinimumHeight(220);
    v->addWidget(m_history, 1);
    m_items = nullptr;

    QSettings s(AppPaths::settingsFile(), QSettings::IniFormat);
    m_auto->setChecked(s.value("updates/autoInstallReference", true).toBool());
    connect(m_auto, &QCheckBox::toggled, this, [this](bool on) {
        QSettings st(AppPaths::settingsFile(), QSettings::IniFormat);
        st.setValue("updates/autoInstallReference", on);
        if (m_ctx->updates) { auto c = m_ctx->updates->config(); c.autoInstallReference = on; m_ctx->updates->setConfig(c); }
    });
    connect(m_check, &QPushButton::clicked, this, &UpdateCenterPage::check);
    connect(m_download, &QPushButton::clicked, this, &UpdateCenterPage::downloadAll);
    connect(m_install, &QPushButton::clicked, this, &UpdateCenterPage::installAll);
    connect(m_changes, &QPushButton::clicked, this, &UpdateCenterPage::viewChanges);
    connect(imp, &QPushButton::clicked, this, &UpdateCenterPage::importCsv);
    connect(restore, &QPushButton::clicked, this, &UpdateCenterPage::restorePrevious);
    return w;
}

QWidget *UpdateCenterPage::buildInformation()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(24, 20, 24, 20);
    auto *title = new QLabel(tr("Pharmacy Information Update Center"), w);
    title->setProperty("role", "h1");
    v->addWidget(title);
    auto *sub = new QLabel(tr("Only updates signed by SoftzenAgency are accepted. No pharmacy data is sent."), w);
    sub->setProperty("role", "muted");
    v->addWidget(sub);

    m_status = new QLabel(w);
    m_status->setWordWrap(true);
    v->addWidget(m_status);
    m_progress = new QProgressBar(w);
    v->addWidget(m_progress);
    auto *h = new QHBoxLayout;
    m_check = new QPushButton(tr("Check for Updates"), w);
    h->addWidget(m_check);
    h->addStretch();
    v->addLayout(h);
    m_download = m_install = m_changes = nullptr;
    m_auto = nullptr;

    auto *sw = new QGroupBox(tr("Software update"), w);
    auto *sf = new QFormLayout(sw);
    m_swCurrent = new QLabel(appVersion(), sw);
    m_swAvailable = new QLabel("-", sw);
    m_swNotes = new QLabel(sw);
    m_swNotes->setWordWrap(true);
    sf->addRow(tr("Installed version"), m_swCurrent);
    sf->addRow(tr("Available version"), m_swAvailable);
    sf->addRow(tr("New features and fixes"), m_swNotes);
    auto *sh = new QHBoxLayout;
    m_swDownload = new QPushButton(tr("Download Update"), sw);
    m_swInstall = new QPushButton(tr("Install Update"), sw);
    m_swInstall->setProperty("role", "primary");
    sh->addWidget(m_swDownload);
    sh->addWidget(m_swInstall);
    sh->addStretch();
    sf->addRow(sh);
    v->addWidget(sw);

    auto *it = new QLabel(tr("Available updates"), w);
    it->setProperty("role", "h2");
    v->addWidget(it);
    m_items = makeTable({tr("Type"), tr("Version"), tr("Title"), tr("Published"), tr("Components"), tr("Description")}, w);
    m_items->setMinimumHeight(180);
    v->addWidget(m_items);
    auto *dl = new QPushButton(tr("Download and Install Selected"), w);
    auto *dh = new QHBoxLayout;
    dh->addWidget(dl);
    dh->addStretch();
    v->addLayout(dh);

    auto *ht = new QLabel(tr("Software update history"), w);
    ht->setProperty("role", "h2");
    v->addWidget(ht);
    m_history = makeTable({tr("Started"), tr("From"), tr("To"), tr("Status"), tr("Backup"), tr("Message")}, w);
    m_history->setMinimumHeight(160);
    v->addWidget(m_history, 1);

    connect(m_check, &QPushButton::clicked, this, &UpdateCenterPage::check);
    connect(m_swDownload, &QPushButton::clicked, this, [this] {
        if (!m_ctx->updates || !m_ctx->updates->hasManifest()) return;
        if (const UpdateItem *i = m_ctx->updates->manifest().latest("software"); i && SoftwareUpdater::isNewer(*i)) {
            setStatus(tr("Downloading %1...").arg(i->version));
            m_ctx->updates->download(*i);
        }
    });
    connect(m_swInstall, &QPushButton::clicked, this, &UpdateCenterPage::installSoftware);
    connect(dl, &QPushButton::clicked, this, [this] {
        const int r = m_items->currentRow();
        if (r < 0 || !m_ctx->updates) return;
        const QString id = m_items->item(r, 0)->data(Qt::UserRole).toString();
        for (const auto &i : m_ctx->updates->manifest().items) {
            if (i.id != id) continue;
            if (i.type.startsWith("reference")) {
                setStatus(tr("Use the Medicine Database Updates page for medicine data."));
                AppEvents::instance()->navigate(Pages::MedicineUpdates);
            } else if (i.type == "software") {
                m_swDownload->click();
            } else {
                m_ctx->updates->download(i);
            }
        }
    });
    return w;
}

bool UpdateCenterPage::updatesConfigured() const
{
    return m_ctx->updates && !m_ctx->updates->config().manifestUrl.isEmpty()
        && !m_ctx->updates->config().publicKeyPem.trimmed().isEmpty();
}

void UpdateCenterPage::setStatus(const QString &s) { m_status->setText(s); }

void UpdateCenterPage::check()
{
    if (!updatesConfigured()) {
        setStatus(tr("The SoftzenAgency update server is not configured in this build. You can still import an official CSV file."));
        return;
    }
    if (m_ctx->network && !m_ctx->network->isOnline()) {
        setStatus(tr("No internet connection. Everything else keeps working offline."));
        return;
    }
    m_check->setEnabled(false);
    m_ctx->updates->checkNow();
}

void UpdateCenterPage::planReferenceUpdates()
{
    m_plan.clear();
    m_packages.clear();
    m_packageSha.clear();
    m_downloaded.clear();
    if (!m_ctx->updates || !m_ctx->updates->hasManifest()) return;
    const auto &items = m_ctx->updates->manifest().items;
    int cur = CatalogImporter::currentVersion(Db::get(Db::Reference));
    // Prefer small deltas. Fall back to the newest full package when the chain is broken.
    QVector<UpdateItem> chain;
    int v = cur;
    bool progressed = true;
    while (progressed) {
        progressed = false;
        for (const auto &i : items) {
            if (i.type == "reference_delta" && i.baseVersion.toInt() == v && i.version.toInt() > v) {
                chain.append(i);
                v = i.version.toInt();
                progressed = true;
                break;
            }
        }
    }
    const UpdateItem *full = m_ctx->updates->manifest().latest("reference_full");
    if (full && full->version.toInt() > v) { chain.clear(); chain.append(*full); }
    m_plan = chain;
}

void UpdateCenterPage::downloadAll()
{
    if (m_plan.isEmpty()) { setStatus(tr("Your medicine database is up to date.")); return; }
    m_downloaded.clear();
    m_downloadIndex = 0;
    downloadNext();
}

void UpdateCenterPage::downloadNext()
{
    if (m_downloadIndex < 0) return;
    while (m_downloadIndex < m_plan.size() && m_downloaded.contains(m_plan[m_downloadIndex].id)) ++m_downloadIndex;
    if (m_downloadIndex >= m_plan.size()) {
        m_downloadIndex = -1;
        parseDownloaded();
        if (m_auto && m_auto->isChecked() && !m_packages.isEmpty()) installAll();
        return;
    }
    const UpdateItem &i = m_plan[m_downloadIndex];
    setStatus(tr("Downloading update %1 of %2...").arg(m_downloadIndex + 1).arg(m_plan.size()));
    m_ctx->updates->download(i);
}

void UpdateCenterPage::parseDownloaded()
{
    m_packages.clear();
    m_packageSha.clear();
    int added = 0, updated = 0, inactive = 0;
    for (const auto &item : m_plan) {
        const QString path = m_downloaded.value(item.id);
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) { setStatus(tr("Downloaded file is missing.")); return; }
        ReferencePackage p;
        QStringList problems;
        if (!ReferenceUpdater::parsePackage(f.readAll(), &p, &problems)) {
            UpdateLog::security(Db::get(Db::Pharmacy), "package_invalid", item.id + ": " + problems.join("; ").left(500));
            setStatus(tr("The update package failed validation and was not installed."));
            return;
        }
        m_packages.append(p);
        m_packageSha.append(item.sha256);
        added += p.upserts.size();
        inactive += p.statusChanges.size();
    }
    if (!m_packages.isEmpty()) {
        const auto pv = ReferenceUpdater(Db::Reference, Db::Pharmacy).preview(m_packages.first());
        added = pv.added; updated = pv.updated; inactive = pv.deactivated;
    }
    m_stat["new"]->setText(QString::number(added));
    m_stat["updated"]->setText(QString::number(updated));
    m_stat["changed"]->setText(QString::number(added + updated + inactive));
    m_stat["inactive"]->setText(QString::number(inactive));
    setStatus(tr("Updates downloaded and verified. Ready to install."));
    refresh();
}

void UpdateCenterPage::installAll()
{
    if (m_packages.isEmpty()) { setStatus(tr("Download the updates first.")); return; }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    ReferenceUpdater up(Db::Reference, Db::Pharmacy);
    up.setApplyPrices(QSettings(AppPaths::settingsFile(), QSettings::IniFormat).value("updates/applyPrices", true).toBool());
    QString summary;
    int prices = 0;
    for (int i = 0; i < m_packages.size(); ++i) {
        setStatus(tr("Installing update %1 of %2. A safety backup is made first.").arg(i + 1).arg(m_packages.size()));
        m_progress->setMaximum(m_packages.size());
        m_progress->setValue(i);
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        const auto res = up.install(m_packages[i], m_packageSha[i]);
        if (!res.ok) {
            summary = tr("Update failed and the previous medicine database was kept. %1").arg(res.error);
            break;
        }
        prices += res.pricesChanged;
        summary = tr("Installed. %1 new, %2 updated, %3 inactive. %4 of your medicines need review.")
                      .arg(res.added).arg(res.updated).arg(res.deactivated).arg(res.flagged);
        if (prices > 0) summary += ' ' + tr("%n sale price(s) updated.", nullptr, prices);
    }
    m_progress->setValue(m_progress->maximum());
    QApplication::restoreOverrideCursor();
    for (const QString &p : m_downloaded) QFile::remove(p);
    m_packages.clear();
    m_downloaded.clear();
    planReferenceUpdates();
    setStatus(summary);
    emit AppEvents::instance()->referenceChanged();
    emit AppEvents::instance()->pharmacyChanged();
}

void UpdateCenterPage::viewChanges()
{
    if (m_packages.isEmpty()) { setStatus(tr("Download the updates first to see the changes.")); return; }
    const auto pv = ReferenceUpdater(Db::Reference, Db::Pharmacy).preview(m_packages.first());
    QDialog d(this);
    d.setWindowTitle(tr("Changes in this update"));
    d.resize(640, 520);
    auto *v = new QVBoxLayout(&d);
    auto *txt = new QPlainTextEdit(&d);
    txt->setReadOnly(true);
    QString s = tr("New medicines: %1").arg(pv.added) + "\n" + pv.newBrands.join("\n") + "\n\n"
        + tr("Updated medicines: %1").arg(pv.updated) + "\n" + pv.changedBrands.join("\n") + "\n\n"
        + tr("Inactive or cancelled registrations: %1").arg(pv.deactivated) + "\n" + pv.inactiveBrands.join("\n");
    txt->setPlainText(s);
    v->addWidget(txt);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, &d);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    v->addWidget(bb);
    d.exec();
}

void UpdateCenterPage::importCsv()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Select official CSV file"), {},
                                                      tr("CSV files (*.csv *.txt)"));
    if (path.isEmpty()) return;
    QDialog d(this);
    d.setWindowTitle(tr("Import official medicine data"));
    auto *f = new QFormLayout(&d);
    auto *code = new QLineEdit(QFileInfo(path).baseName().toLower(), &d);
    auto *name = new QLineEdit(&d);
    name->setPlaceholderText(tr("For example: DRAP registered products list received on 2026-11-20"));
    auto *url = new QLineEdit(&d);
    auto *method = new QLineEdit(&d);
    method->setPlaceholderText(tr("How the file was obtained"));
    auto *coverage = new QLineEdit(&d);
    auto *verified = new QCheckBox(tr("I confirm this is an official dataset and the records are verified"), &d);
    f->addRow(tr("Source code"), code);
    f->addRow(tr("Source name"), name);
    f->addRow(tr("Source address"), url);
    f->addRow(tr("Access method"), method);
    f->addRow(tr("Coverage note"), coverage);
    f->addRow(verified);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    f->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted) return;
    if (name->text().trimmed().isEmpty()) { setStatus(tr("A source name is required.")); return; }

    ImportOptions opt;
    opt.source = {code->text().trimmed(), name->text().trimmed(), url->text().trimmed(), method->text().trimmed(), {},
                  QDate::currentDate().toString(Qt::ISODate)};
    opt.markVerified = verified->isChecked();
    opt.coverageNote = coverage->text().trimmed();
    opt.batchSize = m_ctx->perf.importBatchSize();

    // Validate first, then import for real.
    opt.dryRun = true;
    auto check = CatalogImporter::importCsv(Db::get(Db::Reference), path, opt);
    if (!check.ok) { setStatus(check.error); return; }
    if (QMessageBox::question(this, tr("Import"),
            tr("%1 rows read. %2 valid, %3 rejected, %4 duplicates. Import the valid rows now?")
                .arg(check.rowsRead).arg(check.added).arg(check.skipped).arg(check.duplicates)) != QMessageBox::Yes)
        return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const qint64 hist = UpdateLog::beginHistory(Db::get(Db::Pharmacy), "csv_import",
                                                CatalogImporter::currentVersion(Db::get(Db::Reference)), 0, {});
    opt.dryRun = false;
    const auto rep = CatalogImporter::importCsv(Db::get(Db::Reference), path, opt);
    int flagged = 0;
    if (rep.ok) flagged = PharmacyCatalog::flagChangedProducts(Db::get(Db::Pharmacy), Db::get(Db::Reference), rep.newVersion);
    QSqlQuery hq(Db::get(Db::Pharmacy));
    hq.prepare("UPDATE medicine_update_history SET to_version=?, backup_path=? WHERE id=?");
    hq.addBindValue(rep.newVersion); hq.addBindValue(rep.backupPath); hq.addBindValue(hist);
    hq.exec();
    for (const auto &i : rep.issues) UpdateLog::line(Db::get(Db::Pharmacy), hist, "warning", QStringLiteral("Line %1: %2").arg(i.line).arg(i.message));
    UpdateLog::finishHistory(Db::get(Db::Pharmacy), hist, rep.ok ? "installed" : "failed", rep.added, rep.updated, 0,
                             rep.skipped, flagged, rep.ok ? QFileInfo(path).fileName() : rep.error);
    QApplication::restoreOverrideCursor();
    setStatus(rep.ok ? tr("Import finished. %1 added, %2 updated, %3 unchanged, %4 rejected.")
                           .arg(rep.added).arg(rep.updated).arg(rep.unchanged).arg(rep.skipped)
                     : rep.error);
    emit AppEvents::instance()->referenceChanged();
}

void UpdateCenterPage::restorePrevious()
{
    ReferenceUpdater up(Db::Reference, Db::Pharmacy);
    const QStringList list = up.backups();
    if (list.isEmpty()) { setStatus(tr("No reference database backups found.")); return; }
    QStringList names;
    for (const QString &p : list) names << QFileInfo(p).fileName();
    bool ok = false;
    const QString pick = QInputDialog::getItem(this, tr("Restore Previous Version"),
                                               tr("Choose a backup. Your pharmacy stock, prices and sales are not affected."),
                                               names, 0, false, &ok);
    if (!ok) return;
    QString err;
    if (up.restoreBackup(list[names.indexOf(pick)], &err)) {
        setStatus(tr("Previous medicine database restored."));
        emit AppEvents::instance()->referenceChanged();
    } else setStatus(err);
}

void UpdateCenterPage::installSoftware()
{
    if (m_softwareFile.isEmpty()) { setStatus(tr("Download the update first.")); return; }
    QString err;
    // Check the file again right before running it.
    if (!UpdateClient::verifyPackageFile(m_softwareItem, m_softwareFile, m_ctx->updates->config().publicKeyPem, &err)) {
        UpdateLog::security(Db::get(Db::Pharmacy), "installer_rejected", err);
        setStatus(tr("The installer failed verification and will not run."));
        return;
    }
    QString msg = tr("MediCare Pro will back up your data, close, install version %1 and start again.").arg(m_softwareItem.version);
    if (m_softwareItem.major) msg += "\n\n" + tr("This is a major update. Please make sure no sale is in progress.");
    if (QMessageBox::question(this, tr("Install Update"), msg) != QMessageBox::Yes) return;
    SoftwareUpdater su(Db::Pharmacy, Db::Reference);
    if (su.prepare(m_softwareItem, m_softwareFile, &err).isEmpty() || !su.launchInstaller(m_softwareFile, &err)) {
        setStatus(tr("Update could not start: %1").arg(err));
        return;
    }
    qApp->quit();
}

void UpdateCenterPage::loadHistory()
{
    m_history->setRowCount(0);
    QSqlQuery q(Db::get(Db::Pharmacy));
    if (m_mode == Mode::Medicine)
        q.exec("SELECT started_at, kind, from_version, to_version, status, added, updated, deactivated, flagged, message "
               "FROM medicine_update_history ORDER BY id DESC LIMIT 200");
    else
        q.exec("SELECT started_at, from_version, to_version, status, backup_path, message FROM software_update_history "
               "ORDER BY id DESC LIMIT 200");
    while (q.next()) {
        const int r = m_history->rowCount();
        m_history->insertRow(r);
        for (int c = 0; c < m_history->columnCount(); ++c) {
            QString val = q.value(c).toString();
            if (c == 0) val = QDateTime::fromString(val, Qt::ISODate).toLocalTime().toString("yyyy-MM-dd hh:mm");
            m_history->setItem(r, c, new QTableWidgetItem(val));
        }
    }
}

void UpdateCenterPage::refresh()
{
    const bool online = m_ctx->network && m_ctx->network->isOnline();
    const bool configured = updatesConfigured();
    m_check->setEnabled(online && configured);
    if (!configured && m_status->text().isEmpty())
        setStatus(tr("The SoftzenAgency update server is not configured in this build. Updates can be installed from an official CSV file."));
    else if (!online && m_status->text().isEmpty())
        setStatus(tr("Offline. Everything else keeps working. Updates resume when the internet is back."));

    if (m_mode == Mode::Medicine) {
        const auto s = CatalogStats::load(Db::get(Db::Reference));
        m_stat["version"]->setText(s.version ? QString::number(s.version) : tr("None"));
        const QDateTime last = QDateTime::fromString(s.lastUpdate, Qt::ISODate);
        m_stat["last"]->setText(last.isValid() ? last.toLocalTime().toString("yyyy-MM-dd") : tr("Never"));
        m_stat["total"]->setText(QLocale().toString(s.totalMedicines));
        QString avail = "-";
        if (!m_plan.isEmpty()) avail = m_plan.last().version;
        else if (m_ctx->updates && m_ctx->updates->hasManifest()) avail = tr("Up to date");
        m_stat["available"]->setText(avail);
        m_download->setEnabled(online && !m_plan.isEmpty() && m_downloadIndex < 0);
        m_install->setEnabled(!m_packages.isEmpty());
        m_changes->setEnabled(!m_packages.isEmpty());
    } else {
        const UpdateItem *sw = (m_ctx->updates && m_ctx->updates->hasManifest()) ? m_ctx->updates->manifest().latest("software") : nullptr;
        const bool newer = sw && SoftwareUpdater::isNewer(*sw);
        m_swAvailable->setText(sw ? sw->version + (newer ? "" : " " + tr("(installed)")) : tr("No information yet"));
        m_swNotes->setText(newer ? (sw->description + "\n- " + sw->notes.join("\n- ")) : tr("You have the latest version."));
        m_swDownload->setEnabled(online && newer);
        m_swInstall->setEnabled(newer && !m_softwareFile.isEmpty());
        m_items->setRowCount(0);
        if (m_ctx->updates && m_ctx->updates->hasManifest()) {
            for (const auto &i : m_ctx->updates->manifest().items) {
                const int r = m_items->rowCount();
                m_items->insertRow(r);
                const QStringList cells = {i.type, i.version, i.title, i.publishedAt, i.components.join(", "), i.description};
                for (int c = 0; c < cells.size(); ++c) m_items->setItem(r, c, new QTableWidgetItem(cells[c]));
                m_items->item(r, 0)->setData(Qt::UserRole, i.id);
            }
        }
    }
    loadHistory();
}
}
