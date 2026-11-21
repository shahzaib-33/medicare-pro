#pragma once
#include <QWidget>
#include "AppContext.h"
class QComboBox;

namespace mcp {
namespace ui { class SqlTable; class DateRangeBar; }

// Purchase invoices with batches, payments, returns to supplier and reversals.
class PurchasesPage : public QWidget {
    Q_OBJECT
public:
    explicit PurchasesPage(AppContext *ctx, QWidget *parent = nullptr);
    void reload();
private:
    void newPurchase();
    void pay();
    void returnItems();
    void reverse();
    void showItems();
    ui::SqlTable *m_table;
    ui::DateRangeBar *m_range;
};

// Shared medicine picker: editable combo box that matches any part of the name.
QComboBox *medicinePicker(QWidget *parent);
}
