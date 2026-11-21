#include "OperationsPages.h"
#include "PurchasesPage.h"
#include "Ui.h"
#include "services/AuthService.h"
#include "services/SalesService.h"
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QSplitter>
#include <QSqlQuery>
#include <QTableWidget>

namespace mcp {

// ---------------- Prescriptions ----------------

PrescriptionsPage::PrescriptionsPage(AppContext *, QWidget *parent) : QWidget(parent)
{
    auto f = ui::page(this, tr("Prescriptions"));
    auto *add = ui::button(tr("New prescription"), this, true);
    auto *approve = ui::button(tr("Approve"), this);
    auto *reject = ui::button(tr("Reject"), this);
    add->setEnabled(ui::can("prescriptions.edit"));
    approve->setEnabled(ui::can("prescriptions.approve"));
    reject->setEnabled(ui::can("prescriptions.approve"));
    for (auto *b : {approve, reject, add}) f.actions->addWidget(b);
    f.body->addWidget(ui::muted(tr("Medicines marked as prescription required can be sold only with an approved prescription, "
                                   "or by a user who has the approve permission."), this));
    auto *split = new QSplitter(Qt::Vertical, this);
    m_table = new ui::SqlTable(split);
    m_items = new ui::SqlTable(split);
    split->addWidget(m_table);
    split->addWidget(m_items);
    f.body->addWidget(split, 1);
    connect(add, &QPushButton::clicked, this, &PrescriptionsPage::create);
    connect(approve, &QPushButton::clicked, this, [this] { review(true); });
    connect(reject, &QPushButton::clicked, this, [this] { review(false); });
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this] {
        m_items->setQuery("SELECT i.id, COALESCE(m.brand_name || ' ' || COALESCE(m.strength,''), i.medicine_text), i.qty, COALESCE(i.directions,'') "
                          "FROM prescription_items i LEFT JOIN medicines m ON m.id=i.medicine_id WHERE i.prescription_id=?",
                          {m_table->currentId()}, {"id", tr("Medicine"), tr("Quantity"), tr("Directions")});
    });
    connect(AppEvents::instance(), &AppEvents::pharmacyChanged, this, &PrescriptionsPage::reload);
    reload();
}

void PrescriptionsPage::reload()
{
    m_table->setQuery("SELECT p.id, p.ref_no, COALESCE(p.patient_name,''), COALESCE(p.doctor_name,''), COALESCE(p.issued_date,''), p.status, "
                      "COALESCE(u.full_name,''), COALESCE(p.review_note,''), datetime(p.created_at,'localtime') FROM prescriptions p "
                      "LEFT JOIN users u ON u.id=p.reviewed_by ORDER BY CASE p.status WHEN 'pending' THEN 0 ELSE 1 END, p.id DESC LIMIT 2000",
                      {}, {"id", tr("Reference"), tr("Patient"), tr("Doctor"), tr("Issued"), tr("Status"), tr("Reviewed by"), tr("Review note"), tr("Entered")});
}

void PrescriptionsPage::create()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("New prescription"));
    dlg.resize(760, 560);
    auto *v = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout;
    auto *ref = new QLineEdit(&dlg);
    auto *customer = new QComboBox(&dlg);
    customer->addItem(tr("No customer account"), 0);
    QSqlQuery q(Db::get(Db::Pharmacy));
    q.exec("SELECT id, name FROM customers WHERE active=1 ORDER BY name");
    while (q.next()) customer->addItem(q.value(1).toString(), q.value(0));
    auto *patient = new QLineEdit(&dlg);
    auto *doctor = new QLineEdit(&dlg);
    auto *issued = ui::dateEdit(QDate::currentDate(), &dlg);
    auto *notes = new QLineEdit(&dlg);
    form->addRow(tr("Reference number"), ref);
    form->addRow(tr("Customer"), customer);
    form->addRow(tr("Patient name"), patient);
    form->addRow(tr("Doctor name"), doctor);
    form->addRow(tr("Issued on"), issued);
    form->addRow(tr("Notes"), notes);
    v->addLayout(form);
    auto *t = new QTableWidget(0, 3, &dlg);
    t->setHorizontalHeaderLabels({tr("Medicine"), tr("Quantity"), tr("Directions")});
    t->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    t->verticalHeader()->setDefaultSectionSize(36);
    v->addWidget(t, 1);
    auto *addLine = ui::button(tr("Add medicine"), &dlg);
    v->addWidget(addLine, 0, Qt::AlignLeft);
    connect(addLine, &QPushButton::clicked, &dlg, [t] {
        const int r = t->rowCount();
        t->insertRow(r);
        t->setCellWidget(r, 0, medicinePicker(t));
        auto *s = new QSpinBox(t);
        s->setRange(1, 100000);
        t->setCellWidget(r, 1, s);
        t->setItem(r, 2, new QTableWidgetItem);
    });
    addLine->click();
    auto *ok = ui::button(tr("Save"), &dlg, true);
    v->addWidget(ok, 0, Qt::AlignRight);
    connect(ok, &QPushButton::clicked, &dlg, [&] {
        QVector<PrescriptionService::Item> items;
        for (int r = 0; r < t->rowCount(); ++r) {
            auto *med = qobject_cast<QComboBox *>(t->cellWidget(r, 0));
            PrescriptionService::Item it;
            const bool picked = med->currentIndex() > 0 && med->currentText() == med->itemText(med->currentIndex());
            it.medicineId = picked ? med->currentData().toLongLong() : 0;
            it.medicineText = picked ? QString() : med->currentText().trimmed();
            it.qty = qobject_cast<QSpinBox *>(t->cellWidget(r, 1))->value();
            it.directions = t->item(r, 2) ? t->item(r, 2)->text() : QString();
            if (it.medicineId || !it.medicineText.isEmpty()) items.append(it);
        }
        if (ui::check(&dlg, PrescriptionService::create(ref->text(), customer->currentData().toLongLong(), patient->text(), doctor->text(),
                                                        issued->date().toString(Qt::ISODate), notes->text(), items)))
            dlg.accept();
    });
    if (dlg.exec() == QDialog::Accepted) emit AppEvents::instance()->pharmacyChanged();
}

void PrescriptionsPage::review(bool approve)
{
    const qint64 id = m_table->currentId();
    if (!id) return;
    bool ok = false;
    const QString note = ui::askText(this, approve ? tr("Approve") : tr("Reject"), tr("Pharmacist note:"), &ok);
    if (ok && ui::check(this, PrescriptionService::review(id, approve, note))) emit AppEvents::instance()->pharmacyChanged();
}

// ---------------- Expenses ----------------

ExpensesPage::ExpensesPage(AppContext *, QWidget *parent) : QWidget(parent)
{
    auto f = ui::page(this, tr("Expenses"));
    auto *add = ui::button(tr("Add expense"), this, true);
    auto *rev = ui::button(tr("Reverse"), this);
    auto *csv = ui::button(tr("Export CSV"), this);
    add->setEnabled(ui::can("expenses.edit"));
    rev->setEnabled(ui::can("expenses.edit"));
    for (auto *b : {rev, csv, add}) f.actions->addWidget(b);
    auto *bar = new QHBoxLayout;
    m_range = new ui::DateRangeBar(this, 3);
    m_total = new QLabel(this);
    m_total->setProperty("role", "h2");
    bar->addWidget(m_range);
    bar->addStretch();
    bar->addWidget(m_total);
    f.body->addLayout(bar);
    m_table = new ui::SqlTable(this);
    m_table->format()->money = {6};
    f.body->addWidget(m_table, 1);
    connect(m_range, &ui::DateRangeBar::changed, this, &ExpensesPage::reload);
    connect(add, &QPushButton::clicked, this, &ExpensesPage::add);
    connect(rev, &QPushButton::clicked, this, [this] {
        const qint64 id = m_table->currentId();
        if (!id) return;
        bool ok = false;
        const QString reason = ui::askText(this, tr("Reverse expense"), tr("Reason:"), &ok);
        if (ok && ui::check(this, ExpenseService::reverse(id, reason))) reload();
    });
    connect(csv, &QPushButton::clicked, this, [this] { ui::exportModelCsv(this, m_table->model(), "expenses.csv", true); });
    reload();
}

void ExpensesPage::reload()
{
    m_table->setQuery("SELECT e.id, e.expense_date, c.name, e.description, e.method, e.status, "
                      "CASE WHEN e.status='reversal' THEN -e.amount ELSE e.amount END, COALESCE(e.reference,''), COALESCE(u.full_name,''), "
                      "COALESCE(e.notes,'') FROM expenses e JOIN expense_categories c ON c.id=e.category_id LEFT JOIN users u ON u.id=e.user_id "
                      "WHERE e.expense_date BETWEEN ? AND ? ORDER BY e.expense_date DESC, e.id DESC",
                      {m_range->from(), m_range->to()},
                      {"id", tr("Date"), tr("Category"), tr("Description"), tr("Method"), tr("Status"), tr("Amount"), tr("Reference"), tr("User"), tr("Notes")});
    const Paisa total = scalar("SELECT COALESCE(SUM(CASE WHEN status='reversal' THEN -amount ELSE amount END),0) FROM expenses "
                               "WHERE expense_date BETWEEN ? AND ?", {m_range->from(), m_range->to()}, 0).toLongLong();
    m_total->setText(tr("Total %1").arg(Money::format(total)));
}

void ExpensesPage::add()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Add expense"));
    dlg.setMinimumWidth(440);
    auto *form = new QFormLayout(&dlg);
    auto *category = new QComboBox(&dlg);
    category->setEditable(true);
    QSqlQuery q(Db::get(Db::Pharmacy));
    q.exec("SELECT name FROM expense_categories ORDER BY name");
    QStringList cats;
    while (q.next()) cats << q.value(0).toString();
    for (const QString &d : {tr("Rent"), tr("Electricity"), tr("Salaries"), tr("Internet and phone"), tr("Tea and refreshments"), tr("Repairs"),
                             tr("Transport"), tr("Other")})
        if (!cats.contains(d, Qt::CaseInsensitive)) cats << d;
    category->addItems(cats);
    auto *date = ui::dateEdit(QDate::currentDate(), &dlg);
    auto *desc = new QLineEdit(&dlg);
    auto *amount = new ui::MoneyEdit(&dlg);
    auto *method = new QComboBox(&dlg);
    for (const QString &m : PaymentMethods::all())
        if (m != "customer_credit") method->addItem(PaymentMethods::label(m), m);
    auto *ref = new QLineEdit(&dlg);
    auto *notes = new QLineEdit(&dlg);
    form->addRow(tr("Category"), category);
    form->addRow(tr("Date"), date);
    form->addRow(tr("Description"), desc);
    form->addRow(tr("Amount"), amount);
    form->addRow(tr("Paid by"), method);
    form->addRow(tr("Reference"), ref);
    form->addRow(tr("Notes"), notes);
    form->addRow(ui::muted(tr("Cash expenses are taken from your open cash shift."), &dlg));
    auto *ok = ui::button(tr("Save"), &dlg, true);
    form->addRow(ok);
    connect(ok, &QPushButton::clicked, &dlg, [&] {
        const qint64 cat = ExpenseService::categoryId(category->currentText().trimmed());
        if (!cat) { ui::error(&dlg, tr("Enter a category.")); return; }
        if (ui::check(&dlg, ExpenseService::add(cat, date->date().toString(Qt::ISODate), desc->text(), amount->value(),
                                                method->currentData().toString(), ref->text(), notes->text())))
            dlg.accept();
    });
    if (dlg.exec() == QDialog::Accepted) reload();
}

// ---------------- Cash shifts ----------------

ShiftsPage::ShiftsPage(AppContext *, QWidget *parent) : QWidget(parent)
{
    auto f = ui::page(this, tr("Cash shift"));
    auto *open = ui::button(tr("Open shift"), this, true);
    auto *payIn = ui::button(tr("Cash in"), this);
    auto *payOut = ui::button(tr("Cash out"), this);
    auto *close = ui::button(tr("Close shift"), this);
    for (auto *b : {open, payIn, payOut, close}) {
        b->setEnabled(ui::can("shifts.own"));
        f.actions->addWidget(b);
    }
    auto *card = new QFrame(this);
    card->setProperty("role", "card");
    auto *cv = new QVBoxLayout(card);
    m_status = new QLabel(card);
    m_status->setProperty("role", "h2");
    m_expected = new QLabel(card);
    m_expected->setProperty("role", "stat");
    cv->addWidget(m_status);
    cv->addWidget(m_expected);
    f.body->addWidget(card);
    m_moves = new ui::SqlTable(this);
    m_moves->format()->money = {3};
    f.body->addWidget(m_moves, 1);
    auto *hist = new QLabel(ui::can("shifts.manage") ? tr("All shifts") : tr("My shifts"), this);
    hist->setProperty("role", "h2");
    f.body->addWidget(hist);
    m_history = new ui::SqlTable(this);
    m_history->format()->money = {4, 5, 6, 7};
    f.body->addWidget(m_history, 1);
    auto money = [this](const QString &title, const QString &label, Paisa *out) {
        bool ok = false;
        const QString v = QInputDialog::getText(this, title, label, QLineEdit::Normal, {}, &ok);
        if (!ok) return false;
        if (!Money::parse(v, out)) { ui::error(this, tr("Enter a valid amount.")); return false; }
        return true;
    };
    connect(open, &QPushButton::clicked, this, [=, this] {
        Paisa p = 0;
        if (money(tr("Open shift"), tr("Cash in the drawer now:"), &p) && ui::check(this, ShiftService::open(p))) reload();
    });
    auto move = [=, this](const QString &kind) {
        Paisa p = 0;
        if (!money(kind == "deposit" ? tr("Cash in") : tr("Cash out"), tr("Amount:"), &p)) return;
        bool ok = false;
        const QString note = ui::askText(this, tr("Note"), tr("Reason:"), &ok);
        if (ok && ui::check(this, ShiftService::cashMovement(kind, p, note))) reload();
    };
    connect(payIn, &QPushButton::clicked, this, [=] { move("deposit"); });
    connect(payOut, &QPushButton::clicked, this, [=] { move("payout"); });
    connect(close, &QPushButton::clicked, this, [=, this] {
        Paisa p = 0;
        if (!money(tr("Close shift"), tr("Count the cash in the drawer and enter the amount:"), &p)) return;
        bool ok = false;
        const QString note = ui::askText(this, tr("Close shift"), tr("Note:"), &ok);
        if (ok && ui::check(this, ShiftService::close(p, note))) reload();
    });
    connect(AppEvents::instance(), &AppEvents::pharmacyChanged, this, &ShiftsPage::reload);
    reload();
}

void ShiftsPage::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    reload();
}

void ShiftsPage::reload()
{
    const qint64 uid = Session::instance().userId();
    const qint64 shift = ShiftService::currentShift(uid);
    if (shift) {
        m_status->setText(tr("Shift open since %1").arg(scalar("SELECT datetime(opened_at,'localtime') FROM cash_shifts WHERE id=?", {shift}, "").toString()));
        m_expected->setText(tr("Expected cash in drawer: %1").arg(Money::format(ShiftService::expectedCash(shift))));
    } else {
        m_status->setText(tr("No open shift"));
        m_expected->setText(QString());
    }
    m_moves->setQuery("SELECT id, datetime(ts,'localtime'), kind, amount, COALESCE(ref_type,'') || ' ' || COALESCE(ref_id,''), COALESCE(note,'') "
                      "FROM cash_movements WHERE shift_id=? ORDER BY id DESC", {shift},
                      {"id", tr("Time"), tr("Type"), tr("Amount"), tr("Reference"), tr("Note")});
    const bool all = ui::can("shifts.manage");
    m_history->setQuery("SELECT s.id, u.full_name, datetime(s.opened_at,'localtime'), COALESCE(datetime(s.closed_at,'localtime'),''), s.opening_cash, "
                        "s.expected_cash, s.actual_cash, s.variance, s.status FROM cash_shifts s JOIN users u ON u.id=s.user_id "
                        "WHERE (?=1 OR s.user_id=?) ORDER BY s.id DESC LIMIT 500", {all ? 1 : 0, uid},
                        {"id", tr("User"), tr("Opened"), tr("Closed"), tr("Opening cash"), tr("Expected"), tr("Counted"), tr("Difference"), tr("Status")});
}
}
