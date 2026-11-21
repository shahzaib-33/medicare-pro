#include "PartiesPage.h"
#include "Ui.h"
#include "services/PurchaseService.h"
#include "services/SalesService.h"
#include "services/SettingsService.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QSqlQuery>

namespace mcp {

PartiesPage::PartiesPage(Kind kind, AppContext *, QWidget *parent) : QWidget(parent), m_kind(kind)
{
    const bool sup = kind == Kind::Supplier;
    auto f = ui::page(this, sup ? tr("Suppliers") : tr("Customers"));
    auto *add = ui::button(sup ? tr("Add supplier") : tr("Add customer"), this, true);
    auto *editBtn = ui::button(tr("Edit"), this);
    auto *pay = ui::button(sup ? tr("Pay supplier") : tr("Receive payment"), this);
    auto *open = ui::button(tr("Opening balance"), this);
    auto *stmt = ui::button(tr("Statement"), this);
    const QString editPerm = sup ? "suppliers.edit" : "customers.edit";
    add->setEnabled(ui::can(editPerm));
    editBtn->setEnabled(ui::can(editPerm));
    open->setEnabled(ui::can(editPerm));
    pay->setEnabled(ui::can(sup ? "suppliers.pay" : "customers.receive"));
    for (auto *b : {editBtn, open, pay, stmt, add}) f.actions->addWidget(b);
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Search by name or phone"));
    m_filter->setClearButtonEnabled(true);
    f.body->addWidget(m_filter);
    auto *split = new QSplitter(Qt::Vertical, this);
    m_table = new ui::SqlTable(split);
    m_ledger = new ui::SqlTable(split);
    m_ledger->format()->money = {5, 6, 7};
    split->addWidget(m_table);
    split->addWidget(m_ledger);
    f.body->addWidget(split, 1);
    connect(m_filter, &QLineEdit::textChanged, this, &PartiesPage::reload);
    connect(add, &QPushButton::clicked, this, [this] { edit(0); });
    connect(editBtn, &QPushButton::clicked, this, [this] { if (m_table->currentId()) edit(m_table->currentId()); });
    connect(m_table, &ui::SqlTable::activatedId, this, [this](qint64 id) { edit(id); });
    connect(pay, &QPushButton::clicked, this, &PartiesPage::payment);
    connect(open, &QPushButton::clicked, this, &PartiesPage::openingBalance);
    connect(stmt, &QPushButton::clicked, this, &PartiesPage::statement);
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this] {
        const QString table = m_kind == Kind::Supplier ? "supplier_ledger" : "customer_ledger";
        const QString key = m_kind == Kind::Supplier ? "supplier_id" : "customer_id";
        const QString sign = m_kind == Kind::Supplier ? "credit - debit" : "debit - credit";
        m_ledger->setQuery("SELECT id, datetime(ts,'localtime'), entry_type, COALESCE(ref_type,'') || ' ' || COALESCE(ref_id,''), COALESCE(note,''), "
                           "debit, credit, SUM(" + sign + ") OVER (ORDER BY ts, id) FROM " + table + " WHERE " + key + "=? ORDER BY ts, id",
                           {m_table->currentId()},
                           {"id", tr("Date and time"), tr("Entry"), tr("Reference"), tr("Note"), tr("Debit"), tr("Credit"), tr("Balance")});
    });
    connect(AppEvents::instance(), &AppEvents::pharmacyChanged, this, &PartiesPage::reload);
    reload();
}

void PartiesPage::reload()
{
    const QString like = "%" + m_filter->text().trimmed() + "%";
    if (m_kind == Kind::Supplier) {
        m_table->format()->money = {4};
        m_table->setQuery("SELECT s.id, s.name, COALESCE(s.phone,''), COALESCE(s.address,''), "
                          "COALESCE((SELECT SUM(credit - debit) FROM supplier_ledger l WHERE l.supplier_id=s.id),0), "
                          "CASE s.active WHEN 1 THEN '' ELSE 'inactive' END FROM suppliers s WHERE s.name LIKE ? OR COALESCE(s.phone,'') LIKE ? "
                          "ORDER BY s.active DESC, s.name", {like, like},
                          {"id", tr("Name"), tr("Phone"), tr("Address"), tr("Balance owed"), tr("Status")});
    } else {
        m_table->format()->money = {4, 5};
        m_table->setQuery("SELECT c.id, c.name, COALESCE(c.phone,''), COALESCE(c.address,''), c.credit_limit, "
                          "COALESCE((SELECT SUM(debit - credit) FROM customer_ledger l WHERE l.customer_id=c.id),0), "
                          "CASE c.active WHEN 1 THEN '' ELSE 'inactive' END FROM customers c WHERE c.name LIKE ? OR COALESCE(c.phone,'') LIKE ? "
                          "ORDER BY c.active DESC, c.name", {like, like},
                          {"id", tr("Name"), tr("Phone"), tr("Address"), tr("Credit limit"), tr("Balance due"), tr("Status")});
    }
}

void PartiesPage::edit(qint64 id)
{
    const bool sup = m_kind == Kind::Supplier;
    QDialog dlg(this);
    dlg.setWindowTitle(id ? tr("Edit") : (sup ? tr("Add supplier") : tr("Add customer")));
    dlg.setMinimumWidth(440);
    auto *form = new QFormLayout(&dlg);
    auto *name = new QLineEdit(&dlg);
    auto *phone = new QLineEdit(&dlg);
    auto *address = new QLineEdit(&dlg);
    auto *notes = new QPlainTextEdit(&dlg);
    notes->setMaximumHeight(80);
    auto *limit = new ui::MoneyEdit(&dlg);
    auto *active = new QCheckBox(tr("Active"), &dlg);
    active->setChecked(true);
    form->addRow(tr("Name"), name);
    form->addRow(tr("Phone"), phone);
    form->addRow(tr("Address"), address);
    if (!sup) form->addRow(tr("Credit limit (0 = no limit)"), limit);
    form->addRow(tr("Notes"), notes);
    form->addRow(QString(), active);
    if (id) {
        QSqlQuery q(Db::get(Db::Pharmacy));
        q.prepare(sup ? "SELECT name, phone, address, notes, active, 0 FROM suppliers WHERE id=?"
                      : "SELECT name, phone, address, notes, active, credit_limit FROM customers WHERE id=?");
        q.addBindValue(id);
        q.exec();
        if (q.next()) {
            name->setText(q.value(0).toString());
            phone->setText(q.value(1).toString());
            address->setText(q.value(2).toString());
            notes->setPlainText(q.value(3).toString());
            active->setChecked(q.value(4).toBool());
            limit->setValue(q.value(5).toLongLong());
        }
    }
    auto *ok = ui::button(tr("Save"), &dlg, true);
    form->addRow(ok);
    connect(ok, &QPushButton::clicked, &dlg, [&] {
        const Result r = sup ? SupplierService::save(id, name->text(), phone->text(), address->text(), notes->toPlainText(), active->isChecked())
                             : CustomerService::save(id, name->text(), phone->text(), address->text(), notes->toPlainText(), limit->value(),
                                                     active->isChecked());
        if (ui::check(&dlg, r)) dlg.accept();
    });
    if (dlg.exec() == QDialog::Accepted) emit AppEvents::instance()->pharmacyChanged();
}

void PartiesPage::payment()
{
    const qint64 id = m_table->currentId();
    if (!id) { ui::info(this, tr("Select a name first.")); return; }
    const bool sup = m_kind == Kind::Supplier;
    QDialog dlg(this);
    dlg.setWindowTitle(sup ? tr("Pay supplier") : tr("Receive payment"));
    auto *form = new QFormLayout(&dlg);
    form->addRow(new QLabel(m_table->currentValue(1).toString(), &dlg));
    auto *amount = new ui::MoneyEdit(&dlg);
    amount->setValue(m_table->currentValue(sup ? 4 : 5).toLongLong());
    auto *method = new QComboBox(&dlg);
    for (const QString &m : PaymentMethods::all())
        if (m != "customer_credit") method->addItem(PaymentMethods::label(m), m);
    auto *ref = new QLineEdit(&dlg);
    form->addRow(tr("Amount"), amount);
    form->addRow(tr("Method"), method);
    form->addRow(tr("Reference"), ref);
    if (sup) form->addRow(ui::muted(tr("This payment is on account. To pay one invoice, use the Purchases page."), &dlg));
    auto *ok = ui::button(tr("Save payment"), &dlg, true);
    form->addRow(ok);
    connect(ok, &QPushButton::clicked, &dlg, [&] {
        const Result r = sup ? PurchaseService::pay(id, 0, amount->value(), method->currentData().toString(), ref->text())
                             : CustomerService::receivePayment(id, amount->value(), method->currentData().toString(), ref->text());
        if (ui::check(&dlg, r)) dlg.accept();
    });
    if (dlg.exec() == QDialog::Accepted) emit AppEvents::instance()->pharmacyChanged();
}

void PartiesPage::openingBalance()
{
    const qint64 id = m_table->currentId();
    if (!id) { ui::info(this, tr("Select a name first.")); return; }
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Opening balance"));
    auto *form = new QFormLayout(&dlg);
    form->addRow(ui::muted(m_kind == Kind::Supplier ? tr("Amount you owed this supplier before using MediCare Pro.")
                                                    : tr("Amount this customer owed before using MediCare Pro."), &dlg));
    auto *amount = new ui::MoneyEdit(&dlg);
    form->addRow(tr("Amount"), amount);
    auto *ok = ui::button(tr("Save"), &dlg, true);
    form->addRow(ok);
    connect(ok, &QPushButton::clicked, &dlg, [&] {
        const Result r = m_kind == Kind::Supplier ? SupplierService::openingBalance(id, amount->value())
                                                  : CustomerService::openingBalance(id, amount->value());
        if (ui::check(&dlg, r)) dlg.accept();
    });
    if (dlg.exec() == QDialog::Accepted) emit AppEvents::instance()->pharmacyChanged();
}

void PartiesPage::statement()
{
    const qint64 id = m_table->currentId();
    if (!id) { ui::info(this, tr("Select a name first.")); return; }
    const bool sup = m_kind == Kind::Supplier;
    const PharmacyProfile p = SettingsService::profile();
    QString h = "<html><body style='font-family:\"Segoe UI\",Arial; font-size:9pt;'><h2>" + p.name.toHtmlEscaped() + "</h2>";
    h += "<h3>" + (sup ? tr("Supplier statement") : tr("Customer statement")).toHtmlEscaped() + ": " + m_table->currentValue(1).toString().toHtmlEscaped()
         + "</h3><div>" + tr("Printed on %1").arg(QDateTime::currentDateTime().toString("dd-MM-yyyy hh:mm")).toHtmlEscaped() + "</div><br/>";
    h += "<table width='100%' border='1' cellspacing='0' cellpadding='3'><tr style='background:#ece8f8;'><th>" + tr("Date").toHtmlEscaped() + "</th><th>"
         + tr("Entry").toHtmlEscaped() + "</th><th>" + tr("Note").toHtmlEscaped() + "</th><th>" + tr("Debit").toHtmlEscaped() + "</th><th>"
         + tr("Credit").toHtmlEscaped() + "</th><th>" + tr("Balance").toHtmlEscaped() + "</th></tr>";
    QSqlQuery q(Db::get(Db::Pharmacy));
    q.prepare(QString("SELECT date(ts,'localtime'), entry_type, COALESCE(note,''), debit, credit FROM %1 WHERE %2=? ORDER BY ts, id")
                  .arg(sup ? "supplier_ledger" : "customer_ledger", sup ? "supplier_id" : "customer_id"));
    q.addBindValue(id);
    q.exec();
    Paisa bal = 0;
    while (q.next()) {
        bal += sup ? q.value(4).toLongLong() - q.value(3).toLongLong() : q.value(3).toLongLong() - q.value(4).toLongLong();
        h += "<tr><td>" + q.value(0).toString() + "</td><td>" + q.value(1).toString().toHtmlEscaped() + "</td><td>" + q.value(2).toString().toHtmlEscaped()
             + "</td><td align='right'>" + Money::format(q.value(3).toLongLong(), false) + "</td><td align='right'>"
             + Money::format(q.value(4).toLongLong(), false) + "</td><td align='right'>" + Money::format(bal, false) + "</td></tr>";
    }
    h += "</table><h3>" + (sup ? tr("Balance owed: %1") : tr("Balance due: %1")).arg(Money::format(bal)).toHtmlEscaped() + "</h3></body></html>";
    ui::printHtml(this, h, 0, true);
}
}
