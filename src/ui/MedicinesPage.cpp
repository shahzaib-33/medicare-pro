#include "MedicinesPage.h"
#include "CatalogBrowserDialog.h"
#include "db/Database.h"
#include "refdata/PharmacyCatalog.h"
#include "refdata/TextNormalizer.h"
#include "refdata/CsvReader.h"
#include "services/InventoryService.h"
#include "services/AuthService.h"
#include "Ui.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QScrollArea>
#include <QSpinBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSqlQuery>
#include <QSqlQueryModel>
#include <QTableView>
#include <QTableWidget>
#include <QVBoxLayout>

namespace mcp {

MedicinesPage::MedicinesPage(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    auto f = ui::page(this, tr("Medicines"));
    auto *fromDb = ui::button(tr("Add Medicine from Pakistan Database"), this, true);
    auto *addNew = ui::button(tr("Add manually"), this);
    fromDb->setEnabled(ui::can("medicines.edit"));
    addNew->setEnabled(ui::can("medicines.edit"));
    f.actions->addWidget(addNew);
    f.actions->addWidget(fromDb);

    m_flagBanner = new QFrame(this);
    m_flagBanner->setObjectName("flagBanner");
    auto *fb = new QHBoxLayout(m_flagBanner);
    m_flagText = new QLabel(m_flagBanner);
    auto *review = new QPushButton(tr("Review"), m_flagBanner);
    fb->addWidget(m_flagText);
    fb->addStretch();
    fb->addWidget(review);
    f.body->addWidget(m_flagBanner);

    auto *bar = new QHBoxLayout;
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Search my medicines by name, generic, Urdu name or barcode"));
    m_filter->setClearButtonEnabled(true);
    m_archived = new QCheckBox(tr("Show archived"), this);
    bar->addWidget(m_filter, 1);
    bar->addWidget(m_archived);
    f.body->addLayout(bar);

    auto *tools = new QHBoxLayout;
    auto *edit = ui::button(tr("Edit"), this);
    auto *units = ui::button(tr("Units and barcodes"), this);
    auto *opening = ui::button(tr("Opening stock"), this);
    auto *archive = ui::button(tr("Archive or restore"), this);
    auto *del = ui::button(tr("Delete"), this);
    auto *imp = ui::button(tr("Import CSV"), this);
    auto *exp = ui::button(tr("Export CSV"), this);
    edit->setEnabled(ui::can("medicines.edit"));
    units->setEnabled(ui::can("medicines.edit"));
    opening->setEnabled(ui::can("inventory.adjust"));
    archive->setEnabled(ui::can("medicines.delete"));
    del->setEnabled(ui::can("medicines.delete"));
    imp->setEnabled(ui::can("medicines.edit"));
    for (auto *b : {edit, units, opening, archive, del}) tools->addWidget(b);
    tools->addStretch();
    tools->addWidget(imp);
    tools->addWidget(exp);
    f.body->addLayout(tools);

    m_table = new ui::SqlTable(this);
    m_table->format()->money = {7, 8};
    m_table->format()->numbers = {9, 10};
    m_table->format()->rowColor = [this](const QModelIndex &i) {
        const auto *m = m_table->sqlModel();
        const int stock = m->data(m->index(i.row(), 9)).toInt();
        const int min = m->data(m->index(i.row(), 10)).toInt();
        if (m->data(m->index(i.row(), 12)).toInt() == 1) return QColor("#8a8599");
        return (min > 0 && stock <= min) ? QColor("#b42318") : QColor();
    };
    f.body->addWidget(m_table, 1);

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(ctx->perf.searchDebounceMs());
    connect(m_filter, &QLineEdit::textChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_debounce, &QTimer::timeout, this, &MedicinesPage::reload);
    connect(m_archived, &QCheckBox::toggled, this, &MedicinesPage::reload);
    connect(fromDb, &QPushButton::clicked, this, &MedicinesPage::openCatalog);
    connect(addNew, &QPushButton::clicked, this, [this] { editMedicine(0); });
    connect(edit, &QPushButton::clicked, this, [this] { if (m_table->currentId()) editMedicine(m_table->currentId()); });
    connect(m_table, &ui::SqlTable::activatedId, this, [this](qint64 id) { if (ui::can("medicines.edit")) editMedicine(id); });
    connect(units, &QPushButton::clicked, this, [this] { if (m_table->currentId()) editUnits(m_table->currentId()); });
    connect(opening, &QPushButton::clicked, this, [this] { if (m_table->currentId()) openingStock(m_table->currentId()); });
    connect(archive, &QPushButton::clicked, this, [this] {
        const qint64 id = m_table->currentId();
        if (!id) return;
        const bool archived = m_table->currentValue(12).toInt() == 1;
        if (ui::check(this, InventoryService::archiveMedicine(id, !archived))) emit AppEvents::instance()->pharmacyChanged();
    });
    connect(del, &QPushButton::clicked, this, [this] {
        const qint64 id = m_table->currentId();
        if (!id || !ui::confirm(this, tr("Delete %1? This is only possible when it has no stock or history.").arg(m_table->currentValue(1).toString())))
            return;
        if (ui::check(this, InventoryService::deleteMedicine(id))) emit AppEvents::instance()->pharmacyChanged();
    });
    connect(imp, &QPushButton::clicked, this, &MedicinesPage::importCsv);
    connect(exp, &QPushButton::clicked, this, [this] { ui::exportModelCsv(this, m_table->model(), "medicines.csv", true); });
    connect(review, &QPushButton::clicked, this, &MedicinesPage::openReview);
    connect(AppEvents::instance(), &AppEvents::pharmacyChanged, this, &MedicinesPage::reload);
    connect(AppEvents::instance(), &AppEvents::referenceChanged, this, &MedicinesPage::reload);
    reload();
}

void MedicinesPage::reload()
{
    const QString f = m_filter->text().trimmed();
    const QString like = "%" + f + "%";
    m_table->setQuery(
        "SELECT m.id, m.brand_name, COALESCE(m.brand_name_ur,''), COALESCE(m.generic_name,''), COALESCE(m.strength,''), "
        "COALESCE(m.dosage_form,''), COALESCE(m.barcode,''), m.purchase_price, m.sale_price, "
        "COALESCE((SELECT SUM(qty_available) FROM medicine_batches b WHERE b.medicine_id=m.id AND b.status='active' "
        "AND b.expiry_date > date('now','localtime')),0), m.min_stock, COALESCE(m.rack_location,''), m.archived, "
        "CASE WHEN m.rx_required=1 THEN 'Rx' ELSE '' END, COALESCE(m.manufacturer,''), COALESCE(m.registration_no,'') "
        "FROM medicines m WHERE (m.archived=0 OR ?=1) AND (?='' OR m.brand_name LIKE ? OR m.generic_name LIKE ? "
        "OR m.brand_name_ur LIKE ? OR m.barcode=? OR m.id IN (SELECT medicine_id FROM medicine_barcodes WHERE barcode=?)) "
        "ORDER BY m.archived, m.brand_name LIMIT " + QString::number(m_ctx->perf.lite() ? 1000 : 5000),
        {m_archived->isChecked() ? 1 : 0, f, like, like, like, f, f},
        {"id", tr("Brand"), tr("Urdu name"), tr("Generic"), tr("Strength"), tr("Form"), tr("Barcode"), tr("Purchase price"), tr("Sale price"),
         tr("Stock"), tr("Minimum stock"), tr("Rack or shelf"), tr("Archived"), tr("Prescription"), tr("Manufacturer"), tr("Registration")});
    const int flags = PharmacyCatalog::openReviewFlags(Db::get(Db::Pharmacy));
    m_flagBanner->setVisible(flags > 0);
    m_flagText->setText(tr("%n medicine(s) changed in the official reference list and need your review.", nullptr, flags));
}

void MedicinesPage::editMedicine(qint64 id)
{
    QDialog dlg(this);
    dlg.setWindowTitle(id ? tr("Edit medicine") : tr("Add medicine"));
    dlg.resize(640, 640);
    auto *v = new QVBoxLayout(&dlg);
    auto *scroll = new QScrollArea(&dlg);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *inner = new QWidget;
    auto *form = new QFormLayout(inner);
    auto line = [&](const QString &label) { auto *e = new QLineEdit(inner); form->addRow(label, e); return e; };
    QLineEdit *brand = line(tr("Brand name"));
    QLineEdit *brandUr = line(tr("Urdu brand name"));
    QLineEdit *generic = line(tr("Generic name"));
    QLineEdit *strength = line(tr("Strength"));
    QLineEdit *form_ = line(tr("Dosage form"));
    QLineEdit *maker = line(tr("Manufacturer"));
    QLineEdit *category = line(tr("Category"));
    QLineEdit *reg = line(tr("DRAP registration number"));
    QLineEdit *pack = line(tr("Pack size"));
    QLineEdit *baseUnit = line(tr("Base unit (tablet, ml, piece)"));
    auto *units = new QSpinBox(inner);
    units->setRange(1, 100000);
    form->addRow(tr("Units per pack"), units);
    QLineEdit *barcode = line(tr("Barcode"));
    auto *cost = new ui::MoneyEdit(inner);
    auto *price = new ui::MoneyEdit(inner);
    form->addRow(tr("Purchase price per unit"), cost);
    form->addRow(tr("Sale price per unit"), price);
    auto *packInfo = ui::muted({}, inner);
    form->addRow(QString(), packInfo);
    auto *tax = new QDoubleSpinBox(inner);
    tax->setRange(0, 100);
    tax->setDecimals(2);
    tax->setSuffix(" %");
    form->addRow(tr("Sales tax"), tax);
    auto *supplier = new QComboBox(inner);
    supplier->addItem(tr("No supplier"), 0);
    QSqlQuery sq(Db::get(Db::Pharmacy));
    sq.exec("SELECT id, name FROM suppliers WHERE active=1 ORDER BY name");
    while (sq.next()) supplier->addItem(sq.value(1).toString(), sq.value(0));
    form->addRow(tr("Supplier"), supplier);
    QLineEdit *rack = line(tr("Rack or shelf"));
    auto *minStock = new QSpinBox(inner);
    minStock->setRange(0, 1000000);
    form->addRow(tr("Minimum stock"), minStock);
    auto *rx = new QCheckBox(tr("Prescription required"), inner);
    form->addRow(QString(), rx);
    scroll->setWidget(inner);
    v->addWidget(scroll, 1);
    baseUnit->setText("tablet");

    if (id) {
        QSqlQuery q(Db::get(Db::Pharmacy));
        q.prepare("SELECT brand_name, brand_name_ur, generic_name, strength, dosage_form, manufacturer, category, registration_no, pack_size, "
                  "base_unit, units_per_pack, barcode, purchase_price, sale_price, tax_rate_bp, supplier_id, rack_location, min_stock, rx_required "
                  "FROM medicines WHERE id=?");
        q.addBindValue(id);
        q.exec();
        if (q.next()) {
            const QList<QLineEdit *> texts = {brand, brandUr, generic, strength, form_, maker, category, reg, pack, baseUnit};
            for (int i = 0; i < texts.size(); ++i) texts[i]->setText(q.value(i).toString());
            units->setValue(q.value(10).toInt());
            barcode->setText(q.value(11).toString());
            cost->setValue(q.value(12).toLongLong());
            price->setValue(q.value(13).toLongLong());
            tax->setValue(q.value(14).toInt() / 100.0);
            supplier->setCurrentIndex(qMax(0, supplier->findData(q.value(15))));
            rack->setText(q.value(16).toString());
            minStock->setValue(q.value(17).toInt());
            rx->setChecked(q.value(18).toBool());
        }
    }
    auto updatePack = [=] {
        packInfo->setText(tr("Per pack of %1: purchase %2, sale %3").arg(units->value())
                              .arg(Money::format(Money::times(cost->value(), units->value())), Money::format(Money::times(price->value(), units->value()))));
    };
    connect(units, &QSpinBox::valueChanged, &dlg, updatePack);
    connect(cost, &ui::MoneyEdit::valueChanged, &dlg, updatePack);
    connect(price, &ui::MoneyEdit::valueChanged, &dlg, updatePack);
    updatePack();

    auto *ok = ui::button(tr("Save"), &dlg, true);
    v->addWidget(ok, 0, Qt::AlignRight);
    connect(ok, &QPushButton::clicked, &dlg, [&] {
        MedicineInput m;
        m.brandName = brand->text(); m.brandNameUr = brandUr->text(); m.genericName = generic->text(); m.strength = strength->text();
        m.dosageForm = form_->text(); m.manufacturer = maker->text(); m.category = category->text(); m.registrationNo = reg->text();
        m.packSize = pack->text(); m.baseUnit = baseUnit->text(); m.unitsPerPack = units->value(); m.barcode = barcode->text();
        m.purchasePrice = cost->value(); m.salePrice = price->value(); m.taxRateBp = qRound(tax->value() * 100);
        m.supplierId = supplier->currentData().toLongLong(); m.rackLocation = rack->text(); m.minStock = minStock->value();
        m.rxRequired = rx->isChecked();
        if (m.salePrice > 0 && m.salePrice < m.purchasePrice && !ui::confirm(&dlg, tr("Sale price is lower than purchase price. Save anyway?")))
            return;
        if (ui::check(&dlg, id ? InventoryService::updateMedicine(id, m) : InventoryService::addMedicine(m))) dlg.accept();
    });
    if (dlg.exec() == QDialog::Accepted) emit AppEvents::instance()->pharmacyChanged();
}

void MedicinesPage::editUnits(qint64 id)
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Units and barcodes"));
    dlg.resize(560, 460);
    auto *v = new QVBoxLayout(&dlg);
    v->addWidget(ui::muted(tr("Sell or buy in different units. Factor is how many base units are in one unit. "
                              "For example strip 10, box 100. A barcode can point to a unit, so scanning a box adds 100 units."), &dlg));
    auto *t = new QTableWidget(0, 2, &dlg);
    t->setHorizontalHeaderLabels({tr("Unit name"), tr("Factor")});
    t->horizontalHeader()->setStretchLastSection(true);
    QSqlQuery q(Db::get(Db::Pharmacy));
    q.prepare("SELECT unit_name, factor FROM medicine_units WHERE medicine_id=? ORDER BY factor");
    q.addBindValue(id);
    q.exec();
    while (q.next()) {
        const int r = t->rowCount();
        t->insertRow(r);
        t->setItem(r, 0, new QTableWidgetItem(q.value(0).toString()));
        t->setItem(r, 1, new QTableWidgetItem(q.value(1).toString()));
    }
    v->addWidget(t, 1);
    auto *h = new QHBoxLayout;
    auto *add = ui::button(tr("Add unit"), &dlg);
    auto *rm = ui::button(tr("Remove unit"), &dlg);
    h->addWidget(add);
    h->addWidget(rm);
    h->addStretch();
    v->addLayout(h);
    auto *bcForm = new QFormLayout;
    auto *bc = new QLineEdit(&dlg);
    auto *bcUnit = new QLineEdit(&dlg);
    bcUnit->setPlaceholderText(tr("Unit name, empty for base unit"));
    bcForm->addRow(tr("Extra barcode"), bc);
    bcForm->addRow(tr("Barcode unit"), bcUnit);
    v->addLayout(bcForm);
    auto *save = ui::button(tr("Save"), &dlg, true);
    v->addWidget(save, 0, Qt::AlignRight);
    connect(add, &QPushButton::clicked, &dlg, [t] { t->insertRow(t->rowCount()); });
    connect(rm, &QPushButton::clicked, &dlg, [t] { if (t->currentRow() >= 0) t->removeRow(t->currentRow()); });
    connect(save, &QPushButton::clicked, &dlg, [&] {
        QList<QPair<QString, int>> list;
        for (int r = 0; r < t->rowCount(); ++r) {
            const QString n = t->item(r, 0) ? t->item(r, 0)->text().trimmed() : QString();
            const int fct = t->item(r, 1) ? t->item(r, 1)->text().toInt() : 0;
            if (n.isEmpty() && fct == 0) continue;
            list.append({n, fct});
        }
        if (!ui::check(&dlg, InventoryService::setUnits(id, list))) return;
        if (!bc->text().trimmed().isEmpty() && !ui::check(&dlg, InventoryService::addBarcode(id, bc->text(), bcUnit->text()))) return;
        dlg.accept();
    });
    dlg.exec();
}

void MedicinesPage::openingStock(qint64 id)
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Opening stock for %1").arg(m_table->currentValue(1).toString()));
    auto *v = new QVBoxLayout(&dlg);
    v->addWidget(ui::muted(tr("Use this only for stock you already had before using MediCare Pro. New stock should come through Purchases."), &dlg));
    auto *form = new QFormLayout;
    auto *batch = new QLineEdit(&dlg);
    auto *expiry = ui::dateEdit(QDate::currentDate().addYears(1), &dlg);
    auto *qty = new QSpinBox(&dlg);
    qty->setRange(1, 10000000);
    auto *cost = new ui::MoneyEdit(&dlg);
    cost->setValue(scalar("SELECT COALESCE(purchase_price,0) FROM medicines WHERE id=?", {id}).toLongLong());
    form->addRow(tr("Batch number"), batch);
    form->addRow(tr("Expiry date"), expiry);
    form->addRow(tr("Quantity in base units"), qty);
    form->addRow(tr("Cost per unit"), cost);
    v->addLayout(form);
    auto *ok = ui::button(tr("Add stock"), &dlg, true);
    v->addWidget(ok, 0, Qt::AlignRight);
    connect(ok, &QPushButton::clicked, &dlg, [&] {
        if (ui::check(&dlg, InventoryService::openingStock(id, batch->text(), expiry->date().toString(Qt::ISODate), qty->value(), cost->value())))
            dlg.accept();
    });
    if (dlg.exec() == QDialog::Accepted) emit AppEvents::instance()->pharmacyChanged();
}

void MedicinesPage::importCsv()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import medicines"), {}, tr("CSV files (*.csv *.txt)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { ui::error(this, file.errorString()); return; }
    CsvReader csv(&file);
    QStringList header;
    if (!csv.readRow(&header)) { ui::error(this, tr("The file is empty.")); return; }
    QHash<QString, int> col;
    for (int i = 0; i < header.size(); ++i) col.insert(header[i].trimmed().toLower().replace(' ', '_'), i);
    if (!col.contains("brand_name") && !col.contains("brand")) {
        ui::error(this, tr("The first row must have column names. Required: brand_name. Optional: generic_name, strength, dosage_form, "
                           "manufacturer, barcode, purchase_price, sale_price, units_per_pack, min_stock, rack, rx_required, tax_percent."));
        return;
    }
    QStringList row;
    int added = 0, failed = 0;
    QStringList problems;
    auto get = [&](const QString &k) { const int i = col.value(k, -1); return i >= 0 && i < row.size() ? row[i].trimmed() : QString(); };
    while (csv.readRow(&row)) {
        if (row.join("").trimmed().isEmpty()) continue;
        MedicineInput m;
        m.brandName = get("brand_name").isEmpty() ? get("brand") : get("brand_name");
        m.brandNameUr = get("urdu_name");
        m.genericName = get("generic_name");
        m.strength = get("strength");
        m.dosageForm = get("dosage_form");
        m.manufacturer = get("manufacturer");
        m.barcode = get("barcode");
        m.packSize = get("pack_size");
        m.rackLocation = get("rack");
        m.category = get("category");
        Money::parse(get("purchase_price").isEmpty() ? "0" : get("purchase_price"), &m.purchasePrice);
        Money::parse(get("sale_price").isEmpty() ? "0" : get("sale_price"), &m.salePrice);
        m.unitsPerPack = qMax(1, get("units_per_pack").toInt());
        m.minStock = get("min_stock").toInt();
        m.taxRateBp = qRound(get("tax_percent").toDouble() * 100);
        m.rxRequired = QStringList{"1", "yes", "y", "true", "rx"}.contains(get("rx_required").toLower());
        const Result r = InventoryService::addMedicine(m);
        if (r.ok) ++added;
        else { ++failed; if (problems.size() < 10) problems << tr("Line %1: %2").arg(csv.lineNumber()).arg(r.error); }
    }
    emit AppEvents::instance()->pharmacyChanged();
    ui::info(this, tr("%1 medicines added. %2 rows skipped.").arg(added).arg(failed) + (problems.isEmpty() ? QString() : "\n\n" + problems.join('\n')));
}

void MedicinesPage::openCatalog()
{
    CatalogBrowserDialog dlg(m_ctx, true, this);
    dlg.exec();
    reload();
}

void MedicinesPage::openReview()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Review reference changes"));
    dlg.resize(900, 460);
    auto *v = new QVBoxLayout(&dlg);
    auto *info = new QLabel(tr("These changes came from the official reference list. Your stock and prices were not changed. "
                               "Decide what to do for each medicine."), &dlg);
    info->setWordWrap(true);
    v->addWidget(info);
    auto *t = new QTableWidget(0, 5, &dlg);
    t->setHorizontalHeaderLabels({tr("Medicine"), tr("Change"), tr("Old value"), tr("New value"), tr("Date")});
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->horizontalHeader()->setStretchLastSection(true);
    v->addWidget(t, 1);
    QList<qint64> ids;
    QList<qint64> medIds;
    auto load = [&] {
        t->setRowCount(0); ids.clear(); medIds.clear();
        QSqlQuery q(Db::get(Db::Pharmacy));
        q.exec("SELECT f.id, m.brand_name || ' ' || COALESCE(m.strength,''), f.reason, f.old_value, f.new_value, f.created_at, m.id "
               "FROM medicine_review_flags f JOIN medicines m ON m.id=f.medicine_id WHERE f.resolved_at IS NULL ORDER BY f.id");
        while (q.next()) {
            const int r = t->rowCount();
            t->insertRow(r);
            ids << q.value(0).toLongLong();
            medIds << q.value(6).toLongLong();
            const QString reason = q.value(2).toString();
            const QString label = reason == "registration_status_changed" ? tr("Registration status changed")
                : reason == "registration_no_changed" ? tr("Registration number changed") : tr("Reference details changed");
            const QStringList cells = {q.value(1).toString(), label, q.value(3).toString(), q.value(4).toString(),
                                       q.value(5).toString().left(10)};
            for (int c = 0; c < cells.size(); ++c) t->setItem(r, c, new QTableWidgetItem(cells[c]));
        }
    };
    load();
    auto *h = new QHBoxLayout;
    auto *keep = new QPushButton(tr("Mark as reviewed"), &dlg);
    auto *apply = new QPushButton(tr("Copy new registration info to my record"), &dlg);
    auto *archive = new QPushButton(tr("Archive medicine"), &dlg);
    auto *close = new QPushButton(tr("Close"), &dlg);
    for (auto *b : {keep, apply, archive}) h->addWidget(b);
    h->addStretch();
    h->addWidget(close);
    v->addLayout(h);
    for (auto *b : {keep, apply, archive}) b->setEnabled(ui::can("medicines.edit"));
    auto resolve = [&](const QString &how) {
        const int r = t->currentRow();
        if (r < 0) return;
        audit("reference_flag_resolved", "medicine", medIds[r], how);
        QSqlDatabase db = Db::get(Db::Pharmacy);
        db.transaction();
        QSqlQuery q(db);
        if (how == "applied") {
            QSqlQuery f(db);
            f.prepare("SELECT reason, new_value FROM medicine_review_flags WHERE id=?");
            f.addBindValue(ids[r]);
            f.exec();
            if (f.next()) {
                const QString col = f.value(0).toString() == "registration_status_changed" ? "registration_status"
                    : f.value(0).toString() == "registration_no_changed" ? "registration_no" : QString();
                if (!col.isEmpty()) {
                    q.prepare("UPDATE medicines SET " + col + "=?, updated_at=? WHERE id=?");
                    q.addBindValue(f.value(1)); q.addBindValue(Db::nowIso()); q.addBindValue(medIds[r]);
                    q.exec();
                }
            }
        } else if (how == "archived") {
            q.prepare("UPDATE medicines SET archived=1, updated_at=? WHERE id=?");
            q.addBindValue(Db::nowIso()); q.addBindValue(medIds[r]);
            q.exec();
        }
        q.prepare("UPDATE medicine_review_flags SET resolved_at=?, resolution=? WHERE id=?");
        q.addBindValue(Db::nowIso()); q.addBindValue(how); q.addBindValue(ids[r]);
        q.exec();
        db.commit();
        load();
    };
    connect(keep, &QPushButton::clicked, &dlg, [&] { resolve("kept"); });
    connect(apply, &QPushButton::clicked, &dlg, [&] { resolve("applied"); });
    connect(archive, &QPushButton::clicked, &dlg, [&] { resolve("archived"); });
    connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.exec();
    reload();
}
}
