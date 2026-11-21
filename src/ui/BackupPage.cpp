#include "BackupPage.h"
#include "Ui.h"
#include "app/AppPaths.h"
#include "services/BackupService.h"
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QHeaderView>
#include <QTableWidget>
#include <QUrl>

namespace mcp {

BackupPage::BackupPage(AppContext *, QWidget *parent) : QWidget(parent)
{
    auto f = ui::page(this, tr("Backup and restore"));
    auto *now = ui::button(tr("Back up now"), this, true);
    auto *toFolder = ui::button(tr("Back up to USB or folder"), this);
    auto *verify = ui::button(tr("Verify"), this);
    auto *restoreSel = ui::button(tr("Restore selected"), this);
    auto *restoreFile = ui::button(tr("Restore from file"), this);
    auto *openDir = ui::button(tr("Open backup folder"), this);
    const bool allowed = ui::can("backup.manage");
    for (auto *b : {now, toFolder, restoreSel, restoreFile}) b->setEnabled(allowed);
    for (auto *b : {openDir, verify, restoreFile, restoreSel, toFolder, now}) f.actions->addWidget(b);
    f.body->addWidget(ui::muted(tr("A backup is made automatically every day when MediCare Pro starts, and before every restore and update. "
                                   "Keep a copy on a USB drive or another PC. Backups contain only your pharmacy data. Nothing is uploaded."), this));
    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({tr("Date"), tr("Type"), tr("Size"), tr("File")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    f.body->addWidget(m_table, 1);
    auto selected = [this] { const int r = m_table->currentRow(); return r >= 0 ? m_table->item(r, 3)->text() : QString(); };
    auto restore = [this](const QString &path) {
        if (path.isEmpty()) return;
        if (!ui::confirm(this, tr("Restore this backup? Current data is replaced. A safety copy of the current data is made first.\n\n%1")
                                   .arg(QDir::toNativeSeparators(path))))
            return;
        if (ui::check(this, BackupService::restore(path))) {
            ui::info(this, tr("Backup restored. Please sign in again."));
            emit restored();
        }
        reload();
    };
    connect(now, &QPushButton::clicked, this, [this] {
        QString path;
        if (ui::check(this, BackupService::create("manual", {}, &path))) ui::info(this, tr("Backup saved:\n%1").arg(QDir::toNativeSeparators(path)));
        reload();
    });
    connect(toFolder, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose a USB drive or folder"));
        if (dir.isEmpty()) return;
        QString path;
        if (ui::check(this, BackupService::create("manual", dir, &path))) ui::info(this, tr("Backup saved:\n%1").arg(QDir::toNativeSeparators(path)));
    });
    connect(verify, &QPushButton::clicked, this, [this, selected] {
        const QString p = selected();
        if (p.isEmpty()) return;
        if (ui::check(this, BackupService::verify(p))) ui::info(this, tr("This backup is complete and readable."));
    });
    connect(restoreSel, &QPushButton::clicked, this, [=] { restore(selected()); });
    connect(restoreFile, &QPushButton::clicked, this, [=, this] {
        restore(QFileDialog::getOpenFileName(this, tr("Choose a backup file"), {}, tr("MediCare Pro backups (*.mcbak)")));
    });
    connect(openDir, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::backupDir())); });
    reload();
}

void BackupPage::reload()
{
    const auto list = BackupService::list();
    m_table->setRowCount(list.size());
    for (int r = 0; r < list.size(); ++r) {
        const auto &b = list[r];
        m_table->setItem(r, 0, new QTableWidgetItem(b.createdAt));
        m_table->setItem(r, 1, new QTableWidgetItem(b.kind));
        m_table->setItem(r, 2, new QTableWidgetItem(QLocale().formattedDataSize(b.size)));
        m_table->setItem(r, 3, new QTableWidgetItem(b.path));
    }
    m_table->resizeColumnsToContents();
}
}
