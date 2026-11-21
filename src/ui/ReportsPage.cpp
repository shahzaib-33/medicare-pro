#include "ReportsPage.h"
#include "Ui.h"
#include "services/SettingsService.h"
#include <QHeaderView>
#include <QListWidget>
#include <QSplitter>
#include <QTableWidget>

namespace mcp {

ReportsPage::ReportsPage(AppContext *, QWidget *parent) : QWidget(parent)
{
    auto f = ui::page(this, tr("Reports"));
    auto *print = ui::button(tr("Print preview"), this);
    auto *pdf = ui::button(tr("Save PDF"), this);
    auto *csv = ui::button(tr("Export CSV"), this, true);
    for (auto *b : {print, pdf, csv}) f.actions->addWidget(b);
    m_range = new ui::DateRangeBar(this, 3);
    f.body->addWidget(m_range);
    auto *split = new QSplitter(Qt::Horizontal, this);
    m_list = new QListWidget(split);
    m_list->setMaximumWidth(300);
    m_list->setWordWrap(true);
    for (const ReportDef &d : ReportService::available()) {
        auto *it = new QListWidgetItem(d.title, m_list);
        it->setData(Qt::UserRole, d.code);
    }
    auto *right = new QWidget(split);
    auto *rv = new QVBoxLayout(right);
    rv->setContentsMargins(0, 0, 0, 0);
    m_summary = ui::muted({}, right);
    m_table = new QTableWidget(right);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setStretchLastSection(true);
    rv->addWidget(m_summary);
    rv->addWidget(m_table, 1);
    split->addWidget(m_list);
    split->addWidget(right);
    split->setStretchFactor(1, 1);
    f.body->addWidget(split, 1);
    connect(m_list, &QListWidget::currentRowChanged, this, &ReportsPage::run);
    connect(m_range, &ui::DateRangeBar::changed, this, &ReportsPage::run);
    auto html = [this] {
        const ReportDef *def = nullptr;
        for (const auto &d : ReportService::available()) if (d.code == m_current.code) def = &d;
        return m_current.toHtml(SettingsService::profile().name, def && def->usesDates ? m_range->label() : tr("As of %1").arg(QDate::currentDate().toString("dd-MM-yyyy")));
    };
    connect(print, &QPushButton::clicked, this, [=, this] { if (!m_current.code.isEmpty()) ui::printHtml(this, html(), 0, true); });
    connect(pdf, &QPushButton::clicked, this, [=, this] { if (!m_current.code.isEmpty()) ui::saveHtmlAsPdf(this, html(), 0, m_current.code + ".pdf"); });
    connect(csv, &QPushButton::clicked, this, [this] {
        if (!m_current.code.isEmpty()) ui::saveText(this, m_current.toCsv(), m_current.code + ".csv", tr("CSV files (*.csv)"));
    });
    if (m_list->count()) m_list->setCurrentRow(0);
    else m_summary->setText(tr("You do not have permission to view reports."));
}

bool ReportsPage::selectReport(const QString &code)
{
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(Qt::UserRole).toString() == code) { m_list->setCurrentRow(i); return true; }
    }
    return false;
}

void ReportsPage::run()
{
    auto *it = m_list->currentItem();
    if (!it) return;
    m_current = ReportService::run(it->data(Qt::UserRole).toString(), m_range->from(), m_range->to());
    m_summary->setText(m_current.summary.join('\n'));
    m_summary->setVisible(!m_current.summary.isEmpty());
    const QVariantList totals = m_current.totals();
    m_table->clear();
    m_table->setColumnCount(m_current.headers.size());
    m_table->setHorizontalHeaderLabels(m_current.headers);
    m_table->setRowCount(m_current.rows.size() + (totals.isEmpty() ? 0 : 1));
    for (int r = 0; r < m_current.rows.size(); ++r)
        for (int c = 0; c < m_current.headers.size(); ++c) {
            auto *cell = new QTableWidgetItem(m_current.cell(r, c));
            if (m_current.moneyColumns.contains(c)) cell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_table->setItem(r, c, cell);
        }
    if (!totals.isEmpty()) {
        const int r = m_current.rows.size();
        QFont bold = m_table->font();
        bold.setBold(true);
        for (int c = 0; c < totals.size(); ++c) {
            const bool money = m_current.moneyColumns.contains(c) && m_current.totalColumns.contains(c);
            auto *cell = new QTableWidgetItem(money ? Money::format(totals[c].toLongLong(), false) : totals[c].toString());
            cell->setFont(bold);
            if (m_current.moneyColumns.contains(c)) cell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_table->setItem(r, c, cell);
        }
    }
    m_table->resizeColumnsToContents();
}
}
