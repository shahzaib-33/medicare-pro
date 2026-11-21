#pragma once
#include <QWidget>
#include "AppContext.h"
class QLineEdit;

namespace mcp {
namespace ui { class SqlTable; }

// Suppliers or customers, with ledger, payments and a printable statement.
class PartiesPage : public QWidget {
    Q_OBJECT
public:
    enum class Kind { Supplier, Customer };
    PartiesPage(Kind kind, AppContext *ctx, QWidget *parent = nullptr);
    void reload();
private:
    void edit(qint64 id);
    void payment();
    void openingBalance();
    void statement();
    Kind m_kind;
    ui::SqlTable *m_table;
    ui::SqlTable *m_ledger;
    QLineEdit *m_filter;
};
}
