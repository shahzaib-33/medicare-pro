#pragma once
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QIdentityProxyModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSqlQueryModel>
#include <QTableView>
#include <QVBoxLayout>
#include "core/Money.h"
#include "services/Common.h"

namespace mcp::ui {

// Page frame: title on the left, an action row on the right.
struct PageFrame { QVBoxLayout *body; QHBoxLayout *actions; QLabel *title; };
PageFrame page(QWidget *w, const QString &title);
QPushButton *button(const QString &text, QWidget *parent, bool primary = false);
QLabel *muted(const QString &text, QWidget *parent);

// Line icons built into the app (Lucide, ISC License), tinted to any colour.
QPixmap iconPixmap(const QString &name, const QColor &color, int size = 20);
QIcon icon(const QString &name, const QColor &normal, const QColor &selected = Qt::white, int size = 20);
// A round badge with a tinted icon, used on dashboard cards and the sign in screen.
QLabel *iconBadge(const QString &name, const QColor &fg, const QColor &bg, int size, QWidget *parent);

void error(QWidget *parent, const QString &text);
void info(QWidget *parent, const QString &text);
bool confirm(QWidget *parent, const QString &text);
bool check(QWidget *parent, const Result &r);     // shows the error, returns r.ok
QString askText(QWidget *parent, const QString &title, const QString &label, bool *ok, const QString &initial = {});
bool can(const QString &permission);

// Money entry in rupees with up to 2 decimals. Stores whole paisa.
class MoneyEdit : public QLineEdit {
    Q_OBJECT
public:
    explicit MoneyEdit(QWidget *parent = nullptr);
    Paisa value() const;
    bool isValid() const;
    void setValue(Paisa p);
signals:
    void valueChanged(Paisa p);
};

QDateEdit *dateEdit(const QDate &d, QWidget *parent);

// Formats money columns and right aligns numbers. Column 0 of the query is usually a hidden id.
class FormatProxy : public QIdentityProxyModel {
public:
    using QIdentityProxyModel::QIdentityProxyModel;
    QSet<int> money, numbers;
    QHash<int, std::function<QString(const QVariant &)>> formatters;
    std::function<QColor(const QModelIndex &)> rowColor;
    QVariant data(const QModelIndex &index, int role) const override;
};

// Read only table backed by a SQL query.
class SqlTable : public QTableView {
    Q_OBJECT
public:
    explicit SqlTable(QWidget *parent = nullptr);
    void setQuery(const QString &sql, const QVariantList &binds, const QStringList &headers);
    void reload();
    qint64 currentId() const;          // value of column 0 in the selected row
    QVariant currentValue(int column) const;
    FormatProxy *format() const { return m_proxy; }
    QSqlQueryModel *sqlModel() const { return m_model; }
    bool hideIdColumn = true;
signals:
    void activatedId(qint64 id);
private:
    QSqlQueryModel *m_model;
    FormatProxy *m_proxy;
    QString m_sql;
    QVariantList m_binds;
    QStringList m_headers;
};

// Today, Yesterday, This week, This month, Last month, This year, Custom.
class DateRangeBar : public QWidget {
    Q_OBJECT
public:
    explicit DateRangeBar(QWidget *parent = nullptr, int defaultPreset = 0);
    QString from() const;
    QString to() const;
    QString label() const;
signals:
    void changed();
private:
    void applyPreset(int i);
    QComboBox *m_preset;
    QDateEdit *m_from, *m_to;
};

// Saves a table model to CSV in the format Excel opens directly (UTF-8 with BOM).
bool exportModelCsv(QWidget *parent, QAbstractItemModel *model, const QString &suggestedName, bool skipFirstColumn);
bool saveText(QWidget *parent, const QString &content, const QString &suggestedName, const QString &filter);
// Prints or saves HTML. pageWidthMm 0 = A4.
void printHtml(QWidget *parent, const QString &html, double pageWidthMm, bool preview);
bool saveHtmlAsPdf(QWidget *parent, const QString &html, double pageWidthMm, const QString &suggestedName);
}
