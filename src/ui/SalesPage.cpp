#include "SalesPage.h"
#include "ReceiptDialog.h"
#include "Ui.h"
#include "services/SalesService.h"
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QSpinBox>
#include <QSqlQuery>
#include <QTableWidget>

namespace mcp {

SalesPage::SalesPage(AppContext *, QWidget *parent) : QWidget(parent)
{
    auto f = ui::page(this, tr("Sales history"));
    auto *view = ui::button(tr("Items"), this);
    auto *receipt = ui::button(tr("Receipt"), this);
    auto *ret = ui::button(tr("Return items"), this, true);
    auto *csv = ui::button(tr("Export CSV"), this);
    ret->setEnabled(ui::can("sales.return"));
    for (auto *b : {view, receipt, csv, ret}) f.actions->addWidget(b);
    auto *bar = new QHBoxLayout;
    m_range = new ui::DateRangeBar(this, 0);
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Invoice number or customer"));
    m_filter->setClearButtonEnabled(true);
    bar->addWidget(m_range);
    bar->addWidget(m_filter, 1);
    f.body->addLayout(bar);
    m_table = new ui::SqlTable(this);
    m_table->format()->money = {6, 7, 8};
    f.body->addWidget(m_table, 1);
    connect(m_range, &ui::DateRangeBar::changed, this, &SalesPage::reload);
    connect(m_filter, &QLineEdit::textChanged, this, &SalesPage::reload);
    connect(m_table, &ui::SqlTable::activatedId, this, &SalesPage::showItems);
    connect(view, &QPushButton::clicked, this, &SalesPage::showItems);
    connect(receipt, &QPushButton::clicked, this, [this] { if (m_table->currentId()) ReceiptDialog(m_table->currentId(), false, this).exec(); });
    connect(ret, &QPushButton::clicked, this, &SalesPage::returnSale);
    connect(csv, &QPushButton::clicked, this, [this] { ui::exportModelCsv(this, m_table->model(), "sales.csv", true); });
    connect(AppEvents::instance(), &AppEvents::pharmacyChanged, this, &SalesPage::reload);
    reload();
}

void SalesPage::reload()
{
    const QString like = "%" + m_filter->text().trimmed() + "%";
    m_table->setQuery(
        "SELECT s.id, s.invoice_no, datetime(s.ts,'localtime'), COALESCE(c.name,''), COALESCE(u.full_name,''), "
        "(SELECT GROUP_CONCAT(method, ', ') FROM (SELECT DISTINCT method FROM sale_payments p WHERE p.sale_id=s.id)), s.total, "
        "COALESCE((SELECT SUM(total) FROM sales_returns r WHERE r.sale_id=s.id),0), s.discount, "
        "(SELECT COUNT(*) FROM receipt_prints p WHERE p.sale_id=s.id) FROM sales s LEFT JOIN customers c ON c.id=s.customer_id "
        "LEFT JOIN users u ON u.id=s.user_id WHERE date(s.ts,'localtime') BETWEEN ? AND ? AND (s.invoice_no LIKE ? OR COALESCE(c.name,'') LIKE ?) "
        "ORDER BY s.id DESC",
        {m_range->from(), m_range->to(), like, like},
        {"id", tr("Invoice"), tr("Date and time"), tr("Customer"), tr("Cashier"), tr("Paid by"), tr("Total"), tr("Returned"), tr("Discount"),
         tr("Prints")});
}

void SalesPage::showItems()
{
    const qint64 id = m_table->currentId();
    if (!id) return;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Sale %1").arg(m_table->currentValue(1).toString()));
    dlg.resize(760, 420);
    auto *v = new QVBoxLayout(&dlg);
    auto *t = new ui::SqlTable(&dlg);
    t->format()->money = {3, 4, 5, 6};
    t->format()->numbers = {2};
    t->setQuery("SELECT i.id, m.brand_name || ' ' || COALESCE(m.strength,''), i.qty, i.unit_price, i.discount, i.tax, i.line_total, "
                "(SELECT GROUP_CONCAT(b.batch_no || ' (' || b.expiry_date || ') x' || sb.qty, '; ') FROM sale_batches sb "
                "JOIN medicine_batches b ON b.id=sb.batch_id WHERE sb.sale_item_id=i.id) FROM sale_items i JOIN medicines m ON m.id=i.medicine_id "
                "WHERE i.sale_id=?", {id}, {"id", tr("Medicine"), tr("Qty"), tr("Price"), tr("Discount"), tr("Tax"), tr("Total"), tr("Batches")});
    v->addWidget(t);
    dlg.exec();
}

void SalesPage::returnSale()
{
    const qint64 saleId = m_table->currentId();
    if (!saleId) { ui::info(this, tr("Select a sale first.")); return; }
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Return items from %1").arg(m_table->currentValue(1).toString()));
    dlg.resize(720, 460);
    auto *v = new QVBoxLayout(&dlg);
    v->addWidget(ui::muted(tr("Returned medicines go to quarantine. A pharmacist inspects them before they can be sold again."), &dlg));
    auto *t = new QTableWidget(0, 4, &dlg);
    t->setHorizontalHeaderLabels({tr("Medicine"), tr("Sold"), tr("Can return"), tr("Return now")});
    t->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    t->verticalHeader()->hide();
    QList<qint64> itemIds;
    QList<QSpinBox *> spins;
    QSqlQuery q(Db::get(Db::Pharmacy));
    q.prepare("SELECT i.id, m.brand_name || ' ' || COALESCE(m.strength,''), i.qty FROM sale_items i JOIN medicines m ON m.id=i.medicine_id WHERE i.sale_id=?");
    q.addBindValue(saleId);
    q.exec();
    while (q.next()) {
        const int r = t->rowCount();
        t->insertRow(r);
        const qint64 item = q.value(0).toLongLong();
        const int can = SalesService::returnableQty(item);
        t->setItem(r, 0, new QTableWidgetItem(q.value(1).toString()));
        t->setItem(r, 1, new QTableWidgetItem(QString::number(q.value(2).toInt())));
        t->setItem(r, 2, new QTableWidgetItem(QString::number(can)));
        auto *s = new QSpinBox(t);
        s->setRange(0, can);
        t->setCellWidget(r, 3, s);
        itemIds << item;
        spins << s;
    }
    v->addWidget(t, 1);
    auto *form = new QFormLayout;
    auto *method = new QComboBox(&dlg);
    method->addItem(PaymentMethods::label("cash"), "cash");
    const bool hasCustomer = scalar("SELECT customer_id IS NOT NULL FROM sales WHERE id=?", {saleId}).toBool();
    if (hasCustomer) method->addItem(tr("Credit to customer account"), "customer_credit");
    for (const QString &m : {"bank_transfer", "jazzcash", "easypaisa", "raast", "card"}) method->addItem(PaymentMethods::label(m), m);
    auto *reason = new QLineEdit(&dlg);
    reason->setPlaceholderText(tr("For example: wrong medicine, unopened pack"));
    form->addRow(tr("Refund method"), method);
    form->addRow(tr("Reason"), reason);
    v->addLayout(form);
    auto *ok = ui::button(tr("Save return"), &dlg, true);
    v->addWidget(ok, 0, Qt::AlignRight);
    connect(ok, &QPushButton::clicked, &dlg, [&] {
        QVector<ReturnRequestLine> lines;
        for (int i = 0; i < spins.size(); ++i)
            if (spins[i]->value() > 0) lines.append({itemIds[i], spins[i]->value()});
        const Result r = SalesService::returnItems(saleId, lines, method->currentData().toString(), reason->text());
        if (!ui::check(&dlg, r)) return;
        dlg.accept();
        emit AppEvents::instance()->pharmacyChanged();
        ReceiptDialog(r.id, true, this).exec();
    });
    dlg.exec();
}
}
