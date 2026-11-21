#include "AddToPharmacyDialog.h"
#include "db/Database.h"
#include "refdata/CatalogImporter.h"
#include "refdata/PharmacyCatalog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSqlQuery>
#include <QTableWidget>
#include <QVBoxLayout>

namespace mcp {

AddToPharmacyDialog::AddToPharmacyDialog(const MedicineRecord &ref, QWidget *parent) : QDialog(parent), m_ref(ref)
{
    setWindowTitle(tr("Add to My Pharmacy"));
    resize(760, 640);
    auto *v = new QVBoxLayout(this);

    auto *refBox = new QGroupBox(tr("From the Pakistan medicine reference list"), this);
    auto *rf = new QFormLayout(refBox);
    auto add = [&](const QString &k, const QString &val) {
        auto *l = new QLabel(val.isEmpty() ? tr("Not available") : val, refBox);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        rf->addRow(k, l);
    };
    add(tr("Brand"), ref.brandName);
    add(tr("Urdu brand name"), ref.brandNameUr);
    add(tr("Generic name"), ref.genericName);
    add(tr("Strength"), ref.strength);
    add(tr("Dosage form"), ref.dosageForm);
    add(tr("Manufacturer"), ref.manufacturer);
    add(tr("DRAP registration number"), ref.registrationNo);
    v->addWidget(refBox);

    auto *localBox = new QGroupBox(tr("Your pharmacy settings"), this);
    auto *lf = new QFormLayout(localBox);
    m_purchase = new QDoubleSpinBox(localBox);
    m_sale = new QDoubleSpinBox(localBox);
    for (auto *s : {m_purchase, m_sale}) { s->setRange(0, 10000000); s->setDecimals(2); s->setPrefix("Rs "); s->setSpecialValueText(tr("Not set")); }
    m_pack = new QLineEdit(ref.packSize, localBox);
    m_units = new QSpinBox(localBox);
    m_units->setRange(1, 100000);
    m_barcode = new QLineEdit(ref.barcode, localBox);
    m_barcode->setPlaceholderText(tr("Scan or type the barcode on the pack"));
    m_supplier = new QComboBox(localBox);
    m_supplier->addItem(tr("No supplier"), 0);
    QSqlQuery q(Db::get(Db::Pharmacy));
    q.exec("SELECT id, name FROM suppliers ORDER BY name");
    while (q.next()) m_supplier->addItem(q.value(1).toString(), q.value(0));
    m_rack = new QLineEdit(localBox);
    m_minStock = new QSpinBox(localBox);
    m_minStock->setRange(0, 1000000);
    m_rx = new QCheckBox(tr("Prescription required"), localBox);
    m_rx->setChecked(ref.rxClass.contains("rx", Qt::CaseInsensitive) || ref.rxClass.contains("prescription", Qt::CaseInsensitive));
    lf->addRow(tr("Purchase price"), m_purchase);
    lf->addRow(tr("Sale price"), m_sale);
    lf->addRow(tr("Pack size"), m_pack);
    lf->addRow(tr("Units per pack"), m_units);
    lf->addRow(tr("Barcode"), m_barcode);
    lf->addRow(tr("Supplier"), m_supplier);
    lf->addRow(tr("Rack or shelf"), m_rack);
    lf->addRow(tr("Minimum stock"), m_minStock);
    lf->addRow(QString(), m_rx);
    v->addWidget(localBox);

    auto *stockBox = new QGroupBox(tr("Opening stock (optional)"), this);
    auto *sv = new QVBoxLayout(stockBox);
    auto *hint = new QLabel(tr("No stock is created unless you add a batch here."), stockBox);
    hint->setProperty("role", "muted");
    sv->addWidget(hint);
    m_batches = new QTableWidget(0, 3, stockBox);
    m_batches->setHorizontalHeaderLabels({tr("Batch number"), tr("Expiry (YYYY-MM-DD)"), tr("Quantity")});
    m_batches->horizontalHeader()->setStretchLastSection(true);
    m_batches->setMaximumHeight(130);
    sv->addWidget(m_batches);
    auto *bh = new QHBoxLayout;
    auto *addBatch = new QPushButton(tr("Add batch"), stockBox);
    auto *removeBatch = new QPushButton(tr("Remove batch"), stockBox);
    bh->addWidget(addBatch);
    bh->addWidget(removeBatch);
    bh->addStretch();
    sv->addLayout(bh);
    v->addWidget(stockBox);
    connect(addBatch, &QPushButton::clicked, this, [this] { m_batches->insertRow(m_batches->rowCount()); });
    connect(removeBatch, &QPushButton::clicked, this, [this] { if (m_batches->currentRow() >= 0) m_batches->removeRow(m_batches->currentRow()); });

    m_error = new QLabel(this);
    m_error->setStyleSheet("color:#b42318");
    m_error->setWordWrap(true);
    v->addWidget(m_error);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    auto *ok = new QPushButton(tr("Add to My Pharmacy"), this);
    ok->setProperty("role", "primary");
    auto *cancel = new QPushButton(tr("Cancel"), this);
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    v->addLayout(buttons);
    connect(ok, &QPushButton::clicked, this, &AddToPharmacyDialog::save);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

void AddToPharmacyDialog::save()
{
    LocalMedicineSettings l;
    if (m_purchase->value() > 0) l.purchasePrice = Money::fromInput(m_purchase->value());
    if (m_sale->value() > 0) l.salePrice = Money::fromInput(m_sale->value());
    l.packSize = m_pack->text().trimmed();
    l.unitsPerPack = m_units->value();
    l.barcode = m_barcode->text().trimmed();
    l.supplierId = m_supplier->currentData().toLongLong();
    l.rackLocation = m_rack->text().trimmed();
    l.minStock = m_minStock->value();
    l.rxRequired = m_rx->isChecked();
    for (int r = 0; r < m_batches->rowCount(); ++r) {
        auto cell = [&](int c) { auto *it = m_batches->item(r, c); return it ? it->text().trimmed() : QString(); };
        if (cell(0).isEmpty() && cell(1).isEmpty() && cell(2).isEmpty()) continue;
        l.openingBatches.append({cell(0), QDate::fromString(cell(1), Qt::ISODate), cell(2).toInt()});
    }
    if (l.salePrice.isValid() && l.purchasePrice.isValid() && l.salePrice.toLongLong() < l.purchasePrice.toLongLong()) {
        if (QMessageBox::question(this, windowTitle(), tr("Sale price is lower than purchase price. Save anyway?"))
            != QMessageBox::Yes) return;
    }
    const auto res = PharmacyCatalog::addFromReference(Db::get(Db::Pharmacy), m_ref,
                                                       CatalogImporter::currentVersion(Db::get(Db::Reference)), l);
    if (!res.ok) { m_error->setText(res.error); return; }
    m_created = res.medicineId;
    accept();
}
}
