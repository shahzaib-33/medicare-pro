#include "DashboardPage.h"
#include "MedicineDbCard.h"
#include "UpdateCenterPage.h"
#include "Ui.h"
#include "Theme.h"
#include "services/ReportService.h"
#include <QBarCategoryAxis>
#include <QBarSeries>
#include <QBarSet>
#include <QChart>
#include <QChartView>
#include <QGridLayout>
#include <QHorizontalBarSeries>
#include <QPieSeries>
#include <QAbstractBarSeries>
#include <QValueAxis>
#include <QApplication>
#include <QPen>

namespace mcp {

DashboardPage::DashboardPage(AppContext *ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    auto f = ui::page(this, tr("Dashboard"));
    m_range = new ui::DateRangeBar(this, 0);
    f.actions->addWidget(m_range);

    auto *grid = new QGridLayout;
    grid->setSpacing(Theme::isClassic() ? 4 : 14);
    const bool fin = ui::can("dashboard.financial");
    struct Card { QString key, caption; bool financial; QString icon; QString color; };
    // Colours: indigo for sales, green for money in, blue for stock, amber for warnings, red for problems.
    const QList<Card> cards = {
        {"sales", tr("Sales"), false, "trending-up", "#4338ca"}, {"invoices", tr("Invoices"), false, "receipt", "#0e7490"},
        {"profit", tr("Profit after expenses"), true, "coins", "#15803d"}, {"expenses", tr("Expenses"), true, "wallet", "#b45309"},
        {"purchases", tr("Purchases"), true, "truck", "#1d4ed8"}, {"returns", tr("Sales returns"), false, "undo-2", "#be185d"},
        {"receivable", tr("Customers owe you"), true, "hand-coins", "#15803d"}, {"payable", tr("You owe suppliers"), true, "building-2", "#7c3aed"},
        {"lowStock", tr("Medicines at or below minimum"), false, "package", "#b45309"},
        {"expiring30", tr("Batches expiring in 30 days"), false, "clock", "#c2410c"},
        {"expired", tr("Expired batches with stock"), false, "triangle-alert", "#b91c1c"},
        {"pendingRx", tr("Prescriptions waiting"), false, "clipboard-list", "#0e7490"},
        {"inspect", tr("Returns to inspect"), false, "eye", "#6d28d9"}, {"flags", tr("Medicines needing review"), false, "bell", "#be185d"}};
    int i = 0;
    for (const Card &c : cards) {
        if (c.financial && !fin) continue;
        auto *card = new QFrame(this);
        card->setObjectName("statCard");
        auto *ch = new QHBoxLayout(card);
        if (Theme::isClassic()) { ch->setContentsMargins(8, 5, 8, 5); ch->setSpacing(8); }
        else { ch->setContentsMargins(16, 14, 16, 14); ch->setSpacing(14); }
        QColor bgc(c.color);
        bgc.setAlpha(28);
        ch->addWidget(ui::iconBadge(c.icon, QColor(c.color), bgc, 44, card), 0, Qt::AlignTop);
        auto *cv = new QVBoxLayout;
        cv->setSpacing(2);
        auto *cap = new QLabel(c.caption, card);
        cap->setObjectName("statCaption");
        cap->setWordWrap(true);
        auto *val = new QLabel("0", card);
        val->setObjectName("statValue");
        cv->addWidget(cap);
        cv->addWidget(val);
        cv->addStretch();
        ch->addLayout(cv, 1);
        m_values.insert(c.key, val);
        grid->addWidget(card, i / 4, i % 4);
        ++i;
    }
    f.body->addLayout(grid);

    auto makeView = [this] {
        auto *v = new QChartView(this);
        v->setRenderHint(QPainter::Antialiasing, !m_ctx->perf.lite());
        v->setMinimumHeight(260);
        return v;
    };
    m_salesChart = makeView();
    m_payChart = makeView();
    m_topChart = makeView();
    auto *charts = new QGridLayout;
    charts->addWidget(m_salesChart, 0, 0, 1, 2);
    charts->addWidget(m_payChart, 1, 0);
    charts->addWidget(m_topChart, 1, 1);
    f.body->addLayout(charts);

    auto *card = new MedicineDbCard(ctx, this);
    f.body->addWidget(card);
    f.body->addStretch();
    connect(card, &MedicineDbCard::browseRequested, this, [] { emit AppEvents::instance()->navigate(Pages::Catalog); });
    connect(card, &MedicineDbCard::historyRequested, this, [] { emit AppEvents::instance()->navigate(Pages::MedicineUpdates); });
    connect(card, &MedicineDbCard::updateNowRequested, this, [this] {
        emit AppEvents::instance()->navigate(Pages::MedicineUpdates);
        if (auto *p = window()->findChild<UpdateCenterPage *>()) p->check();
    });
    connect(m_range, &ui::DateRangeBar::changed, this, &DashboardPage::reload);
    connect(AppEvents::instance(), &AppEvents::pharmacyChanged, this, [this] { if (isVisible()) reload(); });
    reload();
}

void DashboardPage::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    reload();
}

static QChart *baseChart(const QString &title, bool lite)
{
    auto *c = new QChart;
    c->setTitle(title);
    c->setAnimationOptions(lite ? QChart::NoAnimation : QChart::SeriesAnimations);
    c->legend()->setVisible(false);
    c->setBackgroundRoundness(14);
    c->setDropShadowEnabled(false);
    QFont tf = c->titleFont();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() + 1);
    c->setTitleFont(tf);
    c->setMargins(QMargins(6, 6, 6, 6));
    return c;
}

static void styleChart(QChart *c, bool dark)
{
    if (Theme::isClassic()) {
        // Plain white chart on a grey panel, black text, no rounding.
        c->setBackgroundRoundness(0);
        c->setDropShadowEnabled(false);
        c->setBackgroundBrush(Qt::white);
        c->setBackgroundPen(QPen(QColor(Theme::isXp() ? "#d0d0bf" : "#808080")));
        c->setAnimationOptions(QChart::NoAnimation);
        QFont tf = qApp->font();
        tf.setBold(true);
        c->setTitleFont(tf);
        c->setTitleBrush(Qt::black);
        for (QAbstractAxis *a : c->axes()) {
            a->setLabelsFont(qApp->font());
            a->setLabelsColor(Qt::black);
            a->setGridLineColor(QColor("#c0c0c0"));
            a->setLinePenColor(Qt::black);
        }
        for (QAbstractSeries *s : c->series()) {
            if (auto *b = qobject_cast<QAbstractBarSeries *>(s))
                for (QBarSet *set : b->barSets()) { set->setColor(QColor(Theme::isXp() ? "#2f6fd6" : "#000080")); set->setBorderColor(Theme::isXp() ? QColor("#1d3f80") : QColor(Qt::black)); }
        }
        return;
    }
    c->setBackgroundRoundness(14);
    c->setDropShadowEnabled(false);
    c->setBackgroundBrush(QColor(dark ? "#181c2e" : "#ffffff"));
    c->setBackgroundPen(QPen(QColor(dark ? "#2a3047" : "#e5e7eb")));
    QFont tf = qApp->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() + 1);
    c->setTitleFont(tf);
    c->setTitleBrush(QColor(dark ? "#e5e7eb" : "#111827"));
    for (QAbstractAxis *a : c->axes()) {
        a->setLabelsFont(qApp->font());
        a->setGridLineColor(QColor(dark ? "#2a3047" : "#eef0f4"));
        a->setLinePenColor(QColor(dark ? "#2a3047" : "#e5e7eb"));
    }
}

void DashboardPage::reload()
{
    const QString from = m_range->from(), to = m_range->to();
    const auto k = ReportService::kpis(from, to);
    auto set = [this](const QString &key, const QString &text) { if (auto *l = m_values.value(key)) l->setText(text); };
    set("sales", Money::format(k.sales));
    set("invoices", QString::number(k.invoices));
    set("profit", Money::format(k.profit));
    set("expenses", Money::format(k.expenses));
    set("purchases", Money::format(k.purchases));
    set("returns", Money::format(k.returns));
    set("receivable", Money::format(k.receivable));
    set("payable", Money::format(k.payable));
    set("lowStock", QString::number(k.lowStock));
    set("expiring30", QString::number(k.expiring30));
    set("expired", QString::number(k.expired));
    set("pendingRx", QString::number(k.pendingRx));
    set("inspect", QString::number(k.pendingInspections));
    set("flags", QString::number(k.reviewFlags));
    if (auto *l = m_values.value("expired")) l->setStyleSheet(k.expired ? "color:#b42318" : "");
    if (auto *l = m_values.value("profit")) l->setStyleSheet(k.profit < 0 ? "color:#b42318" : "");

    const bool lite = m_ctx->perf.lite();
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    const auto theme = dark ? QChart::ChartThemeDark : QChart::ChartThemeLight;

    // Daily sales
    {
        const auto days = ReportService::dailySales(from, to);
        auto *set = new QBarSet(tr("Sales"));
        set->setColor(QColor("#4338ca"));
        QStringList cats;
        qint64 maxV = 0;
        for (const auto &d : days) {
            *set << double(d.second) / 100.0;
            cats << QDate::fromString(d.first, Qt::ISODate).toString(days.size() > 31 ? "MM-dd" : "dd MMM");
            maxV = qMax(maxV, d.second);
        }
        auto *series = new QBarSeries;
        series->append(set);
        auto *c = baseChart(tr("Sales by day (Rs)"), lite);
        c->setTheme(theme);
        styleChart(c, dark);
        set->setColor(QColor("#4338ca"));
        c->addSeries(series);
        auto *ax = new QBarCategoryAxis;
        ax->append(cats);
        if (cats.size() > 16) ax->setLabelsAngle(-60);
        auto *ay = new QValueAxis;
        ay->setRange(0, qMax(1.0, maxV / 100.0 * 1.15));
        ay->setLabelFormat("%.0f");
        c->addAxis(ax, Qt::AlignBottom);
        c->addAxis(ay, Qt::AlignLeft);
        series->attachAxis(ax);
        series->attachAxis(ay);
        styleChart(c, dark);
        QChart *old = m_salesChart->chart();
        m_salesChart->setChart(c);
        delete old;
    }
    // Payment methods
    {
        auto *series = new QPieSeries;
        const QStringList colors = Theme::isXp() ? QStringList{"#2f6fd6", "#3aa04a", "#e3801c", "#d9372b", "#7d4fc4", "#1f9db0", "#888888"}
                                 : Theme::isClassic() ? QStringList{"#000080", "#008080", "#008000", "#800000", "#808000", "#800080", "#808080"}
                                                   : QStringList{"#4338ca", "#0ea5e9", "#16a34a", "#f59e0b", "#db2777", "#8b5cf6", "#64748b"};
        int i = 0;
        for (const auto &p : ReportService::paymentSplit(from, to)) {
            auto *slice = series->append(p.first + " " + Money::format(p.second), double(p.second));
            slice->setColor(QColor(colors[i++ % colors.size()]));
            slice->setLabelVisible(true);
        }
        series->setHoleSize(0.45);
        auto *c = baseChart(tr("Payments by method"), lite);
        c->setTheme(theme);
        styleChart(c, dark);
        c->addSeries(series);
        i = 0;
        for (auto *s : series->slices()) s->setColor(QColor(colors[i++ % colors.size()]));
        styleChart(c, dark);
        QChart *old = m_payChart->chart();
        m_payChart->setChart(c);
        delete old;
    }
    // Top medicines
    {
        const auto top = ReportService::topMedicines(from, to, 8);
        auto *set = new QBarSet(tr("Units"));
        QStringList cats;
        int maxV = 0;
        for (int i = top.size() - 1; i >= 0; --i) {
            *set << top[i].second;
            cats << top[i].first.left(22);
            maxV = qMax(maxV, top[i].second);
        }
        auto *series = new QHorizontalBarSeries;
        series->append(set);
        auto *c = baseChart(tr("Top selling medicines (units)"), lite);
        c->setTheme(theme);
        styleChart(c, dark);
        set->setColor(QColor("#0ea5e9"));
        c->addSeries(series);
        auto *ay = new QBarCategoryAxis;
        ay->append(cats);
        auto *ax = new QValueAxis;
        ax->setRange(0, qMax(1, int(maxV * 1.15)));
        ax->setLabelFormat("%d");
        c->addAxis(ay, Qt::AlignLeft);
        c->addAxis(ax, Qt::AlignBottom);
        series->attachAxis(ax);
        series->attachAxis(ay);
        styleChart(c, dark);
        QChart *old = m_topChart->chart();
        m_topChart->setChart(c);
        delete old;
    }
}
}
