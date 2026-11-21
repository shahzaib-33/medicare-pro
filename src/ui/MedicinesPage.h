#pragma once
#include <QTimer>
#include <QWidget>
#include "AppContext.h"
class QLineEdit;
class QLabel;
class QFrame;
class QCheckBox;

namespace mcp {
namespace ui { class SqlTable; }

// The pharmacy's own medicine list: add, edit, archive, restore, delete, units, barcodes, import and export.
class MedicinesPage : public QWidget {
    Q_OBJECT
public:
    MedicinesPage(AppContext *ctx, QWidget *parent = nullptr);
public slots:
    void reload();
    void openCatalog();
    void openReview();
private:
    void editMedicine(qint64 id);
    void editUnits(qint64 id);
    void openingStock(qint64 id);
    void importCsv();
    AppContext *m_ctx;
    ui::SqlTable *m_table;
    QLineEdit *m_filter;
    QCheckBox *m_archived;
    QLabel *m_flagText;
    QFrame *m_flagBanner;
    QTimer m_debounce;
};
}
