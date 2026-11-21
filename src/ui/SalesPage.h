#pragma once
#include <QWidget>
#include "AppContext.h"
class QLineEdit;

namespace mcp {
namespace ui { class SqlTable; class DateRangeBar; }

// Sales history with receipt reprint (marked as copy) and returns.
class SalesPage : public QWidget {
    Q_OBJECT
public:
    explicit SalesPage(AppContext *ctx, QWidget *parent = nullptr);
private:
    void reload();
    void returnSale();
    void showItems();
    ui::SqlTable *m_table;
    ui::DateRangeBar *m_range;
    QLineEdit *m_filter;
};
}
