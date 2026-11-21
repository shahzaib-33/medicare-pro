#include "Common.h"
#include <QDate>

namespace mcp {

QSqlQuery exec(const QString &sql, std::initializer_list<QVariant> binds)
{
    QSqlQuery q(pharmacyDb());
    if (!q.prepare(sql)) throw ServiceError{q.lastError().text() + " | " + sql.left(120)};
    for (const QVariant &b : binds) q.addBindValue(b);
    if (!q.exec()) throw ServiceError{q.lastError().text() + " | " + sql.left(120)};
    return q;
}

qint64 insert(const QString &sql, std::initializer_list<QVariant> binds)
{
    return exec(sql, binds).lastInsertId().toLongLong();
}

QVariant scalar(const QString &sql, std::initializer_list<QVariant> binds, const QVariant &def)
{
    QSqlQuery q = exec(sql, binds);
    return q.next() ? q.value(0) : def;
}

QString todayIso() { return QDate::currentDate().toString(Qt::ISODate); }
}
