#pragma once
#include <QWidget>
#include "AppContext.h"
#include "services/ReportService.h"
class QListWidget;
class QTableWidget;
class QLabel;

namespace mcp {
namespace ui { class DateRangeBar; }

// Financial, sales, stock and audit reports with print, PDF and CSV export.
class ReportsPage : public QWidget {
    Q_OBJECT
public:
    explicit ReportsPage(AppContext *ctx, QWidget *parent = nullptr);
    // Opens one report by its code, for example sales_daily. Returns false when it is not available.
    bool selectReport(const QString &code);
private:
    void run();
    QListWidget *m_list;
    ui::DateRangeBar *m_range;
    QTableWidget *m_table;
    QLabel *m_summary;
    ReportTable m_current;
};
}
