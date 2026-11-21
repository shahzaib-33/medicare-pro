#include "UsersPage.h"
#include "Ui.h"
#include "services/AuthService.h"
#include <QCheckBox>
#include <QCoreApplication>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QSqlQuery>
#include <QTabWidget>
#include <QTableWidget>

namespace mcp {

static void fillRoles(QComboBox *c)
{
    QSqlQuery q(Db::get(Db::Pharmacy));
    q.exec("SELECT code, name FROM roles ORDER BY id");
    while (q.next()) {
        if (q.value(0).toString() == "super_admin" && Session::instance().user().roleCode != "super_admin") continue;
        c->addItem(QCoreApplication::translate("Roles", q.value(1).toString().toUtf8().constData()), q.value(0));
    }
}

UsersPage::UsersPage(AppContext *, QWidget *parent) : QWidget(parent)
{
    auto f = ui::page(this, tr("Users and permissions"));
    auto *tabs = new QTabWidget(this);

    auto *usersTab = new QWidget;
    auto *uv = new QVBoxLayout(usersTab);
    auto *ub = new QHBoxLayout;
    auto *add = ui::button(tr("Add user"), usersTab, true);
    auto *edit = ui::button(tr("Edit"), usersTab);
    auto *reset = ui::button(tr("Reset password"), usersTab);
    auto *over = ui::button(tr("Special permissions"), usersTab);
    for (auto *b : {edit, reset, over}) ub->addWidget(b);
    ub->addStretch();
    ub->addWidget(add);
    uv->addLayout(ub);
    m_users = new ui::SqlTable(usersTab);
    uv->addWidget(m_users, 1);
    tabs->addTab(usersTab, tr("Users"));

    auto *rolesTab = new QWidget;
    auto *rv = new QVBoxLayout(rolesTab);
    auto *rb = new QHBoxLayout;
    m_role = new QComboBox(rolesTab);
    fillRoles(m_role);
    rb->addWidget(new QLabel(tr("Role"), rolesTab));
    rb->addWidget(m_role);
    rb->addStretch();
    rv->addLayout(rb);
    rv->addWidget(ui::muted(tr("Tick what this role may do. Changes apply at the next sign in. Owner and Super Admin always have every permission."), rolesTab));
    m_perms = new QTableWidget(0, 2, rolesTab);
    m_perms->setHorizontalHeaderLabels({tr("Allowed"), tr("Permission")});
    m_perms->horizontalHeader()->setStretchLastSection(true);
    m_perms->verticalHeader()->hide();
    rv->addWidget(m_perms, 1);
    tabs->addTab(rolesTab, tr("Role permissions"));

    auto *empTab = new QWidget;
    auto *ev = new QVBoxLayout(empTab);
    auto *eb = new QHBoxLayout;
    auto *addEmp = ui::button(tr("Add employee"), empTab, true);
    auto *editEmp = ui::button(tr("Edit"), empTab);
    addEmp->setEnabled(ui::can("employees.manage"));
    editEmp->setEnabled(ui::can("employees.manage"));
    eb->addStretch();
    eb->addWidget(editEmp);
    eb->addWidget(addEmp);
    ev->addLayout(eb);
    m_employees = new ui::SqlTable(empTab);
    ev->addWidget(m_employees, 1);
    tabs->addTab(empTab, tr("Employees"));
    f.body->addWidget(tabs, 1);

    connect(add, &QPushButton::clicked, this, &UsersPage::addUser);
    connect(edit, &QPushButton::clicked, this, &UsersPage::editUser);
    connect(m_users, &ui::SqlTable::activatedId, this, &UsersPage::editUser);
    connect(reset, &QPushButton::clicked, this, &UsersPage::resetPassword);
    connect(over, &QPushButton::clicked, this, &UsersPage::editOverrides);
    connect(m_role, &QComboBox::currentIndexChanged, this, &UsersPage::loadRole);
    connect(m_perms, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *it) {
        if (m_loading || it->column() != 0) return;
        const QString perm = it->data(Qt::UserRole).toString();
        if (!ui::check(this, AuthService::setRolePermission(m_role->currentData().toString(), perm, it->checkState() == Qt::Checked))) loadRole();
    });
    connect(addEmp, &QPushButton::clicked, this, [this] { editEmployee(0); });
    connect(editEmp, &QPushButton::clicked, this, [this] { if (m_employees->currentId()) editEmployee(m_employees->currentId()); });
    connect(m_employees, &ui::SqlTable::activatedId, this, [this](qint64 id) { if (ui::can("employees.manage")) editEmployee(id); });
    reload();
    loadRole();
}

void UsersPage::reload()
{
    m_users->setQuery("SELECT u.id, u.username, u.full_name, r.name, CASE u.active WHEN 1 THEN '' ELSE 'inactive' END, "
                      "COALESCE(datetime(u.last_login_at,'localtime'),''), (SELECT COUNT(*) FROM user_permission_overrides o WHERE o.user_id=u.id), "
                      "r.code FROM users u JOIN roles r ON r.id=u.role_id ORDER BY u.active DESC, u.username",
                      {}, {"id", tr("Username"), tr("Full name"), tr("Role"), tr("Status"), tr("Last sign in"), tr("Special permissions")});
    m_users->setColumnHidden(7, true);
    m_employees->setQuery("SELECT e.id, e.name, COALESCE(e.designation,''), COALESCE(e.phone,''), COALESCE(e.joined_on,''), "
                          "COALESCE(u.username,''), CASE e.active WHEN 1 THEN '' ELSE 'inactive' END FROM employees e "
                          "LEFT JOIN users u ON u.id=e.user_id ORDER BY e.active DESC, e.name",
                          {}, {"id", tr("Name"), tr("Designation"), tr("Phone"), tr("Joined"), tr("Login"), tr("Status")});
}

void UsersPage::loadRole()
{
    m_loading = true;
    const QString code = m_role->currentData().toString();
    QSet<QString> has;
    QSqlQuery q(Db::get(Db::Pharmacy));
    q.prepare("SELECT rp.permission_code FROM role_permissions rp JOIN roles r ON r.id=rp.role_id WHERE r.code=?");
    q.addBindValue(code);
    q.exec();
    while (q.next()) has << q.value(0).toString();
    const auto &all = Perm::all();
    m_perms->setRowCount(all.size());
    const bool locked = code == "owner" || code == "super_admin";
    for (int i = 0; i < all.size(); ++i) {
        auto *c = new QTableWidgetItem;
        c->setData(Qt::UserRole, all[i].first);
        c->setCheckState(has.contains(all[i].first) ? Qt::Checked : Qt::Unchecked);
        if (locked) c->setFlags(c->flags() & ~Qt::ItemIsUserCheckable & ~Qt::ItemIsEnabled);
        m_perms->setItem(i, 0, c);
        m_perms->setItem(i, 1, new QTableWidgetItem(QCoreApplication::translate("Permissions", all[i].second.toUtf8().constData())));
    }
    m_perms->resizeColumnToContents(0);
    m_loading = false;
}

void UsersPage::addUser()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Add user"));
    dlg.setMinimumWidth(420);
    auto *form = new QFormLayout(&dlg);
    auto *user = new QLineEdit(&dlg);
    auto *name = new QLineEdit(&dlg);
    auto *pass = new QLineEdit(&dlg);
    pass->setEchoMode(QLineEdit::Password);
    auto *role = new QComboBox(&dlg);
    fillRoles(role);
    role->setCurrentIndex(qMax(0, role->findData("cashier")));
    form->addRow(tr("Username"), user);
    form->addRow(tr("Full name"), name);
    form->addRow(tr("First password"), pass);
    form->addRow(tr("Role"), role);
    form->addRow(ui::muted(tr("The user must choose a new password at first sign in."), &dlg));
    auto *ok = ui::button(tr("Create user"), &dlg, true);
    form->addRow(ok);
    connect(ok, &QPushButton::clicked, &dlg, [&] {
        if (ui::check(&dlg, AuthService::createUser(user->text(), name->text(), pass->text(), role->currentData().toString()))) dlg.accept();
    });
    if (dlg.exec() == QDialog::Accepted) reload();
}

void UsersPage::editUser()
{
    const qint64 id = m_users->currentId();
    if (!id) return;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Edit user %1").arg(m_users->currentValue(1).toString()));
    dlg.setMinimumWidth(420);
    auto *form = new QFormLayout(&dlg);
    auto *name = new QLineEdit(m_users->currentValue(2).toString(), &dlg);
    auto *role = new QComboBox(&dlg);
    fillRoles(role);
    role->setCurrentIndex(qMax(0, role->findData(m_users->currentValue(7))));
    auto *active = new QCheckBox(tr("Active"), &dlg);
    active->setChecked(m_users->currentValue(4).toString().isEmpty());
    form->addRow(tr("Full name"), name);
    form->addRow(tr("Role"), role);
    form->addRow(QString(), active);
    auto *ok = ui::button(tr("Save"), &dlg, true);
    form->addRow(ok);
    connect(ok, &QPushButton::clicked, &dlg, [&] {
        if (ui::check(&dlg, AuthService::updateUser(id, name->text(), role->currentData().toString(), active->isChecked()))) dlg.accept();
    });
    if (dlg.exec() == QDialog::Accepted) reload();
}

void UsersPage::resetPassword()
{
    const qint64 id = m_users->currentId();
    if (!id) return;
    bool ok = false;
    const QString p = QInputDialog::getText(this, tr("Reset password"), tr("New password for %1:").arg(m_users->currentValue(1).toString()),
                                            QLineEdit::Password, {}, &ok);
    if (ok && ui::check(this, AuthService::resetPassword(id, p))) ui::info(this, tr("Password reset. The user must choose a new one at next sign in."));
}

void UsersPage::editOverrides()
{
    const qint64 id = m_users->currentId();
    if (!id) return;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Special permissions for %1").arg(m_users->currentValue(1).toString()));
    dlg.resize(620, 560);
    auto *v = new QVBoxLayout(&dlg);
    v->addWidget(ui::muted(tr("Role default follows the role. Allow or Deny changes it for this user only."), &dlg));
    auto *t = new QTableWidget(0, 2, &dlg);
    t->setHorizontalHeaderLabels({tr("Permission"), tr("Setting")});
    t->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    t->verticalHeader()->hide();
    QHash<QString, int> current;
    QSqlQuery q(Db::get(Db::Pharmacy));
    q.prepare("SELECT permission_code, allowed FROM user_permission_overrides WHERE user_id=?");
    q.addBindValue(id);
    q.exec();
    while (q.next()) current.insert(q.value(0).toString(), q.value(1).toInt());
    const auto &all = Perm::all();
    t->setRowCount(all.size());
    for (int i = 0; i < all.size(); ++i) {
        t->setItem(i, 0, new QTableWidgetItem(QCoreApplication::translate("Permissions", all[i].second.toUtf8().constData())));
        auto *c = new QComboBox(t);
        c->addItem(tr("Role default"), -1);
        c->addItem(tr("Allow"), 1);
        c->addItem(tr("Deny"), 0);
        c->setCurrentIndex(c->findData(current.value(all[i].first, -1)));
        t->setCellWidget(i, 1, c);
        const QString code = all[i].first;
        connect(c, &QComboBox::currentIndexChanged, &dlg, [&dlg, c, code, id] {
            ui::check(&dlg, AuthService::setOverride(id, code, c->currentData().toInt()));
        });
    }
    v->addWidget(t, 1);
    dlg.exec();
    reload();
}

void UsersPage::editEmployee(qint64 id)
{
    QDialog dlg(this);
    dlg.setWindowTitle(id ? tr("Edit employee") : tr("Add employee"));
    dlg.setMinimumWidth(420);
    auto *form = new QFormLayout(&dlg);
    auto *name = new QLineEdit(&dlg);
    auto *desig = new QLineEdit(&dlg);
    auto *phone = new QLineEdit(&dlg);
    auto *joined = ui::dateEdit(QDate::currentDate(), &dlg);
    auto *notes = new QLineEdit(&dlg);
    auto *active = new QCheckBox(tr("Active"), &dlg);
    active->setChecked(true);
    if (id) {
        QSqlQuery q(Db::get(Db::Pharmacy));
        q.prepare("SELECT name, designation, phone, joined_on, notes, active FROM employees WHERE id=?");
        q.addBindValue(id);
        q.exec();
        if (q.next()) {
            name->setText(q.value(0).toString());
            desig->setText(q.value(1).toString());
            phone->setText(q.value(2).toString());
            if (QDate::fromString(q.value(3).toString(), Qt::ISODate).isValid()) joined->setDate(QDate::fromString(q.value(3).toString(), Qt::ISODate));
            notes->setText(q.value(4).toString());
            active->setChecked(q.value(5).toBool());
        }
    }
    form->addRow(tr("Name"), name);
    form->addRow(tr("Designation"), desig);
    form->addRow(tr("Phone"), phone);
    form->addRow(tr("Joined on"), joined);
    form->addRow(tr("Notes"), notes);
    form->addRow(QString(), active);
    auto *ok = ui::button(tr("Save"), &dlg, true);
    form->addRow(ok);
    connect(ok, &QPushButton::clicked, &dlg, [&] {
        if (ui::check(&dlg, AuthService::saveEmployee(id, name->text(), phone->text(), desig->text(), joined->date().toString(Qt::ISODate),
                                                      notes->text(), active->isChecked())))
            dlg.accept();
    });
    if (dlg.exec() == QDialog::Accepted) reload();
}
}
