#pragma once
#include <QSet>
#include <QStringList>
#include "Common.h"

namespace mcp {

struct UserInfo {
    qint64 id = 0;
    QString username, fullName, roleCode, roleName;
    bool active = true;
    bool mustChangePassword = false;
};

// Who is logged in, and what they may do. Checked in services, not only in the UI.
class Session {
public:
    static Session &instance();
    bool loggedIn() const { return m_user.id != 0; }
    const UserInfo &user() const { return m_user; }
    qint64 userId() const { return m_user.id; }
    bool can(const QString &permission) const { return m_perms.contains(permission); }
    void set(const UserInfo &u, const QSet<QString> &perms) { m_user = u; m_perms = perms; }
    void clear() { m_user = {}; m_perms.clear(); }
    // Test helper: act as a user with explicit permissions.
    void impersonate(qint64 id, const QSet<QString> &perms) { m_user.id = id; m_perms = perms; }
private:
    UserInfo m_user;
    QSet<QString> m_perms;
};

// Throws ServiceError when the current user lacks a permission.
void require(const QString &permission);
void audit(const QString &action, const QString &entity = {}, qint64 entityId = 0, const QString &details = {});

namespace Perm {
// Every permission in the app, with a description for the permission editor.
const QList<QPair<QString, QString>> &all();
}

namespace AuthService {
// PBKDF2-HMAC-SHA256 with a random 16 byte salt.
struct PasswordHash { QString hash, salt; int iterations; };
PasswordHash hashPassword(const QString &password, int iterations = 120000);
bool verifyPassword(const QString &password, const QString &hash, const QString &salt, int iterations);
QString passwordProblem(const QString &password);   // empty when acceptable

// Creates roles and permissions. Safe to call on every start.
void seedRolesAndPermissions();
bool isFirstRun();   // no users yet
Result createFirstOwner(const QString &username, const QString &fullName, const QString &password);

Result login(const QString &username, const QString &password);   // sets Session on success
void logout();
Result verifyCurrentUser(const QString &password);                  // unlock after auto lock
Result changePassword(const QString &oldPassword, const QString &newPassword);

Result createUser(const QString &username, const QString &fullName, const QString &password, const QString &roleCode);
Result updateUser(qint64 id, const QString &fullName, const QString &roleCode, bool active);
Result resetPassword(qint64 id, const QString &newPassword);
Result setOverride(qint64 userId, const QString &permission, int state);   // -1 remove, 0 deny, 1 allow
QSet<QString> effectivePermissions(qint64 userId);
// Changes a role's default permissions. Owner and Super Admin always keep every permission.
Result setRolePermission(const QString &roleCode, const QString &permission, bool allowed);
// Staff records. userId 0 = employee without a login.
Result saveEmployee(qint64 id, const QString &name, const QString &phone, const QString &designation, const QString &joinedOn,
                    const QString &notes, bool active);
}
}
