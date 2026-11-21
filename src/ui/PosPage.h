#pragma once
#include <QWidget>
#include "AppContext.h"
#include "services/SalesService.h"
class QLineEdit;
class QTableWidget;
class QLabel;
class QComboBox;
class QListWidget;
class QTimer;

namespace mcp {
namespace ui { class MoneyEdit; }

// Point of sale. Barcode or name search, FEFO stock, discounts, tax, split payments, hold and resume.
class PosPage : public QWidget {
    Q_OBJECT
public:
    explicit PosPage(AppContext *ctx, QWidget *parent = nullptr);
protected:
    void showEvent(QShowEvent *e) override;
private:
    void search();
    void addMedicine(qint64 id, int qty);
    void rebuildCart();
    void recalc();
    void loadCustomers();
    void pay(bool printInvoice = false);
    void hold();
    void resume();
    void clearBill(bool ask);
    void addPayment(const QString &method, Paisa amount);
    void ensureShift();
    Cart currentCart() const;

    AppContext *m_ctx;
    Cart m_cart;
    QLineEdit *m_search;
    QLineEdit *m_barcode;
    ui::MoneyEdit *m_cashReceived;
    QTableWidget *m_results;
    QTableWidget *m_lines;
    QTableWidget *m_payments;
    QComboBox *m_customer, *m_rx;
    ui::MoneyEdit *m_billDiscount;
    QLabel *m_subtotal, *m_discount, *m_tax, *m_total, *m_paid, *m_change, *m_shiftInfo, *m_lastSale;
    QTimer *m_debounce;
    qint64 m_lastSaleId = 0;
    bool m_updating = false;
};
}
