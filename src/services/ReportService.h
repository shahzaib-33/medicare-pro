#pragma once
#include <QSet>
#include <QStringList>
#include <QVector>
#include "Common.h"

namespace mcp {

struct ReportTable {
    QString code, title;
    QStringList headers;
    QVector<QVariantList> rows;
    QSet<int> moneyColumns;
    QSet<int> totalColumns;      // summed in the last line
    QStringList summary;         // label: value lines shown above the table
    QString cell(int row, int col) const;
    QVariantList totals() const;
    QString toCsv() const;
    QString toHtml(const QString &pharmacyName, const QString &period) const;
};

struct ReportDef { QString code, title, permission; bool usesDates = true; };

namespace ReportService {
QList<ReportDef> available();   // only reports the current user may open
// from and to are local dates (yyyy-MM-dd), inclusive.
ReportTable run(const QString &code, const QString &from, const QString &to);

// Dashboard figures for a period.
struct Kpis {
    Paisa sales = 0, returns = 0, cost = 0, expenses = 0, purchases = 0, profit = 0, receivable = 0, payable = 0;
    int invoices = 0, lowStock = 0, expiring30 = 0, expired = 0, pendingRx = 0, pendingInspections = 0, reviewFlags = 0;
};
Kpis kpis(const QString &from, const QString &to);
QVector<QPair<QString, Paisa>> dailySales(const QString &from, const QString &to);
QVector<QPair<QString, Paisa>> paymentSplit(const QString &from, const QString &to);
QVector<QPair<QString, int>> topMedicines(const QString &from, const QString &to, int limit = 8);
}
}
