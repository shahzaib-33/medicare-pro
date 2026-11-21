#include "PosPage.h"
#include "ReceiptDialog.h"
#include "Ui.h"
#include "Theme.h"
#include "db/Database.h"
#include <QDate>
#include "net/ConnectivityMonitor.h"
#include "services/AuthService.h"
#include "services/InventoryService.h"
#include "services/SettingsService.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QListWidget>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QSqlQuery>
#include <QTableWidget>
#include <QTimer>

namespace mcp {

// The batch the sale will take first (first expiry, first out). "+" means the quantity spans more batches.
static QPair<QString, QString> fefoBatch(qint64 medicineId, int qty)
{
    QSqlQuery q(Db::get(Db::Pharmacy));
    q.prepare("SELECT batch_no, expiry_date, qty_available FROM medicine_batches WHERE medicine_id=? AND status='active' "
              "AND qty_available > 0 AND expiry_date > date('now','localtime') ORDER BY expiry_date, id LIMIT 1");
    q.addBindValue(medicineId);
    if (!q.exec() || !q.next()) return {};
    QString batch = q.value(0).toString();
    if (q.value(2).toInt() < qty) batch += " +";
    return {batch, q.value(1).toString()};
}

PosPage::PosPage(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    auto f = ui::page(this, tr("Point of Sale"));
    m_shiftInfo = ui::muted({}, this);
    f.actions->addWidget(m_shiftInfo);
    auto *resumeBtn = ui::button(tr("Held bills (F7)"), this);
    auto *reprint = ui::button(tr("Last receipt"), this);
    f.actions->addWidget(resumeBtn);
    f.actions->addWidget(reprint);

    auto *split = new QSplitter(Qt::Horizontal, this);
    auto *left = new QWidget(split);
    auto *lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    m_search = new QLineEdit(left);
    m_search->setPlaceholderText(tr("Scan barcode or type medicine name, generic or Urdu name (F2)"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumHeight(Theme::isClassic() ? 0 : 38);
    m_search->setPlaceholderText(tr("Medicine name, generic or Urdu name (F2)"));
    m_barcode = new QLineEdit(left);
    m_barcode->setPlaceholderText(tr("Barcode (F4)"));
    m_barcode->setMinimumHeight(Theme::isClassic() ? 0 : 38);
    m_barcode->setMaximumWidth(220);
    auto *searchRow = new QHBoxLayout;
    auto *searchLabel = new QLabel(tr("&Search:"), left);
    searchLabel->setBuddy(m_search);
    auto *barcodeLabel = new QLabel(tr("&Barcode:"), left);
    barcodeLabel->setBuddy(m_barcode);
    searchRow->addWidget(searchLabel);
    searchRow->addWidget(m_search, 1);
    searchRow->addWidget(barcodeLabel);
    searchRow->addWidget(m_barcode);
    lv->addLayout(searchRow);
    m_results = new QTableWidget(0, 5, left);
    m_results->setHorizontalHeaderLabels({tr("Medicine"), tr("Generic"), tr("Price"), tr("Stock"), tr("Rack")});
    m_results->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_results->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_results->verticalHeader()->hide();
    m_results->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_results->setMaximumHeight(200);
    m_results->hide();
    lv->addWidget(m_results);
    m_lines = new QTableWidget(0, 9, left);
    m_lines->setHorizontalHeaderLabels({tr("Medicine"), tr("Batch"), tr("Expiry"), tr("Qty"), tr("Price"), tr("Discount"), tr("Tax"), tr("Total"), QString()});
    m_lines->verticalHeader()->hide();
    m_lines->verticalHeader()->setDefaultSectionSize(Theme::isClassic() ? 24 : 36);
    m_lines->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_lines->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_lines->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lv->addWidget(m_lines, 1);
    m_lastSale = ui::muted({}, left);
    lv->addWidget(m_lastSale);

    auto *right = new QFrame(split);
    right->setProperty("role", "card");
    right->setMinimumWidth(340);
    auto *rv = new QVBoxLayout(right);
    auto *form = new QFormLayout;
    m_customer = new QComboBox(right);
    m_customer->setEditable(false);
    m_rx = new QComboBox(right);
    form->addRow(tr("Customer"), m_customer);
    form->addRow(tr("Prescription"), m_rx);
    rv->addLayout(form);
    auto *tot = new QFormLayout;
    m_subtotal = new QLabel(right);
    m_discount = new QLabel(right);
    m_tax = new QLabel(right);
    m_total = new QLabel(right);
    m_total->setProperty("role", "stat");
    if (Theme::isClassic()) m_total->setObjectName("posTotal");
    m_billDiscount = new ui::MoneyEdit(right);
    m_billDiscount->setEnabled(ui::can("pos.discount"));
    for (auto *l : {m_subtotal, m_discount, m_tax, m_total}) l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    tot->addRow(tr("Subtotal"), m_subtotal);
    tot->addRow(tr("Bill discount"), m_billDiscount);
    tot->addRow(tr("Total discount"), m_discount);
    tot->addRow(tr("Sales tax"), m_tax);
    tot->addRow(tr("Total"), m_total);
    rv->addLayout(tot);

    auto *payTitle = new QLabel(tr("Payments"), right);
    payTitle->setProperty("role", "h2");
    rv->addWidget(payTitle);
    m_payments = new QTableWidget(0, 4, right);
    m_payments->setHorizontalHeaderLabels({tr("Method"), tr("Amount"), tr("Reference"), QString()});
    m_payments->verticalHeader()->hide();
    m_payments->verticalHeader()->setDefaultSectionSize(34);
    m_payments->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_payments->setMaximumHeight(170);
    rv->addWidget(m_payments);
    auto *quick = new QHBoxLayout;
    auto *addPay = ui::button(tr("Add payment"), right);
    auto *exact = ui::button(tr("Exact cash"), right);
    quick->addWidget(addPay);
    quick->addWidget(exact);
    rv->addLayout(quick);
    auto *pf = new QFormLayout;
    m_cashReceived = new ui::MoneyEdit(right);
    m_cashReceived->setPlaceholderText(tr("Cash received (F8)"));
    pf->addRow(tr("Cash received"), m_cashReceived);
    m_paid = new QLabel(right);
    m_change = new QLabel(right);
    m_change->setProperty("role", "h2");
    if (Theme::isClassic()) m_change->setObjectName("posChange");
    for (auto *l : {m_paid, m_change}) l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    pf->addRow(tr("Received"), m_paid);
    pf->addRow(tr("Change or due"), m_change);
    rv->addLayout(pf);
    rv->addStretch();
    auto *payBtn = ui::button(tr("Save sale (F9)"), right);
    auto *printBtn = ui::button(tr("Print Invoice (F12)"), right, true);
    printBtn->setMinimumHeight(Theme::isClassic() ? 30 : 44);
    auto *holdBtn = ui::button(tr("Hold (F6)"), right);
    auto *cancelBtn = ui::button(tr("Cancel bill"), right);
    auto *bh = new QHBoxLayout;
    bh->addWidget(holdBtn);
    bh->addWidget(cancelBtn);
    bh->addWidget(payBtn);
    rv->addLayout(bh);
    rv->addWidget(printBtn);
    split->addWidget(left);
    split->addWidget(right);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    f.body->addWidget(split, 1);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(ctx->perf.searchDebounceMs());
    connect(m_search, &QLineEdit::textEdited, m_debounce, qOverload<>(&QTimer::start));
    connect(m_debounce, &QTimer::timeout, this, &PosPage::search);
    connect(m_search, &QLineEdit::returnPressed, this, [this] {
        const QString text = m_search->text().trimmed();
        if (text.isEmpty()) return;
        int factor = 1;
        if (const qint64 id = InventoryService::findByBarcode(text, &factor)) {
            addMedicine(id, factor);
            m_search->clear();
            m_results->hide();
            return;
        }
        m_debounce->stop();
        search();
        if (m_results->rowCount() == 1) addMedicine(m_results->item(0, 0)->data(Qt::UserRole).toLongLong(), 1);
        else if (m_results->rowCount() > 1) { m_results->setFocus(); m_results->selectRow(0); }
    });
    auto pickResult = [this] {
        const int r = m_results->currentRow();
        if (r < 0) return;
        addMedicine(m_results->item(r, 0)->data(Qt::UserRole).toLongLong(), 1);
    };
    connect(m_results, &QTableWidget::cellDoubleClicked, this, pickResult);
    auto *enter = new QShortcut(QKeySequence(Qt::Key_Return), m_results, nullptr, nullptr, Qt::WidgetShortcut);
    connect(enter, &QShortcut::activated, this, pickResult);
    connect(m_billDiscount, &ui::MoneyEdit::valueChanged, this, [this](Paisa p) { m_cart.billDiscount = p; recalc(); });
    connect(m_customer, &QComboBox::currentIndexChanged, this, [this] { m_cart.customerId = m_customer->currentData().toLongLong(); });
    connect(m_rx, &QComboBox::currentIndexChanged, this, [this] { m_cart.prescriptionId = m_rx->currentData().toLongLong(); });
    connect(addPay, &QPushButton::clicked, this, [this] {
        const CartTotals t = SalesService::compute(currentCart());
        addPayment("card", qMax<Paisa>(0, t.total - t.paid - t.credit));
    });
    connect(exact, &QPushButton::clicked, this, [this] {
        m_cart.payments.clear();
        addPayment("cash", SalesService::compute(m_cart).total);
    });
    connect(payBtn, &QPushButton::clicked, this, [this] { pay(false); });
    connect(printBtn, &QPushButton::clicked, this, [this] { pay(true); });
    connect(m_barcode, &QLineEdit::returnPressed, this, [this] {
        const QString code = m_barcode->text().trimmed();
        if (code.isEmpty()) return;
        int factor = 1;
        if (const qint64 id = InventoryService::findByBarcode(code, &factor)) addMedicine(id, factor);
        else ui::error(this, tr("No medicine has the barcode %1.").arg(code));
        m_barcode->clear();
        m_barcode->setFocus();
    });
    connect(m_cashReceived, &ui::MoneyEdit::valueChanged, this, [this](Paisa v) {
        // Cash received replaces a single cash payment. Split payments stay in the table.
        const bool onlyCash = m_cart.payments.isEmpty() || (m_cart.payments.size() == 1 && m_cart.payments[0].method == "cash");
        if (!onlyCash) return;
        if (v > 0) m_cart.payments = {{"cash", v, {}}};
        else m_cart.payments.clear();
        rebuildCart();
    });
    connect(holdBtn, &QPushButton::clicked, this, &PosPage::hold);
    connect(cancelBtn, &QPushButton::clicked, this, [this] { clearBill(true); });
    connect(resumeBtn, &QPushButton::clicked, this, &PosPage::resume);
    connect(reprint, &QPushButton::clicked, this, [this] {
        if (!m_lastSaleId) { ui::info(this, tr("No sale yet in this session.")); return; }
        ReceiptDialog(m_lastSaleId, false, this).exec();
    });
    auto key = [this](QKeySequence k, auto fn) {
        auto *s = new QShortcut(k, this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
        connect(s, &QShortcut::activated, this, fn);
    };
    key(QKeySequence(Qt::Key_F2), [this] { m_search->setFocus(); m_search->selectAll(); });
    key(QKeySequence(Qt::Key_F6), [this] { hold(); });
    key(QKeySequence(Qt::Key_F7), [this] { resume(); });
    key(QKeySequence(Qt::Key_F9), [this] { pay(false); });
    key(QKeySequence(Qt::Key_F12), [this] { pay(true); });
    key(QKeySequence(Qt::Key_F4), [this] { m_barcode->setFocus(); m_barcode->selectAll(); });
    key(QKeySequence(Qt::Key_F8), [this] { m_cashReceived->setFocus(); m_cashReceived->selectAll(); });
    key(QKeySequence(Qt::Key_Delete), [this] {
        const int r = m_lines->currentRow();
        if (r >= 0 && r < m_cart.lines.size()) { m_cart.lines.remove(r); rebuildCart(); }
    });
    connect(AppEvents::instance(), &AppEvents::pharmacyChanged, this, &PosPage::loadCustomers);
    loadCustomers();
    rebuildCart();
}

void PosPage::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    loadCustomers();
    const qint64 shift = ShiftService::currentShift(Session::instance().userId());
    m_shiftInfo->setText(shift ? tr("Shift open") : tr("No open cash shift"));
    m_search->setFocus();
}

void PosPage::loadCustomers()
{
    QSignalBlocker b1(m_customer), b2(m_rx);
    const qint64 cur = m_cart.customerId, rx = m_cart.prescriptionId;
    m_customer->clear();
    m_customer->addItem(tr("Walk in customer"), 0);
    QSqlQuery q(Db::get(Db::Pharmacy));
    q.exec("SELECT id, name || COALESCE(' (' || phone || ')','') FROM customers WHERE active=1 ORDER BY name");
    while (q.next()) m_customer->addItem(q.value(1).toString(), q.value(0));
    m_customer->setCurrentIndex(qMax(0, m_customer->findData(cur)));
    m_rx->clear();
    m_rx->addItem(tr("None"), 0);
    q.exec("SELECT id, ref_no || ' ' || COALESCE(patient_name,'') FROM prescriptions WHERE status='approved' ORDER BY id DESC LIMIT 200");
    while (q.next()) m_rx->addItem(q.value(1).toString(), q.value(0));
    m_rx->setCurrentIndex(qMax(0, m_rx->findData(rx)));
}

void PosPage::search()
{
    const QString text = m_search->text().trimmed();
    m_results->setRowCount(0);
    if (text.size() < 2) { m_results->hide(); return; }
    QSqlQuery q(Db::get(Db::Pharmacy));
    q.prepare("SELECT m.id, m.brand_name || ' ' || COALESCE(m.strength,'') || ' ' || COALESCE(m.dosage_form,''), COALESCE(m.generic_name,''), "
              "m.sale_price, COALESCE((SELECT SUM(qty_available) FROM medicine_batches b WHERE b.medicine_id=m.id AND b.status='active' "
              "AND b.expiry_date > date('now','localtime')),0), COALESCE(m.rack_location,''), m.rx_required FROM medicines m "
              "WHERE m.archived=0 AND (m.brand_name LIKE ? OR m.generic_name LIKE ? OR m.brand_name_ur LIKE ? OR m.barcode=?) "
              "ORDER BY m.brand_name LIMIT 40");
    const QString like = "%" + text + "%";
    q.addBindValue(text.size() >= 3 ? like : text + "%");
    q.addBindValue(like);
    q.addBindValue(like);
    q.addBindValue(text);
    q.exec();
    while (q.next()) {
        const int r = m_results->rowCount();
        m_results->insertRow(r);
        auto *name = new QTableWidgetItem(q.value(1).toString().simplified() + (q.value(6).toBool() ? " (Rx)" : QString()));
        name->setData(Qt::UserRole, q.value(0));
        m_results->setItem(r, 0, name);
        m_results->setItem(r, 1, new QTableWidgetItem(q.value(2).toString()));
        m_results->setItem(r, 2, new QTableWidgetItem(Money::format(q.value(3).toLongLong(), false)));
        auto *stock = new QTableWidgetItem(QString::number(q.value(4).toInt()));
        if (q.value(4).toInt() <= 0) stock->setForeground(QColor("#b42318"));
        m_results->setItem(r, 3, stock);
        m_results->setItem(r, 4, new QTableWidgetItem(q.value(5).toString()));
    }
    m_results->setVisible(m_results->rowCount() > 0);
}

void PosPage::addMedicine(qint64 id, int qty)
{
    for (auto &l : m_cart.lines) {
        if (l.medicineId == id) { l.qty += qty; rebuildCart(); m_search->clear(); m_results->hide(); m_search->setFocus(); return; }
    }
    CartLine line;
    QString err;
    if (!SalesService::fillLine(id, qty, &line, &err)) { ui::error(this, err); return; }
    const int stock = InventoryService::saleableStock(id);
    if (stock < qty) {
        ui::error(this, stock <= 0 ? tr("%1 is out of stock or only expired stock is left.").arg(line.name)
                                   : tr("Only %1 units of %2 can be sold.").arg(stock).arg(line.name));
        return;
    }
    m_cart.lines.append(line);
    rebuildCart();
    m_search->clear();
    m_results->hide();
    m_search->setFocus();
}

void PosPage::rebuildCart()
{
    m_updating = true;
    m_lines->setRowCount(m_cart.lines.size());
    const bool priceEdit = ui::can("pos.price_override");
    const bool discEdit = ui::can("pos.discount");
    for (int r = 0; r < m_cart.lines.size(); ++r) {
        const CartLine &l = m_cart.lines[r];
        auto *name = new QTableWidgetItem(l.name + (l.rxRequired ? " (Rx)" : QString()));
        m_lines->setItem(r, 0, name);
        const auto batch = fefoBatch(l.medicineId, l.qty);
        m_lines->setItem(r, 1, new QTableWidgetItem(batch.first));
        auto *exp = new QTableWidgetItem(batch.second);
        if (!batch.second.isEmpty() && QDate::fromString(batch.second, Qt::ISODate) <= QDate::currentDate().addDays(30)) exp->setForeground(QColor("#b42318"));
        m_lines->setItem(r, 2, exp);
        auto *qty = new QSpinBox(m_lines);
        qty->setRange(1, 100000);
        qty->setValue(l.qty);
        m_lines->setCellWidget(r, 3, qty);
        connect(qty, &QSpinBox::valueChanged, this, [this, r](int v) {
            if (r >= m_cart.lines.size()) return;
            const int stock = InventoryService::saleableStock(m_cart.lines[r].medicineId);
            if (v > stock) { ui::error(this, tr("Only %1 units can be sold.").arg(stock)); QTimer::singleShot(0, this, [this] { rebuildCart(); }); return; }
            m_cart.lines[r].qty = v;
            recalc();
        });
        auto *price = new ui::MoneyEdit(m_lines);
        price->setValue(l.unitPrice);
        price->setEnabled(priceEdit);
        m_lines->setCellWidget(r, 4, price);
        connect(price, &ui::MoneyEdit::valueChanged, this, [this, r](Paisa p) { if (r < m_cart.lines.size()) { m_cart.lines[r].unitPrice = p; recalc(); } });
        auto *disc = new ui::MoneyEdit(m_lines);
        disc->setValue(l.discount);
        disc->setEnabled(discEdit);
        m_lines->setCellWidget(r, 5, disc);
        connect(disc, &ui::MoneyEdit::valueChanged, this, [this, r](Paisa p) { if (r < m_cart.lines.size()) { m_cart.lines[r].discount = p; recalc(); } });
        m_lines->setItem(r, 6, new QTableWidgetItem);
        m_lines->setItem(r, 7, new QTableWidgetItem);
        auto *rm = ui::button(QStringLiteral("✕"), m_lines);
        rm->setToolTip(tr("Remove"));
        m_lines->setCellWidget(r, 8, rm);
        connect(rm, &QPushButton::clicked, this, [this, r] { if (r < m_cart.lines.size()) { m_cart.lines.remove(r); rebuildCart(); } });
    }
    m_lines->setColumnWidth(1, 70);
    m_lines->setColumnWidth(2, 80);
    m_lines->setColumnWidth(3, 70);
    m_lines->setColumnWidth(4, 80);
    m_lines->setColumnWidth(5, 70);
    m_lines->setColumnWidth(6, 60);
    m_lines->setColumnWidth(7, 80);
    m_lines->setColumnWidth(8, 30);

    m_payments->setRowCount(m_cart.payments.size());
    for (int r = 0; r < m_cart.payments.size(); ++r) {
        const PaymentLine &p = m_cart.payments[r];
        auto *method = new QComboBox(m_payments);
        for (const QString &code : PaymentMethods::all()) method->addItem(PaymentMethods::label(code), code);
        method->setCurrentIndex(method->findData(p.method));
        m_payments->setCellWidget(r, 0, method);
        connect(method, &QComboBox::currentIndexChanged, this, [this, r, method] {
            if (r < m_cart.payments.size()) { m_cart.payments[r].method = method->currentData().toString(); recalc(); }
        });
        auto *amount = new ui::MoneyEdit(m_payments);
        amount->setValue(p.amount);
        m_payments->setCellWidget(r, 1, amount);
        connect(amount, &ui::MoneyEdit::valueChanged, this, [this, r](Paisa v) { if (r < m_cart.payments.size()) { m_cart.payments[r].amount = v; recalc(); } });
        auto *ref = new QLineEdit(p.reference, m_payments);
        ref->setPlaceholderText(tr("Txn ID"));
        m_payments->setCellWidget(r, 2, ref);
        connect(ref, &QLineEdit::textChanged, this, [this, r](const QString &s) { if (r < m_cart.payments.size()) m_cart.payments[r].reference = s; });
        auto *rm = ui::button(QStringLiteral("✕"), m_payments);
        m_payments->setCellWidget(r, 3, rm);
        connect(rm, &QPushButton::clicked, this, [this, r] { if (r < m_cart.payments.size()) { m_cart.payments.remove(r); rebuildCart(); } });
    }
    m_payments->setColumnWidth(1, 100);
    m_payments->setColumnWidth(3, 40);
    m_updating = false;
    recalc();
}

void PosPage::recalc()
{
    const CartTotals t = SalesService::compute(m_cart);
    for (int r = 0; r < m_cart.lines.size() && r < m_lines->rowCount(); ++r) {
        const CartLine &l = m_cart.lines[r];
        if (auto *it = m_lines->item(r, 6)) it->setText(Money::format(Money::percentOf(Money::times(l.unitPrice, l.qty) - l.discount, l.taxBp), false));
        if (auto *it = m_lines->item(r, 7)) it->setText(Money::format(t.lineTotals.value(r), false));
        const auto batch = fefoBatch(l.medicineId, l.qty);
        if (auto *it = m_lines->item(r, 1)) it->setText(batch.first);
    }
    m_subtotal->setText(Money::format(t.subtotal));
    m_discount->setText(Money::format(t.lineDiscount + t.billDiscount));
    m_tax->setText(Money::format(t.tax));
    m_total->setText(Money::format(t.total));
    m_paid->setText(Money::format(t.paid + t.credit));
    if (t.due > 0) {
        m_change->setText(tr("Due %1").arg(Money::format(t.due)));
        m_change->setStyleSheet(Theme::isXp() ? "color:#c00000" : Theme::isClassic() ? "color:#ff5050" : "color:#b42318");
    } else {
        m_change->setText(tr("Change %1").arg(Money::format(t.change)));
        m_change->setStyleSheet(QString());
    }
}

void PosPage::addPayment(const QString &method, Paisa amount)
{
    m_cart.payments.append({method, amount, {}});
    rebuildCart();
}

Cart PosPage::currentCart() const { return m_cart; }

void PosPage::ensureShift()
{
    if (ShiftService::currentShift(Session::instance().userId()) || !ui::can("shifts.own")) return;
    bool ok = false;
    const QString v = QInputDialog::getText(this, tr("Open cash shift"),
                                            tr("No cash shift is open. Enter the cash in the drawer to open one:"), QLineEdit::Normal, "0", &ok);
    if (!ok) return;
    Paisa p = 0;
    if (!Money::parse(v, &p)) { ui::error(this, tr("Enter a valid amount.")); return; }
    if (ui::check(this, ShiftService::open(p))) m_shiftInfo->setText(tr("Shift open"));
}

void PosPage::pay(bool printInvoice)
{
    if (m_cart.lines.isEmpty()) { ui::info(this, tr("Add medicines to the bill first.")); return; }
    ensureShift();
    if (m_cart.payments.isEmpty()) {
        // Ask for cash received. Enter with an empty box means exact cash.
        const CartTotals t = SalesService::compute(m_cart);
        bool ok = false;
        const QString v = QInputDialog::getText(this, tr("Cash received"), tr("Total %1. Cash received:").arg(Money::format(t.total)),
                                                QLineEdit::Normal, Money::format(t.total, false).remove(','), &ok);
        if (!ok) return;
        Paisa p = 0;
        if (!Money::parse(v, &p)) { ui::error(this, tr("Enter a valid amount.")); return; }
        m_cart.payments = {{"cash", p, {}}};
    }
    const bool offline = m_ctx->network && !m_ctx->network->isOnline();
    const Result r = SalesService::post(m_cart, offline);
    if (!r.ok) { ui::error(this, r.error); rebuildCart(); return; }
    m_lastSaleId = r.id;
    const CartTotals t = SalesService::compute(m_cart);
    const QString invoice = scalar("SELECT invoice_no FROM sales WHERE id=?", {r.id}).toString();
    m_lastSale->setText(tr("Last sale %1, total %2, change %3").arg(invoice, Money::format(t.total), Money::format(t.change)));
    clearBill(false);
    emit AppEvents::instance()->pharmacyChanged();
    const PharmacyProfile p = SettingsService::profile();
    QString err;
    const bool direct = printInvoice || p.printAfterSale;
    if (!direct || !ReceiptDialog::printOriginalDirect(r.id, &err)) ReceiptDialog(r.id, false, this).exec();
    m_search->setFocus();
}

void PosPage::hold()
{
    if (m_cart.lines.isEmpty()) return;
    bool ok = false;
    const QString label = ui::askText(this, tr("Hold bill"), tr("Name or note for this bill:"), &ok);
    if (!ok) return;
    if (ui::check(this, SalesService::hold(m_cart, label))) clearBill(false);
}

void PosPage::resume()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Held bills"));
    dlg.resize(560, 360);
    auto *v = new QVBoxLayout(&dlg);
    auto *list = new ui::SqlTable(&dlg);
    list->setQuery("SELECT h.id, datetime(h.ts,'localtime'), COALESCE(h.label,''), COALESCE(u.full_name,'') FROM held_bills h "
                   "LEFT JOIN users u ON u.id=h.user_id ORDER BY h.id DESC", {}, {"id", tr("Time"), tr("Note"), tr("User")});
    v->addWidget(list);
    auto *h = new QHBoxLayout;
    auto *open = ui::button(tr("Resume"), &dlg, true);
    auto *discard = ui::button(tr("Discard"), &dlg);
    h->addWidget(discard);
    h->addStretch();
    h->addWidget(open);
    v->addLayout(h);
    auto doOpen = [&] {
        const qint64 id = list->currentId();
        if (!id) return;
        if (!m_cart.lines.isEmpty() && !ui::confirm(&dlg, tr("The current bill will be held first. Continue?"))) return;
        if (!m_cart.lines.isEmpty() && !ui::check(&dlg, SalesService::hold(m_cart, tr("Auto held")))) return;
        Cart c;
        if (!ui::check(&dlg, SalesService::resume(id, &c))) return;
        m_cart = c;
        m_cart.payments.clear();
        m_billDiscount->setValue(m_cart.billDiscount);
        loadCustomers();
        rebuildCart();
        dlg.accept();
    };
    connect(open, &QPushButton::clicked, &dlg, doOpen);
    connect(list, &ui::SqlTable::activatedId, &dlg, doOpen);
    connect(discard, &QPushButton::clicked, &dlg, [&] {
        if (list->currentId() && ui::confirm(&dlg, tr("Discard this held bill?")) && ui::check(&dlg, SalesService::discardHeld(list->currentId())))
            list->reload();
    });
    dlg.exec();
}

void PosPage::clearBill(bool ask)
{
    if (ask && !m_cart.lines.isEmpty() && !ui::confirm(this, tr("Cancel this bill? Nothing is saved."))) return;
    m_cart = Cart();
    {
        QSignalBlocker b(m_billDiscount);
        m_billDiscount->clear();
    }
    {
        QSignalBlocker b(m_cashReceived);
        m_cashReceived->clear();
    }
    m_customer->setCurrentIndex(0);
    m_rx->setCurrentIndex(0);
    rebuildCart();
}
}
