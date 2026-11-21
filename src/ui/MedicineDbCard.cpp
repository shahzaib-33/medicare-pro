#include "MedicineDbCard.h"
#include "db/Database.h"
#include "net/ConnectivityMonitor.h"
#include "refdata/CatalogImporter.h"
#include "refdata/CatalogStats.h"
#include "update/UpdateClient.h"
#include <QGridLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

namespace mcp {

static QLabel *statLabel(QWidget *p) { auto *l = new QLabel("-", p); l->setProperty("role", "stat"); return l; }
static QLabel *captionLabel(const QString &t, QWidget *p) { auto *l = new QLabel(t, p); l->setProperty("role", "muted"); return l; }

MedicineDbCard::MedicineDbCard(AppContext *ctx, QWidget *parent) : QFrame(parent), m_ctx(ctx)
{
    setProperty("role", "card");
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(20, 18, 20, 18);
    auto *title = new QLabel(tr("PAKISTAN MEDICINE DATABASE"), this);
    title->setProperty("role", "h2");
    v->addWidget(title);

    auto *g = new QGridLayout;
    g->setHorizontalSpacing(28);
    m_total = statLabel(this); m_version = statLabel(this); m_lastUpdate = statLabel(this);
    m_newAvail = statLabel(this); m_pending = statLabel(this); m_conn = statLabel(this);
    const QList<QPair<QLabel *, QString>> cells = {
        {m_total, tr("Total downloaded medicines")}, {m_version, tr("Latest database version")},
        {m_lastUpdate, tr("Last update date")}, {m_newAvail, tr("New medicines available")},
        {m_pending, tr("Pending updates")}, {m_conn, tr("Connection status")}};
    for (int i = 0; i < cells.size(); ++i) {
        g->addWidget(cells[i].first, (i / 3) * 2, i % 3);
        g->addWidget(captionLabel(cells[i].second, this), (i / 3) * 2 + 1, i % 3);
    }
    v->addLayout(g);
    m_coverage = captionLabel({}, this);
    m_coverage->setWordWrap(true);
    v->addWidget(m_coverage);

    auto *h = new QHBoxLayout;
    auto *update = new QPushButton(tr("Update Now"), this);
    update->setProperty("role", "primary");
    auto *check = new QPushButton(tr("Check for Updates"), this);
    auto *browse = new QPushButton(tr("Browse Medicine Database"), this);
    auto *hist = new QPushButton(tr("View Update History"), this);
    for (auto *b : {update, check, browse, hist}) h->addWidget(b);
    h->addStretch();
    v->addLayout(h);
    connect(update, &QPushButton::clicked, this, &MedicineDbCard::updateNowRequested);
    connect(check, &QPushButton::clicked, this, [this] { if (m_ctx->updates) m_ctx->updates->checkNow(); });
    connect(browse, &QPushButton::clicked, this, &MedicineDbCard::browseRequested);
    connect(hist, &QPushButton::clicked, this, &MedicineDbCard::historyRequested);
    connect(AppEvents::instance(), &AppEvents::referenceChanged, this, &MedicineDbCard::refresh);
    if (m_ctx->network) connect(m_ctx->network, &ConnectivityMonitor::onlineChanged, this, &MedicineDbCard::refresh);
    if (m_ctx->updates) connect(m_ctx->updates, &UpdateClient::checkFinished, this, &MedicineDbCard::refresh);
    refresh();
}

void MedicineDbCard::refresh()
{
    const auto s = CatalogStats::load(Db::get(Db::Reference));
    QLocale loc;
    m_total->setText(loc.toString(s.totalMedicines));
    m_version->setText(s.version ? QString::number(s.version) : tr("None"));
    const QDateTime last = QDateTime::fromString(s.lastUpdate, Qt::ISODate);
    m_lastUpdate->setText(last.isValid() ? loc.toString(last.toLocalTime().date(), QLocale::ShortFormat) : tr("Never"));
    int pending = 0;
    QString newAvail = tr("Unknown");
    if (m_ctx->updates && m_ctx->updates->hasManifest()) {
        const int cur = CatalogImporter::currentVersion(Db::get(Db::Reference));
        for (const auto &i : m_ctx->updates->manifest().items)
            if (i.type.startsWith("reference") && i.version.toInt() > cur) ++pending;
        newAvail = pending ? tr("Yes") : tr("No");
    }
    m_newAvail->setText(newAvail);
    m_pending->setText(QString::number(pending));
    const bool online = m_ctx->network && m_ctx->network->isOnline();
    m_conn->setText(online ? tr("Online") : tr("Offline"));
    m_coverage->setText(s.coverageText());
}
}
