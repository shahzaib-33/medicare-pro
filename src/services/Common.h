#pragma once
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QVariant>
#include <initializer_list>
#include "core/Money.h"
#include "db/Database.h"

namespace mcp {

struct Result {
    bool ok = true;
    QString error;
    qint64 id = 0;
    static Result fail(const QString &e) { Result r; r.ok = false; r.error = e; return r; }
    static Result success(qint64 id = 0) { Result r; r.id = id; return r; }
    explicit operator bool() const { return ok; }
};

// Thrown inside service transactions and turned into a Result with a full rollback.
struct ServiceError { QString message; };

inline QSqlDatabase pharmacyDb() { return Db::get(Db::Pharmacy); }

// Runs a prepared statement. Throws ServiceError on failure.
QSqlQuery exec(const QString &sql, std::initializer_list<QVariant> binds = {});
qint64 insert(const QString &sql, std::initializer_list<QVariant> binds = {});
QVariant scalar(const QString &sql, std::initializer_list<QVariant> binds = {}, const QVariant &def = {});

// Runs fn inside one transaction. Any ServiceError or failed commit rolls everything back.
template <typename Fn>
Result transaction(Fn &&fn)
{
    QSqlDatabase db = pharmacyDb();
    if (!db.transaction()) return Result::fail(db.lastError().text());
    try {
        Result r = fn();
        if (!r.ok) { db.rollback(); return r; }
        if (!db.commit()) { const QString e = db.lastError().text(); db.rollback(); return Result::fail(e); }
        return r;
    } catch (const ServiceError &e) {
        db.rollback();
        return Result::fail(e.message);
    }
}

inline QVariant nullIf(const QString &s) { return s.trimmed().isEmpty() ? QVariant() : QVariant(s.trimmed()); }
inline QVariant nullIfZero(qint64 v) { return v == 0 ? QVariant() : QVariant(v); }
QString todayIso();
}
