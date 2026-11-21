#pragma once
#include <QWidget>
#include "AppContext.h"
class QLabel;
class QChartView;
class QGridLayout;

namespace mcp {
namespace ui { class DateRangeBar; }

// Live figures from the database with Today, Yesterday, This week, This month and custom ranges.
class DashboardPage : public QWidget {
    Q_OBJECT
public:
    explicit DashboardPage(AppContext *ctx, QWidget *parent = nullptr);
    void reload();
protected:
    void showEvent(QShowEvent *e) override;
private:
    AppContext *m_ctx;
    ui::DateRangeBar *m_range;
    QHash<QString, QLabel *> m_values;
    QChartView *m_salesChart, *m_payChart, *m_topChart;
};
}
