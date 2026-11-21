#pragma once
#include <QWidget>
#include "AppContext.h"
class QLabel;

namespace mcp {
namespace ui { class SqlTable; class DateRangeBar; }

// Prescriptions with pharmacist approval.
class PrescriptionsPage : public QWidget {
    Q_OBJECT
public:
    explicit PrescriptionsPage(AppContext *ctx, QWidget *parent = nullptr);
    void reload();
private:
    void create();
    void review(bool approve);
    ui::SqlTable *m_table;
    ui::SqlTable *m_items;
};

// Expenses with reversal instead of delete.
class ExpensesPage : public QWidget {
    Q_OBJECT
public:
    explicit ExpensesPage(AppContext *ctx, QWidget *parent = nullptr);
    void reload();
private:
    void add();
    ui::SqlTable *m_table;
    ui::DateRangeBar *m_range;
    QLabel *m_total;
};

// The signed in user's cash shift: open, pay in or out, close with counted cash.
class ShiftsPage : public QWidget {
    Q_OBJECT
public:
    explicit ShiftsPage(AppContext *ctx, QWidget *parent = nullptr);
    void reload();
protected:
    void showEvent(QShowEvent *e) override;
private:
    QLabel *m_status, *m_expected;
    ui::SqlTable *m_moves, *m_history;
};
}
