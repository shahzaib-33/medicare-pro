#include "CatalogBrowserDialog.h"
#include "AddToPharmacyDialog.h"
#include "db/Database.h"
#include "refdata/CatalogStats.h"
#include "refdata/MedicineSearch.h"
#include <QHBoxLayout>
#include <QLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace mcp {

static QString statusText(const QString &s)
{
    if (s == "active") return QObject::tr("Active");
    if (s == "cancelled") return QObject::tr("Cancelled");
    if (s == "suspended") return QObject::tr("Suspended");
    if (s == "discontinued") return QObject::tr("Discontinued");
    return QObject::tr("Not stated");
}

static QString verificationText(const QString &s)
{
    if (s == "verified") return QObject::tr("Verified");
    if (s == "sample") return QObject::tr("Sample data, not for use");
    return QObject::tr("Unverified");
}

CatalogBrowserDialog::CatalogBrowserDialog(AppContext *ctx, bool allowAdd, QWidget *parent)
    : QDialog(parent), m_ctx(ctx)
{
    setWindowTitle(tr("Pakistan Medicine Database"));
    resize(1100, 680);
    auto *v = new QVBoxLayout(this);
    auto *title = new QLabel(tr("Search the Pakistan medicine reference list"), this);
    title->setProperty("role", "h1");
    v->addWidget(title);
    m_coverage = new QLabel(this);
    m_coverage->setProperty("role", "muted");
    m_coverage->setWordWrap(true);
    m_coverage->setText(CatalogStats::load(Db::get(Db::Reference)).coverageText());
    v->addWidget(m_coverage);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Brand, generic, Urdu name, manufacturer, strength, registration no or barcode"));
    m_search->setClearButtonEnabled(true);
    v->addWidget(m_search);
    auto *row = new QHBoxLayout;
    m_count = new QLabel(this);
    m_count->setProperty("role", "muted");
    m_suggest = new QLabel(this);
    m_suggest->setTextFormat(Qt::RichText);
    row->addWidget(m_count);
    row->addStretch();
    row->addWidget(m_suggest);
    v->addLayout(row);

    auto *split = new QSplitter(this);
    m_table = new QTableWidget(0, 7, split);
    m_table->setHorizontalHeaderLabels({tr("Brand"), tr("Urdu name"), tr("Generic"), tr("Strength"), tr("Form"),
                                        tr("Manufacturer"), tr("Registration")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_details = new QLabel(split);
    m_details->setWordWrap(true);
    m_details->setAlignment(Qt::AlignTop);
    m_details->setMinimumWidth(300);
    m_details->setTextFormat(Qt::RichText);
    m_details->setText(tr("Select a medicine to see its details."));
    split->addWidget(m_table);
    split->addWidget(m_details);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    v->addWidget(split, 1);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    m_add = new QPushButton(tr("Add to My Pharmacy"), this);
    m_add->setProperty("role", "primary");
    m_add->setEnabled(false);
    m_add->setVisible(allowAdd);
    m_close = new QPushButton(tr("Close"), this);
    auto *close = m_close;
    buttons->addWidget(m_add);
    buttons->addWidget(close);
    v->addLayout(buttons);

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(m_ctx->perf.searchDebounceMs());
    connect(m_search, &QLineEdit::textChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(m_search, &QLineEdit::returnPressed, this, &CatalogBrowserDialog::runSearch);
    connect(&m_debounce, &QTimer::timeout, this, &CatalogBrowserDialog::runSearch);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &CatalogBrowserDialog::showSelected);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this] { if (m_add->isVisible()) addSelected(); });
    connect(m_add, &QPushButton::clicked, this, &CatalogBrowserDialog::addSelected);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_suggest, &QLabel::linkActivated, this, [this](const QString &s) { m_search->setText(s); runSearch(); });
    m_search->setFocus();
}

void CatalogBrowserDialog::setEmbedded()
{
    setWindowFlags(Qt::Widget);
    m_close->hide();
    layout()->setContentsMargins(24, 20, 24, 20);
}

void CatalogBrowserDialog::setQuery(const QString &q) { m_search->setText(q); runSearch(); }

void CatalogBrowserDialog::runSearch()
{
    m_debounce.stop();
    const QString q = m_search->text().trimmed();
    m_table->setRowCount(0);
    m_hits.clear();
    m_suggest->clear();
    m_add->setEnabled(false);
    if (q.isEmpty()) { m_count->clear(); return; }
    const auto res = MedicineSearch(Db::get(Db::Reference)).search(q, m_ctx->perf.searchPageSize());
    m_hits = res.hits;
    m_table->setUpdatesEnabled(false);
    m_table->setRowCount(m_hits.size());
    for (int i = 0; i < m_hits.size(); ++i) {
        const auto &h = m_hits[i];
        const QStringList cols = {h.brandName, h.brandNameUr, h.genericName, h.strength, h.dosageForm,
                                  h.manufacturer, h.registrationNo.isEmpty() ? tr("Not available") : h.registrationNo};
        for (int c = 0; c < cols.size(); ++c) {
            auto *it = new QTableWidgetItem(cols[c]);
            if (h.registrationStatus == "cancelled" || h.registrationStatus == "suspended")
                it->setForeground(QColor("#b42318"));
            m_table->setItem(i, c, it);
        }
    }
    m_table->setUpdatesEnabled(true);
    m_table->resizeColumnsToContents();
    m_count->setText(res.totalCandidates > m_hits.size()
                         ? tr("Showing %1 of %2 matches. Type more to narrow the list.").arg(m_hits.size()).arg(res.totalCandidates)
                         : tr("%n match(es)", nullptr, int(m_hits.size())));
    if (!res.suggestions.isEmpty()) {
        QStringList links;
        for (const QString &s : res.suggestions) links << QStringLiteral("<a href=\"%1\">%1</a>").arg(s.toHtmlEscaped());
        m_suggest->setText(tr("Did you mean: %1").arg(links.join(", ")));
    }
    if (!m_hits.isEmpty()) {
        m_table->setCurrentCell(0, 0);
        m_table->selectRow(0);
        showSelected();
    } else {
        m_details->setText(tr("Select a medicine to see its details."));
    }
}

qint64 CatalogBrowserDialog::selectedId() const
{
    const int r = m_table->currentRow();
    return (r >= 0 && r < m_hits.size()) ? m_hits[r].id : 0;
}

void CatalogBrowserDialog::showSelected()
{
    const qint64 id = selectedId();
    MedicineRecord rec;
    if (!id || !MedicineSearch(Db::get(Db::Reference)).loadRecord(id, &rec)) {
        m_add->setEnabled(false);
        return;
    }
    auto row = [](const QString &k, const QString &v) {
        return QStringLiteral("<tr><td style='color:#6b6680;padding-right:10px'>%1</td><td>%2</td></tr>")
            .arg(k.toHtmlEscaped(), v.isEmpty() ? QObject::tr("Not available").toHtmlEscaped() : v.toHtmlEscaped());
    };
    QString html = QStringLiteral("<h3>%1</h3><table>").arg(rec.brandName.toHtmlEscaped());
    html += row(tr("Urdu brand name"), rec.brandNameUr);
    html += row(tr("Generic name"), rec.genericName);
    html += row(tr("Active ingredients"), rec.ingredients.join(", "));
    html += row(tr("Composition"), rec.composition);
    html += row(tr("Strength"), rec.strength);
    html += row(tr("Dosage form"), rec.dosageForm);
    html += row(tr("Manufacturer"), rec.manufacturer);
    html += row(tr("Marketing authorization holder"), rec.mahName);
    html += row(tr("DRAP registration number"), rec.registrationNo);
    html += row(tr("Registration date"), rec.registrationDate);
    html += row(tr("Registration status"), statusText(rec.registrationStatus));
    html += row(tr("Category"), rec.category);
    html += row(tr("Pack size"), rec.packSize);
    html += row(tr("Barcode"), rec.barcode);
    html += row(tr("Source"), rec.sourceCode);
    html += row(tr("Verification status"), verificationText(rec.verificationStatus));
    html += row(tr("Verified on"), rec.verifiedAt);
    html += "</table>";
    if (rec.registrationStatus == "cancelled" || rec.registrationStatus == "suspended")
        html += "<p style='color:#b42318'>" + tr("The official registration of this product is not active.").toHtmlEscaped() + "</p>";
    m_details->setText(html);
    m_add->setEnabled(true);
}

void CatalogBrowserDialog::addSelected()
{
    const qint64 id = selectedId();
    MedicineRecord rec;
    if (!id || !MedicineSearch(Db::get(Db::Reference)).loadRecord(id, &rec)) return;
    AddToPharmacyDialog dlg(rec, this);
    if (dlg.exec() == QDialog::Accepted) emit AppEvents::instance()->pharmacyChanged();
}
}
