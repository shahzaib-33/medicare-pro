#pragma once
#include <QWidget>
#include "AppContext.h"
class QComboBox;
class QTableWidget;

namespace mcp {
namespace ui { class SqlTable; }

// Users, role permissions, per user overrides, employees.
class UsersPage : public QWidget {
    Q_OBJECT
public:
    explicit UsersPage(AppContext *ctx, QWidget *parent = nullptr);
    void reload();
private:
    void addUser();
    void editUser();
    void resetPassword();
    void editOverrides();
    void loadRole();
    void editEmployee(qint64 id);
    ui::SqlTable *m_users, *m_employees;
    QComboBox *m_role;
    QTableWidget *m_perms;
    bool m_loading = false;
};
}
