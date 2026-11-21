#include "PurchasesPage.h"
#include "Ui.h"
#include "services/PurchaseService.h"
#include "services/SalesService.h"
#include <QComboBox>
#include <QCompleter>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QSpinBox>
#include <QSqlQuery>
#include <QTableWidget>

namespace mcp {

QComboBox *medicinePicker(QWidget *parent)
{
    auto *c = new QComboBox(parent);
    c->setEditable(true);
    c->setInsertPolicy(QComboBox::NoInsert);
    c->addItem(QString(), 0);
    QSqlQuery q(Db::get(Db::Pharmacy));
    q.exec("SELECT id, brand_name || ' ' || COALESCE(strength,'') || ' ' || COALESCE(dosage_form,''), purchase_price, sale_price "
           "FROM medicines WHERE archived=0 ORDER BY brand_name");
    while (q.next()) {
        c->addItem(q.value(1).toString().simplified(), q.value(0));
        c->setItemData(c->count() - 1, q.value(2), Qt::UserRole + 1);
        c->setItemData(c->count() - 1, q.value(3), Qt::UserRole + 2);
    }
    c->completer()->setFilterMode(Qt::MatchContains);
    c->completer()->setCompletionMode(QCompleter::PopupCompletion);
    c->setMinimumWidth(220);
    return c;
}

static QComboBox *supplierPicker(QWidget *parent)
{
    auto *c = new QComboBox(parent);
    QSqlQuery q(Db::get(Db::Pharmacy));
    q.exec("SELECT id, name FROM suppliers WHERE active=1 ORDER BY name");
    while (q.next()) c->addItem(q.value(1).toString(), q.value(0));
    return c;
}

static QComboBox *methodPicker(QWidget *parent, bool includeCredit = false)
{
    auto *c = new QComboBox(parent);
    for (const QString &m : PaymentMethods::all())
        if (includeCredit || m != "customer_credit") c->addItem(PaymentMethods::label(m), m);
    return c;
}

PurchasesPage::PurchasesPage(AppContext *, QWidget *parent) : QWidget(parent)
{
    auto f = ui::page(this, tr("Purchases"));
    auto *add = ui::button(tr("New purchase invoice"), this, true);
    auto *items = ui::button(tr("Items"), this);
    auto *pay = ui::button(tr("Pay supplier"), this);
    auto *ret = ui::button(tr("Return to supplier"), this);
    auto *rev = ui::button(tr("Reverse invoice"), this);
    auto *csv = ui::button(tr("Export CSV"), this);
    add->setEnabled(ui::can("purchases.post"));
    pay->setEnabled(ui::can("suppliers.pay"));
    ret->setEnabled(ui::can("purchases.return"));
    rev->setEnabled(ui::can("purchases.reverse"));
    for (auto *b : {items, pay, ret, rev, csv, add}) f.actions->addWidget(b);
    m_range = new ui::DateRangeBar(this, 3);
    f.body->addWidget(m_range);
    m_table = new ui::SqlTable(this);
    m_table->format()->money = {5, 6, 7, 8, 9};
    f.body->addWidget(m_table, 1);
    connect(m_range, &ui::DateRangeBar::changed, this, &PurchasesPage::reload);
    connect(add, &QPushButton::clicked, this, &PurchasesPage::newPurchase);
    connect(items, &QPushButton::clicked, this, &PurchasesPage::showItems);
    connect(m_table, &ui::SqlTable::activatedId, this, &PurchasesPage::showItems);
    connect(pay, &QPushButton::clicked, this, &PurchasesPage::pay);
    connect(ret, &QPushButton::clicked, this, &PurchasesPage::returnItems);
    connect(rev, &QPushButton::clicked, this, &PurchasesPage::reverse);
    connect(csv, &QPushButton::clicked, this, [this] { ui::exportModelCsv(this, m_table->model(), "purchases.csv", true); });
    connect(AppEvents::instance(), &AppEvents::pharmacyChanged, this, &PurchasesPage::reload);
    reload();
}

void PurchasesPage::reload()
{
    m_table->setQuery(
        "SELECT p.id, p.invoice_date, s.name, p.invoice_no, p.status, p.subtotal, p.discount, p.tax, p.total, "
        "CASE WHEN p.status='posted' THEN p.total - p.paid ELSE 0 END, p.paid, COALESCE(u.full_name,'') "
        "FROM purchase_invoices p JOIN suppliers s ON s.id=p.supplier_id LEFT JOIN users u ON u.id=p.user_id "
        "WHERE p.invoice_date BETWEEN ? AND ? ORDER BY p.invoice_date DESC, p.id DESC",
        {m_range->from(), m_range->to()},
        {"id", tr("Date"), tr("Supplier"), tr("Invoice"), tr("Status"), tr("Subtotal"), tr("Discount"), tr("Tax"), tr("Total"), tr("Due"),
         tr("Paid"), tr("Entered by")});
    m_table->format()->money = {5, 6, 7, 8, 9, 10};
}

void PurchasesPage::newPurchase()
{
    if (scalar("SELECT COUNT(*) FROM suppliers WHERE active=1", {}, 0).toInt() == 0) {
        ui::info(this, tr("Add a supplier first on the Suppliers page."));
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle(tr("New purchase invoice"));
    dlg.resize(1180, 640);
    auto *v = new QVBoxLayout(&dlg);
    auto *head = new QHBoxLayout;
    auto *supplier = supplierPicker(&dlg);
    auto *invNo = new QLineEdit(&dlg);
    auto *date = ui::dateEdit(QDate::currentDate(), &dlg);
    head->addWidget(new QLabel(tr("Supplier"), &dlg));
    head->addWidget(supplier, 1);
    head->addWidget(new QLabel(tr("Invoice number"), &dlg));
    head->addWidget(invNo);
    head->addWidget(new QLabel(tr("Invoice date"), &dlg));
    head->addWidget(date);
    v->addLayout(head);
    v->addWidget(ui::muted(tr("Quantities and costs are per base unit (tablet, ml, piece). Bonus units are free and lower the real unit cost."), &dlg));
    auto *t = new QTableWidget(0, 10, &dlg);
    t->setHorizontalHeaderLabels({tr("Medicine"), tr("Batch"), tr("Mfg date"), tr("Expiry"), tr("Qty"), tr("Bonus"), tr("Unit cost"),
                                  tr("Discount"), tr("Tax %"), tr("Sale price")});
    t->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    t->verticalHeader()->setDefaultSectionSize(36);
    v->addWidget(t, 1);
    auto *lineBtns = new QHBoxLayout;
    auto *addLine = ui::button(tr("Add line"), &dlg);
    auto *rmLine = ui::button(tr("Remove line"), &dlg);
    lineBtns->addWidget(addLine);
    lineBtns->addWidget(rmLine);
    lineBtns->addStretch();
    v->addLayout(lineBtns);
    auto *form = new QFormLayout;
    auto *invDisc = new ui::MoneyEdit(&dlg);
    auto *paid = new ui::MoneyEdit(&dlg);
    auto *method = methodPicker(&dlg);
    auto *ref = new QLineEdit(&dlg);
    auto *notes = new QLineEdit(&dlg);
    auto *totals = new QLabel(&dlg);
    totals->setProperty("role", "h2");
    form->addRow(tr("Invoice discount"), invDisc);
    form->addRow(tr("Paid now"), paid);
    form->addRow(tr("Payment method"), method);
    form->addRow(tr("Payment reference"), ref);
    form->addRow(tr("Notes"), notes);
    form->addRow(tr("Totals"), totals);
    v->addLayout(form);
    auto *save = ui::button(tr("Post invoice"), &dlg, true);
    v->addWidget(save, 0, Qt::AlignRight);

    auto draft = [=] {
        PurchaseDraft d;
        d.supplierId = supplier->currentData().toLongLong();
        d.invoiceNo = invNo->text().trimmed();
        d.invoiceDate = date->date().toString(Qt::ISODate);
        d.invoiceDiscount = invDisc->value();
        d.paidNow = paid->value();
        d.paymentMethod = method->currentData().toString();
        d.paymentReference = ref->text();
        d.notes = notes->text();
        for (int r = 0; r < t->rowCount(); ++r) {
            auto *med = qobject_cast<QComboBox *>(t->cellWidget(r, 0));
            PurchaseLine l;
            l.medicineId = med && med->currentIndex() > 0 && med->currentText() == med->itemText(med->currentIndex()) ? med->currentData().toLongLong() : 0;
            l.batchNo = qobject_cast<QLineEdit *>(t->cellWidget(r, 1))->text().trimmed();
            const QString mfg = qobject_cast<QLineEdit *>(t->cellWidget(r, 2))->text().trimmed();
            l.mfgDate = mfg;
            l.expiryDate = qobject_cast<QDateEdit *>(t->cellWidget(r, 3))->date().toString(Qt::ISODate);
            l.qty = qobject_cast<QSpinBox *>(t->cellWidget(r, 4))->value();
            l.bonusQty = qobject_cast<QSpinBox *>(t->cellWidget(r, 5))->value();
            l.unitCost = qobject_cast<ui::MoneyEdit *>(t->cellWidget(r, 6))->value();
            l.discount = qobject_cast<ui::MoneyEdit *>(t->cellWidget(r, 7))->value();
            l.taxBp = qRound(qobject_cast<QDoubleSpinBox *>(t->cellWidget(r, 8))->value() * 100);
            const Paisa sp = qobject_cast<ui::MoneyEdit *>(t->cellWidget(r, 9))->value();
            if (sp > 0) l.salePrice = sp;
            d.lines.append(l);
        }
        return d;
    };
    auto refresh = [=] {
        const PurchaseTotals pt = PurchaseService::compute(draft());
        totals->setText(tr("Subtotal %1, discount %2, tax %3, total %4")
                            .arg(Money::format(pt.subtotal), Money::format(pt.discount), Money::format(pt.tax), Money::format(pt.total)));
    };
    auto addRow = [=] {
        const int r = t->rowCount();
        t->insertRow(r);
        auto *med = medicinePicker(t);
        auto *batch = new QLineEdit(t);
        auto *mfg = new QLineEdit(t);
        mfg->setPlaceholderText("YYYY-MM-DD");
        auto *exp = ui::dateEdit(QDate::currentDate().addYears(2), t);
        auto *qty = new QSpinBox(t);
        qty->setRange(0, 10000000);
        auto *bonus = new QSpinBox(t);
        bonus->setRange(0, 10000000);
        auto *cost = new ui::MoneyEdit(t);
        auto *disc = new ui::MoneyEdit(t);
        auto *tax = new QDoubleSpinBox(t);
        tax->setRange(0, 100);
        auto *sale = new ui::MoneyEdit(t);
        t->setCellWidget(r, 0, med);
        t->setCellWidget(r, 1, batch);
        t->setCellWidget(r, 2, mfg);
        t->setCellWidget(r, 3, exp);
        t->setCellWidget(r, 4, qty);
        t->setCellWidget(r, 5, bonus);
        t->setCellWidget(r, 6, cost);
        t->setCellWidget(r, 7, disc);
        t->setCellWidget(r, 8, tax);
        t->setCellWidget(r, 9, sale);
        connect(med, &QComboBox::currentIndexChanged, t, [=] {
            if (med->currentIndex() > 0 && cost->value() == 0) cost->setValue(med->currentData(Qt::UserRole + 1).toLongLong());
        });
        connect(qty, &QSpinBox::valueChanged, t, refresh);
        connect(cost, &ui::MoneyEdit::valueChanged, t, refresh);
        connect(disc, &ui::MoneyEdit::valueChanged, t, refresh);
        connect(tax, &QDoubleSpinBox::valueChanged, t, refresh);
        t->setColumnWidth(1, 90);
        t->setColumnWidth(2, 100);
        t->setColumnWidth(3, 120);
        for (int c = 4; c < 10; ++c) t->setColumnWidth(c, 80);
    };
    connect(addLine, &QPushButton::clicked, &dlg, addRow);
    connect(rmLine, &QPushButton::clicked, &dlg, [=] { if (t->currentRow() >= 0) { t->removeRow(t->currentRow()); refresh(); } });
    connect(invDisc, &ui::MoneyEdit::valueChanged, &dlg, refresh);
    addRow();
    refresh();
    connect(save, &QPushButton::clicked, &dlg, [&] {
        const PurchaseDraft d = draft();
        for (const auto &l : d.lines)
            if (!l.medicineId) { ui::error(&dlg, tr("Pick a medicine from the list on every line.")); return; }
        if (ui::check(&dlg, PurchaseService::post(d))) dlg.accept();
    });
    if (dlg.exec() == QDialog::Accepted) emit AppEvents::instance()->pharmacyChanged();
}

void PurchasesPage::showItems()
{
    const qint64 id = m_table->currentId();
    if (!id) return;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Invoice %1").arg(m_table->currentValue(3).toString()));
    dlg.resize(900, 420);
    auto *v = new QVBoxLayout(&dlg);
    auto *t = new ui::SqlTable(&dlg);
    t->format()->money = {6, 7, 8, 9};
    t->setQuery("SELECT i.id, m.brand_name || ' ' || COALESCE(m.strength,''), b.batch_no, b.expiry_date, i.qty, i.bonus_qty, i.unit_cost, "
                "i.discount, i.tax, i.line_total FROM purchase_items i JOIN medicines m ON m.id=i.medicine_id JOIN medicine_batches b ON b.id=i.batch_id "
                "WHERE i.invoice_id=?", {id},
                {"id", tr("Medicine"), tr("Batch"), tr("Expiry"), tr("Qty"), tr("Bonus"), tr("Unit cost"), tr("Discount"), tr("Tax"), tr("Total")});
    v->addWidget(t);
    dlg.exec();
}

void PurchasesPage::pay()
{
    const qint64 id = m_table->currentId();
    if (!id) { ui::info(this, tr("Select an invoice first.")); return; }
    const qint64 supplier = scalar("SELECT supplier_id FROM purchase_invoices WHERE id=?", {id}).toLongLong();
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Pay supplier"));
    auto *form = new QFormLayout(&dlg);
    auto *amount = new ui::MoneyEdit(&dlg);
    amount->setValue(m_table->currentValue(9).toLongLong());
    auto *method = methodPicker(&dlg);
    auto *ref = new QLineEdit(&dlg);
    form->addRow(tr("Amount"), amount);
    form->addRow(tr("Method"), method);
    form->addRow(tr("Reference"), ref);
    auto *ok = ui::button(tr("Save payment"), &dlg, true);
    form->addRow(ok);
    connect(ok, &QPushButton::clicked, &dlg, [&] {
        if (ui::check(&dlg, PurchaseService::pay(supplier, id, amount->value(), method->currentData().toString(), ref->text()))) dlg.accept();
    });
    if (dlg.exec() == QDialog::Accepted) emit AppEvents::instance()->pharmacyChanged();
}

void PurchasesPage::returnItems()
{
    const qint64 id = m_table->currentId();
    if (!id) { ui::info(this, tr("Select an invoice first.")); return; }
    const qint64 supplier = scalar("SELECT supplier_id FROM purchase_invoices WHERE id=?", {id}).toLongLong();
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Return to supplier"));
    dlg.resize(760, 420);
    auto *v = new QVBoxLayout(&dlg);
    auto *t = new QTableWidget(0, 5, &dlg);
    t->setHorizontalHeaderLabels({tr("Medicine"), tr("Batch"), tr("Available"), tr("Damaged"), tr("Return")});
    t->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    QList<qint64> batches;
    QList<QSpinBox *> spins;
    QList<QComboBox *> buckets;
    QSqlQuery q(Db::get(Db::Pharmacy));
    q.prepare("SELECT DISTINCT b.id, m.brand_name, b.batch_no, b.qty_available, b.qty_damaged FROM purchase_items i JOIN medicine_batches b ON b.id=i.batch_id "
              "JOIN medicines m ON m.id=b.medicine_id WHERE i.invoice_id=?");
    q.addBindValue(id);
    q.exec();
    while (q.next()) {
        const int r = t->rowCount();
        t->insertRow(r);
        t->setItem(r, 0, new QTableWidgetItem(q.value(1).toString()));
        t->setItem(r, 1, new QTableWidgetItem(q.value(2).toString()));
        t->setItem(r, 2, new QTableWidgetItem(q.value(3).toString()));
        t->setItem(r, 3, new QTableWidgetItem(q.value(4).toString()));
        auto *w = new QWidget(t);
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        auto *bucket = new QComboBox(w);
        bucket->addItem(tr("from available"), "available");
        bucket->addItem(tr("from damaged"), "damaged");
        auto *s = new QSpinBox(w);
        s->setRange(0, qMax(q.value(3).toInt(), q.value(4).toInt()));
        h->addWidget(s);
        h->addWidget(bucket);
        t->setCellWidget(r, 4, w);
        t->setColumnWidth(4, 220);
        batches << q.value(0).toLongLong();
        spins << s;
        buckets << bucket;
    }
    v->addWidget(t, 1);
    auto *reason = new QLineEdit(&dlg);
    reason->setPlaceholderText(tr("Reason"));
    v->addWidget(reason);
    auto *ok = ui::button(tr("Save return"), &dlg, true);
    v->addWidget(ok, 0, Qt::AlignRight);
    connect(ok, &QPushButton::clicked, &dlg, [&] {
        QVector<ReturnLine> lines;
        for (int i = 0; i < spins.size(); ++i)
            if (spins[i]->value() > 0) lines.append({batches[i], buckets[i]->currentData().toString(), spins[i]->value()});
        if (ui::check(&dlg, PurchaseService::returnToSupplier(supplier, id, lines, reason->text()))) dlg.accept();
    });
    if (dlg.exec() == QDialog::Accepted) emit AppEvents::instance()->pharmacyChanged();
}

void PurchasesPage::reverse()
{
    const qint64 id = m_table->currentId();
    if (!id) { ui::info(this, tr("Select an invoice first.")); return; }
    bool ok = false;
    const QString reason = ui::askText(this, tr("Reverse invoice"),
                                       tr("Posted invoices are never edited. Reversing removes its stock and supplier balance. Reason:"), &ok);
    if (ok && ui::check(this, PurchaseService::reverse(id, reason))) emit AppEvents::instance()->pharmacyChanged();
}
}
