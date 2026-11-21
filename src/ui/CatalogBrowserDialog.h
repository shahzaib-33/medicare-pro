#pragma once
#include <QDialog>
#include <QTimer>
#include <QVector>
#include "AppContext.h"
#include "refdata/MedicineRecord.h"
class QLineEdit;
class QTableWidget;
class QLabel;
class QPushButton;

namespace mcp {
// "Add Medicine from Pakistan Database": search the reference catalog and add to the pharmacy.
class CatalogBrowserDialog : public QDialog {
    Q_OBJECT
public:
    CatalogBrowserDialog(AppContext *ctx, bool allowAdd, QWidget *parent = nullptr);
    void setQuery(const QString &q);
    void setEmbedded();   // used as a page inside the main window

private slots:
    void runSearch();
    void showSelected();
    void addSelected();

private:
    AppContext *m_ctx;
    QLineEdit *m_search;
    QTableWidget *m_table;
    QLabel *m_details, *m_suggest, *m_coverage, *m_count;
    QPushButton *m_add, *m_close;
    QTimer m_debounce;
    QVector<CatalogHit> m_hits;
    qint64 selectedId() const;
};
}
