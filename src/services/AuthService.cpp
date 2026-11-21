#include "AuthService.h"
#include <QDateTime>
#include <QRandomGenerator>
#include <openssl/evp.h>

namespace mcp {

Session &Session::instance() { static Session s; return s; }

void require(const QString &permission)
{
    if (!Session::instance().can(permission))
        throw ServiceError{QObject::tr("You do not have permission for this action (%1).").arg(permission)};
}

void audit(const QString &action, const QString &entity, qint64 entityId, const QString &details)
{
    QSqlQuery q(pharmacyDb());
    q.prepare("INSERT INTO audit_logs(ts, user_id, action, entity, entity_id, details) VALUES(?,?,?,?,?,?)");
    q.addBindValue(Db::nowIso());
    q.addBindValue(Session::instance().userId() ? QVariant(Session::instance().userId()) : QVariant());
    q.addBindValue(action);
    q.addBindValue(nullIf(entity));
    q.addBindValue(entityId ? QVariant(entityId) : QVariant());
    q.addBindValue(nullIf(details));
    q.exec();
}

namespace Perm {
const QList<QPair<QString, QString>> &all()
{
    static const QList<QPair<QString, QString>> p = {
        {"dashboard.view", QT_TRANSLATE_NOOP("Permissions", "View dashboard")},
        {"dashboard.financial", QT_TRANSLATE_NOOP("Permissions", "See profit, expenses and financial totals")},
        {"pos.sell", QT_TRANSLATE_NOOP("Permissions", "Sell at the counter")},
        {"pos.discount", QT_TRANSLATE_NOOP("Permissions", "Give discounts")},
        {"pos.price_override", QT_TRANSLATE_NOOP("Permissions", "Change sale price at the counter")},
        {"pos.credit_sale", QT_TRANSLATE_NOOP("Permissions", "Sell on customer credit")},
        {"sales.view", QT_TRANSLATE_NOOP("Permissions", "View sales history")},
        {"sales.reprint", QT_TRANSLATE_NOOP("Permissions", "Reprint receipts")},
        {"sales.return", QT_TRANSLATE_NOOP("Permissions", "Process customer returns")},
        {"medicines.view", QT_TRANSLATE_NOOP("Permissions", "View medicines")},
        {"medicines.edit", QT_TRANSLATE_NOOP("Permissions", "Add and edit medicines")},
        {"medicines.delete", QT_TRANSLATE_NOOP("Permissions", "Archive and delete medicines")},
        {"inventory.view", QT_TRANSLATE_NOOP("Permissions", "View stock and batches")},
        {"inventory.adjust", QT_TRANSLATE_NOOP("Permissions", "Adjust stock, damage, counts")},
        {"inventory.inspect", QT_TRANSLATE_NOOP("Permissions", "Inspect returned stock")},
        {"purchases.view", QT_TRANSLATE_NOOP("Permissions", "View purchases")},
        {"purchases.post", QT_TRANSLATE_NOOP("Permissions", "Post purchase invoices")},
        {"purchases.return", QT_TRANSLATE_NOOP("Permissions", "Return stock to suppliers")},
        {"purchases.reverse", QT_TRANSLATE_NOOP("Permissions", "Reverse posted purchase invoices")},
        {"suppliers.view", QT_TRANSLATE_NOOP("Permissions", "View suppliers")},
        {"suppliers.edit", QT_TRANSLATE_NOOP("Permissions", "Add and edit suppliers")},
        {"suppliers.pay", QT_TRANSLATE_NOOP("Permissions", "Pay suppliers")},
        {"customers.view", QT_TRANSLATE_NOOP("Permissions", "View customers")},
        {"customers.edit", QT_TRANSLATE_NOOP("Permissions", "Add and edit customers")},
        {"customers.receive", QT_TRANSLATE_NOOP("Permissions", "Receive customer payments")},
        {"prescriptions.view", QT_TRANSLATE_NOOP("Permissions", "View prescriptions")},
        {"prescriptions.edit", QT_TRANSLATE_NOOP("Permissions", "Record prescriptions")},
        {"prescriptions.approve", QT_TRANSLATE_NOOP("Permissions", "Pharmacist review and approval")},
        {"expenses.view", QT_TRANSLATE_NOOP("Permissions", "View expenses")},
        {"expenses.edit", QT_TRANSLATE_NOOP("Permissions", "Record and reverse expenses")},
        {"shifts.own", QT_TRANSLATE_NOOP("Permissions", "Open and close own cash shift")},
        {"shifts.manage", QT_TRANSLATE_NOOP("Permissions", "View and manage all shifts")},
        {"reports.sales", QT_TRANSLATE_NOOP("Permissions", "Sales reports")},
        {"reports.financial", QT_TRANSLATE_NOOP("Permissions", "Profit, cash flow and financial reports")},
        {"reports.stock", QT_TRANSLATE_NOOP("Permissions", "Stock and expiry reports")},
        {"users.manage", QT_TRANSLATE_NOOP("Permissions", "Manage users, roles and permissions")},
        {"employees.manage", QT_TRANSLATE_NOOP("Permissions", "Manage employee profiles")},
        {"settings.edit", QT_TRANSLATE_NOOP("Permissions", "Change pharmacy settings")},
        {"backup.manage", QT_TRANSLATE_NOOP("Permissions", "Backup and restore")},
        {"updates.manage", QT_TRANSLATE_NOOP("Permissions", "Install software and data updates")},
        {"refdata.import", QT_TRANSLATE_NOOP("Permissions", "Import official medicine data")},
        {"audit.view", QT_TRANSLATE_NOOP("Permissions", "View audit and activity logs")},
    };
    return p;
}
}

namespace AuthService {

static QByteArray pbkdf2(const QString &password, const QByteArray &salt, int iterations)
{
    QByteArray out(32, 0);
    const QByteArray pw = password.toUtf8();
    PKCS5_PBKDF2_HMAC(pw.constData(), int(pw.size()), reinterpret_cast<const unsigned char *>(salt.constData()),
                      int(salt.size()), iterations, EVP_sha256(), int(out.size()),
                      reinterpret_cast<unsigned char *>(out.data()));
    return out;
}

PasswordHash hashPassword(const QString &password, int iterations)
{
    QByteArray salt(16, 0);
    for (int i = 0; i < salt.size(); ++i) salt[i] = char(QRandomGenerator::system()->bounded(256));
    return {QString::fromLatin1(pbkdf2(password, salt, iterations).toBase64()), QString::fromLatin1(salt.toBase64()), iterations};
}

bool verifyPassword(const QString &password, const QString &hash, const QString &salt, int iterations)
{
    const QByteArray a = pbkdf2(password, QByteArray::fromBase64(salt.toLatin1()), iterations);
    const QByteArray b = QByteArray::fromBase64(hash.toLatin1());
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;   // constant time compare
    for (int i = 0; i < a.size(); ++i) diff |= uchar(a[i]) ^ uchar(b[i]);
    return diff == 0;
}

QString passwordProblem(const QString &p)
{
    if (p.size() < 8) return QObject::tr("Password must have at least 8 characters.");
    bool letter = false, digit = false;
    for (QChar c : p) { if (c.isLetter()) letter = true; if (c.isDigit()) digit = true; }
    if (!letter || !digit) return QObject::tr("Password must contain letters and numbers.");
    return {};
}

static const QHash<QString, QStringList> &roleDefaults()
{
    static const QHash<QString, QStringList> r = {
        {"pharmacist", {"dashboard.view", "pos.sell", "pos.discount", "pos.credit_sale", "sales.view", "sales.reprint",
                        "sales.return", "medicines.view", "medicines.edit", "inventory.view", "inventory.inspect",
                        "prescriptions.view", "prescriptions.edit", "prescriptions.approve", "customers.view",
                        "customers.edit", "customers.receive", "shifts.own", "reports.stock"}},
        {"cashier", {"dashboard.view", "pos.sell", "sales.view", "sales.reprint", "medicines.view", "customers.view",
                     "customers.edit", "customers.receive", "prescriptions.view", "shifts.own"}},
        {"inventory_manager", {"dashboard.view", "medicines.view", "medicines.edit", "medicines.delete", "inventory.view",
                               "inventory.adjust", "inventory.inspect", "purchases.view", "purchases.post",
                               "purchases.return", "suppliers.view", "suppliers.edit", "reports.stock", "refdata.import"}},
        {"accountant", {"dashboard.view", "dashboard.financial", "sales.view", "purchases.view", "suppliers.view",
                        "suppliers.pay", "customers.view", "customers.receive", "expenses.view", "expenses.edit",
                        "shifts.manage", "reports.sales", "reports.financial", "reports.stock"}},
    };
    return r;
}

void seedRolesAndPermissions()
{
    QSqlDatabase db = pharmacyDb();
    db.transaction();
    try {
        for (const auto &p : Perm::all())
            exec("INSERT INTO permissions(code, description) VALUES(?,?) ON CONFLICT(code) DO UPDATE SET description=excluded.description",
                 {p.first, p.second});
        const QList<QPair<QString, QString>> roles = {{"super_admin", QT_TRANSLATE_NOOP("Roles", "Super Admin")}, {"owner", QT_TRANSLATE_NOOP("Roles", "Pharmacy Owner")},
            {"pharmacist", QT_TRANSLATE_NOOP("Roles", "Pharmacist")}, {"cashier", QT_TRANSLATE_NOOP("Roles", "Cashier")}, {"inventory_manager", QT_TRANSLATE_NOOP("Roles", "Inventory Manager")},
            {"accountant", QT_TRANSLATE_NOOP("Roles", "Accountant")}};
        for (const auto &r : roles) {
            const bool isNew = scalar("SELECT COUNT(*) FROM roles WHERE code=?", {r.first}).toInt() == 0;
            exec("INSERT OR IGNORE INTO roles(code, name) VALUES(?,?)", {r.first, r.second});
            const qint64 id = scalar("SELECT id FROM roles WHERE code=?", {r.first}).toLongLong();
            if (r.first == "super_admin" || r.first == "owner") {
                // Admin roles always hold every permission, including new ones from updates.
                for (const auto &p : Perm::all())
                    exec("INSERT OR IGNORE INTO role_permissions(role_id, permission_code) VALUES(?,?)", {id, p.first});
            } else if (isNew) {
                // Defaults only when the role is created. The owner's later changes are kept.
                for (const QString &p : roleDefaults().value(r.first))
                    exec("INSERT OR IGNORE INTO role_permissions(role_id, permission_code) VALUES(?,?)", {id, p});
            }
        }
        db.commit();
    } catch (const ServiceError &) {
        db.rollback();
    }
}

bool isFirstRun() { return scalar("SELECT COUNT(*) FROM users").toInt() == 0; }

static UserInfo loadUser(qint64 id)
{
    UserInfo u;
    QSqlQuery q = exec("SELECT u.id, u.username, u.full_name, r.code, r.name, u.active, u.must_change_password "
                       "FROM users u JOIN roles r ON r.id=u.role_id WHERE u.id=?", {id});
    if (q.next()) {
        u.id = q.value(0).toLongLong(); u.username = q.value(1).toString(); u.fullName = q.value(2).toString();
        u.roleCode = q.value(3).toString(); u.roleName = q.value(4).toString(); u.active = q.value(5).toBool();
        u.mustChangePassword = q.value(6).toBool();
    }
    return u;
}

QSet<QString> effectivePermissions(qint64 userId)
{
    QSet<QString> s;
    QSqlQuery q = exec("SELECT rp.permission_code FROM users u JOIN role_permissions rp ON rp.role_id=u.role_id WHERE u.id=?", {userId});
    while (q.next()) s.insert(q.value(0).toString());
    QSqlQuery o = exec("SELECT permission_code, allowed FROM user_permission_overrides WHERE user_id=?", {userId});
    while (o.next()) {
        if (o.value(1).toBool()) s.insert(o.value(0).toString());
        else s.remove(o.value(0).toString());
    }
    return s;
}

Result createFirstOwner(const QString &username, const QString &fullName, const QString &password)
{
    if (!isFirstRun()) return Result::fail(QObject::tr("An administrator already exists."));
    if (const QString p = passwordProblem(password); !p.isEmpty()) return Result::fail(p);
    if (username.trimmed().size() < 3) return Result::fail(QObject::tr("Username must have at least 3 characters."));
    return transaction([&] {
        const auto h = hashPassword(password);
        const qint64 role = scalar("SELECT id FROM roles WHERE code='owner'").toLongLong();
        const qint64 id = insert("INSERT INTO users(username, full_name, password_hash, password_salt, password_iterations, "
                                 "role_id, created_at) VALUES(?,?,?,?,?,?,?)",
                                 {username.trimmed(), fullName.trimmed(), h.hash, h.salt, h.iterations, role, Db::nowIso()});
        insert("INSERT INTO employees(user_id, name, designation, joined_on) VALUES(?,?,?,?)",
               {id, fullName.trimmed(), "Owner", todayIso()});
        audit("first_owner_created", "user", id);
        return Result::success(id);
    });
}

static void history(qint64 userId, const QString &username, const QString &event)
{
    QSqlQuery q(pharmacyDb());
    q.prepare("INSERT INTO login_history(user_id, username, event, ts) VALUES(?,?,?,?)");
    q.addBindValue(userId ? QVariant(userId) : QVariant());
    q.addBindValue(username);
    q.addBindValue(event);
    q.addBindValue(Db::nowIso());
    q.exec();
}

Result login(const QString &username, const QString &password)
{
    try {
        QSqlQuery q = exec("SELECT id, password_hash, password_salt, password_iterations, active, failed_attempts, locked_until "
                           "FROM users WHERE username=?", {username.trimmed()});
        if (!q.next()) { history(0, username, "failed"); return Result::fail(QObject::tr("Wrong username or password.")); }
        const qint64 id = q.value(0).toLongLong();
        if (!q.value(4).toBool()) { history(id, username, "inactive"); return Result::fail(QObject::tr("This account is deactivated.")); }
        const QDateTime lockedUntil = QDateTime::fromString(q.value(6).toString(), Qt::ISODate);
        if (lockedUntil.isValid() && lockedUntil > QDateTime::currentDateTimeUtc())
            return Result::fail(QObject::tr("Too many wrong attempts. Try again after a few minutes."));
        if (!verifyPassword(password, q.value(1).toString(), q.value(2).toString(), q.value(3).toInt())) {
            const int fails = q.value(5).toInt() + 1;
            const QVariant lock = fails >= 5 ? QVariant(QDateTime::currentDateTimeUtc().addSecs(300).toString(Qt::ISODate)) : QVariant();
            exec("UPDATE users SET failed_attempts=?, locked_until=? WHERE id=?", {fails >= 5 ? 0 : fails, lock, id});
            history(id, username, fails >= 5 ? "locked" : "failed");
            return Result::fail(QObject::tr("Wrong username or password."));
        }
        exec("UPDATE users SET failed_attempts=0, locked_until=NULL, last_login_at=? WHERE id=?", {Db::nowIso(), id});
        Session::instance().set(loadUser(id), effectivePermissions(id));
        history(id, username, "login");
        return Result::success(id);
    } catch (const ServiceError &e) {
        return Result::fail(e.message);
    }
}

void logout()
{
    if (Session::instance().loggedIn())
        history(Session::instance().userId(), Session::instance().user().username, "logout");
    Session::instance().clear();
}

Result verifyCurrentUser(const QString &password)
{
    try {
        QSqlQuery q = exec("SELECT password_hash, password_salt, password_iterations FROM users WHERE id=?", {Session::instance().userId()});
        if (q.next() && verifyPassword(password, q.value(0).toString(), q.value(1).toString(), q.value(2).toInt())) {
            history(Session::instance().userId(), Session::instance().user().username, "unlock");
            return Result::success();
        }
        history(Session::instance().userId(), Session::instance().user().username, "unlock_failed");
        return Result::fail(QObject::tr("Wrong password."));
    } catch (const ServiceError &e) { return Result::fail(e.message); }
}

Result changePassword(const QString &oldPassword, const QString &newPassword)
{
    if (const Result r = verifyCurrentUser(oldPassword); !r.ok) return r;
    if (const QString p = passwordProblem(newPassword); !p.isEmpty()) return Result::fail(p);
    return transaction([&] {
        const auto h = hashPassword(newPassword);
        exec("UPDATE users SET password_hash=?, password_salt=?, password_iterations=?, must_change_password=0 WHERE id=?",
             {h.hash, h.salt, h.iterations, Session::instance().userId()});
        audit("password_changed", "user", Session::instance().userId());
        return Result::success();
    });
}

Result createUser(const QString &username, const QString &fullName, const QString &password, const QString &roleCode)
{
    return transaction([&] {
        require("users.manage");
        if (username.trimmed().size() < 3) throw ServiceError{QObject::tr("Username must have at least 3 characters.")};
        if (const QString p = passwordProblem(password); !p.isEmpty()) throw ServiceError{p};
        if (roleCode == "super_admin" && Session::instance().user().roleCode != "super_admin")
            throw ServiceError{QObject::tr("Only a Super Admin can create another Super Admin.")};
        const qint64 role = scalar("SELECT id FROM roles WHERE code=?", {roleCode}).toLongLong();
        if (!role) throw ServiceError{QObject::tr("Unknown role.")};
        if (scalar("SELECT COUNT(*) FROM users WHERE username=?", {username.trimmed()}).toInt())
            throw ServiceError{QObject::tr("This username is already taken.")};
        const auto h = hashPassword(password);
        const qint64 id = insert("INSERT INTO users(username, full_name, password_hash, password_salt, password_iterations, "
                                 "role_id, must_change_password, created_at) VALUES(?,?,?,?,?,?,1,?)",
                                 {username.trimmed(), fullName.trimmed(), h.hash, h.salt, h.iterations, role, Db::nowIso()});
        insert("INSERT INTO employees(user_id, name, joined_on) VALUES(?,?,?)", {id, fullName.trimmed(), todayIso()});
        audit("user_created", "user", id, username + " / " + roleCode);
        return Result::success(id);
    });
}

Result updateUser(qint64 id, const QString &fullName, const QString &roleCode, bool active)
{
    return transaction([&] {
        require("users.manage");
        if (id == Session::instance().userId() && !active) throw ServiceError{QObject::tr("You cannot deactivate your own account.")};
        const qint64 role = scalar("SELECT id FROM roles WHERE code=?", {roleCode}).toLongLong();
        if (!role) throw ServiceError{QObject::tr("Unknown role.")};
        // Keep at least one active owner or super admin.
        const QString oldRole = scalar("SELECT r.code FROM users u JOIN roles r ON r.id=u.role_id WHERE u.id=?", {id}).toString();
        if ((oldRole == "owner" || oldRole == "super_admin") && (!active || (roleCode != "owner" && roleCode != "super_admin"))) {
            const int admins = scalar("SELECT COUNT(*) FROM users u JOIN roles r ON r.id=u.role_id WHERE u.active=1 "
                                      "AND r.code IN ('owner','super_admin') AND u.id<>?", {id}).toInt();
            if (admins == 0) throw ServiceError{QObject::tr("At least one active owner account is required.")};
        }
        exec("UPDATE users SET full_name=?, role_id=?, active=? WHERE id=?", {fullName.trimmed(), role, active ? 1 : 0, id});
        exec("UPDATE employees SET name=?, active=? WHERE user_id=?", {fullName.trimmed(), active ? 1 : 0, id});
        audit("user_updated", "user", id, roleCode + (active ? " active" : " inactive"));
        return Result::success(id);
    });
}

Result resetPassword(qint64 id, const QString &newPassword)
{
    return transaction([&] {
        require("users.manage");
        if (const QString p = passwordProblem(newPassword); !p.isEmpty()) throw ServiceError{p};
        const auto h = hashPassword(newPassword);
        exec("UPDATE users SET password_hash=?, password_salt=?, password_iterations=?, must_change_password=1, "
             "failed_attempts=0, locked_until=NULL WHERE id=?", {h.hash, h.salt, h.iterations, id});
        audit("password_reset", "user", id);
        return Result::success(id);
    });
}

Result setOverride(qint64 userId, const QString &permission, int state)
{
    return transaction([&] {
        require("users.manage");
        if (state < 0) exec("DELETE FROM user_permission_overrides WHERE user_id=? AND permission_code=?", {userId, permission});
        else exec("INSERT INTO user_permission_overrides(user_id, permission_code, allowed) VALUES(?,?,?) "
                  "ON CONFLICT(user_id, permission_code) DO UPDATE SET allowed=excluded.allowed", {userId, permission, state});
        audit("permission_override", "user", userId, permission + "=" + QString::number(state));
        return Result::success();
    });
}

Result setRolePermission(const QString &roleCode, const QString &permission, bool allowed)
{
    return transaction([&] {
        require("users.manage");
        if (roleCode == "owner" || roleCode == "super_admin") throw ServiceError{QObject::tr("Owner and Super Admin always have every permission.")};
        const qint64 role = scalar("SELECT id FROM roles WHERE code=?", {roleCode}).toLongLong();
        if (!role) throw ServiceError{QObject::tr("Unknown role.")};
        if (!scalar("SELECT COUNT(*) FROM permissions WHERE code=?", {permission}).toInt()) throw ServiceError{"unknown permission"};
        if (allowed) exec("INSERT OR IGNORE INTO role_permissions(role_id, permission_code) VALUES(?,?)", {role, permission});
        else exec("DELETE FROM role_permissions WHERE role_id=? AND permission_code=?", {role, permission});
        audit("role_permission", "role", role, permission + (allowed ? "=allow" : "=deny"));
        return Result::success();
    });
}

Result saveEmployee(qint64 id, const QString &name, const QString &phone, const QString &designation, const QString &joinedOn,
                    const QString &notes, bool active)
{
    return transaction([&] {
        require("employees.manage");
        if (name.trimmed().isEmpty()) throw ServiceError{QObject::tr("Name is required.")};
        if (id) exec("UPDATE employees SET name=?, phone=?, designation=?, joined_on=?, notes=?, active=? WHERE id=?",
                     {name.trimmed(), nullIf(phone), nullIf(designation), nullIf(joinedOn), nullIf(notes), active ? 1 : 0, id});
        else id = insert("INSERT INTO employees(name, phone, designation, joined_on, notes, active) VALUES(?,?,?,?,?,?)",
                         {name.trimmed(), nullIf(phone), nullIf(designation), nullIf(joinedOn), nullIf(notes), active ? 1 : 0});
        audit("employee_saved", "employee", id, name);
        return Result::success(id);
    });
}
}
}
