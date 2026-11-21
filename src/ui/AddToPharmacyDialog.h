#pragma once
#include <QDialog>
#include "refdata/MedicineRecord.h"
class QLineEdit;
class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QCheckBox;
class QTableWidget;
class QLabel;

namespace mcp {
// Step 7 and 8: reference details are filled in, the pharmacist sets local values.
// Opening stock is optional and only created when batches are entered.
class AddToPharmacyDialog : public QDialog {
    Q_OBJECT
public:
    AddToPharmacyDialog(const MedicineRecord &ref, QWidget *parent = nullptr);
    qint64 createdId() const { return m_created; }
private slots:
    void save();
private:
    MedicineRecord m_ref;
    qint64 m_created = 0;
    QDoubleSpinBox *m_purchase, *m_sale;
    QLineEdit *m_pack, *m_barcode, *m_rack;
    QSpinBox *m_units, *m_minStock;
    QComboBox *m_supplier;
    QCheckBox *m_rx;
    QTableWidget *m_batches;
    QLabel *m_error;
};
}
