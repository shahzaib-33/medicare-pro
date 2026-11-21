#include "InventoryPage.h"
#include "Ui.h"
#include "services/InventoryService.h"
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QTabWidget>

namespace mcp {

InventoryPage::InventoryPage(AppContext *, QWidget *parent) : QWidget(parent)
{
    auto f = ui::page(this, tr("Inventory and expiry"));
    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildBatches(), tr("Batches and expiry"));
    tabs->addTab(buildInspections(), tr("Returned stock to inspect"));
    tabs->addTab(buildCounts(), tr("Stock count"));
    tabs->addTab(buildMovements(), tr("Stock movements"));
    f.body->addWidget(tabs, 1);
    connect(AppEvents::instance(), &AppEvents::pharmacyChanged, this, &InventoryPage::reload);
    reload();
}

QWidget *InventoryPage::buildBatches()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    auto *cards = new QHBoxLayout;
    const QStringList captions = {tr("Expired"), tr("Within 7 days"), tr("Within 30 days"), tr("Within 60 days"), tr("Within 90 days")};
    for (int i = 0; i < 5; ++i) {
        auto *card = new QFrame(w);
        card->setProperty("role", "card");
        auto *cv = new QVBoxLayout(card);
        m_alert[i] = new QLabel("0", card);
        m_alert[i]->setProperty("role", "stat");
        if (i < 2) m_alert[i]->setStyleSheet("color:#b42318");
        cv->addWidget(m_alert[i]);
        cv->addWidget(ui::muted(captions[i], card));
        cards->addWidget(card);
    }
    v->addLayout(cards);
    auto *bar = new QHBoxLayout;
    m_window = new QComboBox(w);
    m_window->addItem(tr("All batches with stock"), -1);
    m_window->addItem(tr("Expired"), 0);
    for (int d : {7, 30, 60, 90}) m_window->addItem(tr("Expiring within %1 days").arg(d), d);
    m_window->addItem(tr("Recalled batches"), -2);
    bar->addWidget(m_window);
    bar->addStretch();
    auto *adj = ui::button(tr("Adjust stock"), w, true);
    auto *recall = ui::button(tr("Recall or reactivate"), w);
    auto *csv = ui::button(tr("Export CSV"), w);
    adj->setEnabled(ui::can("inventory.adjust"));
    recall->setEnabled(ui::can("inventory.adjust"));
    bar->addWidget(adj);
    bar->addWidget(recall);
    bar->addWidget(csv);
    v->addLayout(bar);
    m_batches = new ui::SqlTable(w);
    m_batches->format()->money = {10};
    m_batches->format()->numbers = {5, 6, 7, 8};
    m_batches->format()->rowColor = [this](const QModelIndex &i) {
        const int days = m_batches->sqlModel()->data(m_batches->sqlModel()->index(i.row(), 5)).toInt();
        return days <= 0 ? QColor("#b42318") : days <= 30 ? QColor("#b54708") : QColor();
    };
    v->addWidget(m_batches, 1);
    connect(m_window, &QComboBox::currentIndexChanged, this, &InventoryPage::reload);
    connect(adj, &QPushButton::clicked, this, &InventoryPage::adjust);
    connect(recall, &QPushButton::clicked, this, [this] {
        const qint64 id = m_batches->currentId();
        if (!id) return;
        const bool recalled = m_batches->currentValue(9).toString() == "recalled";
        bool ok = false;
        const QString note = ui::askText(this, tr("Batch status"), recalled ? tr("Reason to reactivate:") : tr("Recall reason:"), &ok);
        if (ok && ui::check(this, InventoryService::setBatchStatus(id, recalled ? "active" : "recalled", note))) reload();
    });
    connect(csv, &QPushButton::clicked, this, [this] { ui::exportModelCsv(this, m_batches->model(), "batches.csv", true); });
    return w;
}

QWidget *InventoryPage::buildInspections()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->addWidget(ui::muted(tr("Medicines returned by customers wait here. Check the seal and expiry. Restock good items, or mark them damaged."), w));
    auto *bar = new QHBoxLayout;
    auto *restock = ui::button(tr("Restock"), w, true);
    auto *damage = ui::button(tr("Mark damaged"), w);
    restock->setEnabled(ui::can("inventory.inspect"));
    damage->setEnabled(ui::can("inventory.inspect"));
    bar->addStretch();
    bar->addWidget(damage);
    bar->addWidget(restock);
    v->addLayout(bar);
    m_inspect = new ui::SqlTable(w);
    v->addWidget(m_inspect, 1);
    auto act = [this](bool ok) {
        const qint64 id = m_inspect->currentId();
        if (!id) return;
        bool given = false;
        const QString note = ui::askText(this, tr("Inspection"), tr("Note:"), &given);
        if (given && ui::check(this, InventoryService::inspectReturn(id, ok, note))) emit AppEvents::instance()->pharmacyChanged();
    };
    connect(restock, &QPushButton::clicked, this, [act] { act(true); });
    connect(damage, &QPushButton::clicked, this, [act] { act(false); });
    return w;
}

QWidget *InventoryPage::buildCounts()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->addWidget(ui::muted(tr("Start a count, enter what is on the shelf for each batch, then finish. Differences are saved as stock movements. "
                              "Sales during the count are taken into account."), w));
    auto *bar = new QHBoxLayout;
    m_countInfo = new QLabel(w);
    auto *start = ui::button(tr("Start new count"), w);
    auto *enter = ui::button(tr("Enter counted quantity"), w);
    auto *finish = ui::button(tr("Finish count"), w, true);
    for (auto *b : {start, enter, finish}) b->setEnabled(ui::can("inventory.adjust"));
    bar->addWidget(m_countInfo);
    bar->addStretch();
    bar->addWidget(start);
    bar->addWidget(enter);
    bar->addWidget(finish);
    v->addLayout(bar);
    m_count = new ui::SqlTable(w);
    m_count->format()->numbers = {4, 5, 6};
    v->addWidget(m_count, 1);
    connect(start, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString note = ui::askText(this, tr("Stock count"), tr("Note, for example shelf A or full count:"), &ok);
        if (!ok) return;
        m_countId = InventoryService::startCount(note);
        if (!m_countId) ui::error(this, tr("Could not start the count."));
        reload();
    });
    auto enterQty = [this] {
        const qint64 batch = m_count->currentId();
        if (!batch || !m_countId) return;
        bool ok = false;
        const int n = QInputDialog::getInt(this, tr("Counted quantity"), m_count->currentValue(1).toString() + " " + m_count->currentValue(2).toString(),
                                           m_count->currentValue(4).toInt(), 0, 10000000, 1, &ok);
        if (ok && ui::check(this, InventoryService::recordCount(m_countId, batch, n))) m_count->reload();
    };
    connect(enter, &QPushButton::clicked, this, enterQty);
    connect(m_count, &ui::SqlTable::activatedId, this, enterQty);
    connect(finish, &QPushButton::clicked, this, [this] {
        if (!m_countId || !ui::confirm(this, tr("Finish this count and save the differences?"))) return;
        if (ui::check(this, InventoryService::finishCount(m_countId))) { m_countId = 0; emit AppEvents::instance()->pharmacyChanged(); }
    });
    return w;
}

QWidget *InventoryPage::buildMovements()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    auto *bar = new QHBoxLayout;
    m_moveRange = new ui::DateRangeBar(w, 3);
    auto *csv = ui::button(tr("Export CSV"), w);
    bar->addWidget(m_moveRange);
    bar->addStretch();
    bar->addWidget(csv);
    v->addLayout(bar);
    m_moves = new ui::SqlTable(w);
    m_moves->format()->numbers = {5};
    v->addWidget(m_moves, 1);
    connect(m_moveRange, &ui::DateRangeBar::changed, this, &InventoryPage::reload);
    connect(csv, &QPushButton::clicked, this, [this] { ui::exportModelCsv(this, m_moves->model(), "stock_movements.csv", true); });
    return w;
}

void InventoryPage::reload()
{
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    const int win = m_window->currentData().toInt();
    QString where = "b.qty_available + b.qty_quarantine + b.qty_damaged > 0";
    if (win == 0) where += " AND b.expiry_date <= '" + today + "'";
    else if (win > 0) where += QString(" AND b.expiry_date > '%1' AND b.expiry_date <= date('%1','+%2 day')").arg(today).arg(win);
    else if (win == -2) where = "b.status='recalled'";
    m_batches->setQuery(
        "SELECT b.id, m.brand_name || ' ' || COALESCE(m.strength,''), b.batch_no, COALESCE(b.mfg_date,''), b.expiry_date, "
        "CAST(julianday(b.expiry_date) - julianday(?) AS INTEGER), b.qty_available, b.qty_quarantine, b.qty_damaged, b.status, b.unit_cost, "
        "COALESCE(s.name,'') FROM medicine_batches b JOIN medicines m ON m.id=b.medicine_id LEFT JOIN suppliers s ON s.id=b.supplier_id WHERE "
            + where + " ORDER BY b.expiry_date, m.brand_name",
        {today},
        {"id", tr("Medicine"), tr("Batch"), tr("Manufactured"), tr("Expiry"), tr("Days left"), tr("Available"), tr("Quarantine"), tr("Damaged"),
         tr("Status"), tr("Unit cost"), tr("Supplier")});
    const int counts[5] = {
        scalar("SELECT COUNT(*) FROM medicine_batches WHERE qty_available+qty_quarantine>0 AND expiry_date<=?", {today}, 0).toInt(),
        scalar("SELECT COUNT(*) FROM medicine_batches WHERE qty_available>0 AND expiry_date>? AND expiry_date<=date(?,'+7 day')", {today, today}, 0).toInt(),
        scalar("SELECT COUNT(*) FROM medicine_batches WHERE qty_available>0 AND expiry_date>? AND expiry_date<=date(?,'+30 day')", {today, today}, 0).toInt(),
        scalar("SELECT COUNT(*) FROM medicine_batches WHERE qty_available>0 AND expiry_date>? AND expiry_date<=date(?,'+60 day')", {today, today}, 0).toInt(),
        scalar("SELECT COUNT(*) FROM medicine_batches WHERE qty_available>0 AND expiry_date>? AND expiry_date<=date(?,'+90 day')", {today, today}, 0).toInt()};
    for (int i = 0; i < 5; ++i) m_alert[i]->setText(QString::number(counts[i]));

    m_inspect->setQuery(
        "SELECT ri.id, datetime(r.ts,'localtime'), s.invoice_no, m.brand_name || ' ' || COALESCE(m.strength,''), b.batch_no, b.expiry_date, ri.qty, r.reason "
        "FROM sales_return_items ri JOIN sales_returns r ON r.id=ri.return_id JOIN sales s ON s.id=r.sale_id JOIN medicine_batches b ON b.id=ri.batch_id "
        "JOIN medicines m ON m.id=b.medicine_id WHERE ri.disposition='pending' ORDER BY ri.id",
        {}, {"id", tr("Returned"), tr("Invoice"), tr("Medicine"), tr("Batch"), tr("Expiry"), tr("Quantity"), tr("Reason")});

    if (!m_countId) m_countId = scalar("SELECT id FROM stock_counts WHERE status='open' ORDER BY id DESC LIMIT 1", {}, 0).toLongLong();
    m_countInfo->setText(m_countId ? tr("Count %1 is open.").arg(m_countId) : tr("No open count."));
    m_count->setQuery(
        "SELECT i.batch_id, m.brand_name || ' ' || COALESCE(m.strength,''), b.batch_no, b.expiry_date, i.expected, i.counted, "
        "CASE WHEN i.counted IS NULL THEN NULL ELSE i.counted - i.expected END FROM stock_count_items i JOIN medicine_batches b ON b.id=i.batch_id "
        "JOIN medicines m ON m.id=b.medicine_id WHERE i.count_id=? ORDER BY m.brand_name",
        {m_countId}, {"id", tr("Medicine"), tr("Batch"), tr("Expiry"), tr("Expected"), tr("Counted"), tr("Difference")});

    m_moves->setQuery(
        "SELECT mv.id, datetime(mv.ts,'localtime'), m.brand_name, b.batch_no, mv.bucket, mv.qty, mv.reason, COALESCE(mv.ref_type,'') || ' ' || "
        "COALESCE(mv.ref_id,''), COALESCE(u.full_name,''), COALESCE(mv.note,'') FROM stock_movements mv JOIN medicines m ON m.id=mv.medicine_id "
        "JOIN medicine_batches b ON b.id=mv.batch_id LEFT JOIN users u ON u.id=mv.user_id WHERE date(mv.ts,'localtime') BETWEEN ? AND ? "
        "ORDER BY mv.id DESC LIMIT 5000",
        {m_moveRange->from(), m_moveRange->to()},
        {"id", tr("Date and time"), tr("Medicine"), tr("Batch"), tr("Stock type"), tr("Change"), tr("Reason"), tr("Reference"), tr("User"), tr("Note")});
}

void InventoryPage::adjust()
{
    const qint64 batch = m_batches->currentId();
    if (!batch) { ui::info(this, tr("Select a batch first.")); return; }
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Adjust stock"));
    auto *v = new QVBoxLayout(&dlg);
    v->addWidget(new QLabel(m_batches->currentValue(1).toString() + " " + tr("batch") + " " + m_batches->currentValue(2).toString(), &dlg));
    auto *form = new QFormLayout;
    auto *kind = new QComboBox(&dlg);
    kind->addItem(tr("Move to damaged"), "damage");
    kind->addItem(tr("Found extra stock (add)"), "correction_in");
    kind->addItem(tr("Missing stock (remove)"), "correction_out");
    kind->addItem(tr("Dispose damaged stock"), "dispose_damaged");
    kind->addItem(tr("Dispose expired stock"), "dispose_expired");
    auto *qty = new QSpinBox(&dlg);
    qty->setRange(1, 10000000);
    auto *reason = new QLineEdit(&dlg);
    form->addRow(tr("Type"), kind);
    form->addRow(tr("Quantity"), qty);
    form->addRow(tr("Reason"), reason);
    v->addLayout(form);
    auto *ok = ui::button(tr("Save"), &dlg, true);
    v->addWidget(ok, 0, Qt::AlignRight);
    connect(ok, &QPushButton::clicked, &dlg, [&] {
        if (ui::check(&dlg, InventoryService::adjust(batch, kind->currentData().toString(), qty->value(), reason->text()))) dlg.accept();
    });
    if (dlg.exec() == QDialog::Accepted) emit AppEvents::instance()->pharmacyChanged();
}
}
