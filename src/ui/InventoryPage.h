#pragma once
#include <QWidget>
#include "AppContext.h"
class QComboBox;
class QLabel;
class QTabWidget;

namespace mcp {
namespace ui { class SqlTable; class DateRangeBar; }

// Batches, expiry alerts (7, 30, 60, 90 days), adjustments, returned stock inspection, stock counts, movement log.
class InventoryPage : public QWidget {
    Q_OBJECT
public:
    explicit InventoryPage(AppContext *ctx, QWidget *parent = nullptr);
    void reload();
private:
    QWidget *buildBatches();
    QWidget *buildInspections();
    QWidget *buildCounts();
    QWidget *buildMovements();
    void adjust();
    ui::SqlTable *m_batches, *m_inspect, *m_count, *m_moves;
    QComboBox *m_window;
    QLabel *m_alert[5];
    ui::DateRangeBar *m_moveRange;
    qint64 m_countId = 0;
    QLabel *m_countInfo;
};
}
