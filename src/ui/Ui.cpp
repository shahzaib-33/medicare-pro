#include "Ui.h"
#include "Theme.h"
#include <QApplication>
#include <QPainter>
#include "services/AuthService.h"
#include <QAbstractTextDocumentLayout>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QRegularExpressionValidator>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTextDocument>

namespace mcp::ui {

PageFrame page(QWidget *w, const QString &title)
{
    auto *v = new QVBoxLayout(w);
    auto *top = new QHBoxLayout;
    auto *t = new QLabel(title, w);
    t->setProperty("role", "h1");
    auto *actions = new QHBoxLayout;
    if (Theme::isClassic()) {
        // Classic: a blue caption bar across the page, buttons in a row under it.
        v->setContentsMargins(4, 4, 4, 4);
        v->setSpacing(4);
        actions->setSpacing(3);
        v->addWidget(t);
        top->addLayout(actions);
        top->addStretch();
        v->addLayout(top);
        return {v, actions, t};
    }
    v->setContentsMargins(24, 20, 24, 20);
    v->setSpacing(10);
    top->addWidget(t);
    top->addStretch();
    top->addLayout(actions);
    v->addLayout(top);
    return {v, actions, t};
}

QPushButton *button(const QString &text, QWidget *parent, bool primary)
{
    auto *b = new QPushButton(text, parent);
    if (primary) b->setProperty("role", "primary");
    return b;
}

QPixmap iconPixmap(const QString &name, const QColor &color, int size)
{
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    if (Theme::isXp()) {
        // Windows 2003 look: full colour icons instead of one tinted colour.
        const QImage xp(QStringLiteral(":/icons/xp/%1.png").arg(name));
        if (!xp.isNull()) {
            QPixmap pm = QPixmap::fromImage(xp.scaled(qRound(size * dpr), qRound(size * dpr), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            pm.setDevicePixelRatio(dpr);
            return pm;
        }
    }
    QImage src(QStringLiteral(":/icons/ui/%1.png").arg(name));
    if (src.isNull()) return {};
    QImage img = src.scaled(qRound(size * dpr), qRound(size * dpr), Qt::KeepAspectRatio, Qt::SmoothTransformation)
                     .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter p(&img);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(img.rect(), color);
    p.end();
    QPixmap pm = QPixmap::fromImage(img);
    pm.setDevicePixelRatio(dpr);
    return pm;
}

QIcon icon(const QString &name, const QColor &normal, const QColor &selected, int size)
{
    QIcon i;
    i.addPixmap(iconPixmap(name, normal, size), QIcon::Normal);
    i.addPixmap(iconPixmap(name, selected, size), QIcon::Selected);
    i.addPixmap(iconPixmap(name, selected, size), QIcon::Active);
    return i;
}

QLabel *iconBadge(const QString &name, const QColor &fg, const QColor &bg, int size, QWidget *parent)
{
    auto *l = new QLabel(parent);
    if (Theme::isClassic()) {
        l->setFixedSize(size * 3 / 4, size * 3 / 4);
        l->setAlignment(Qt::AlignCenter);
        l->setPixmap(iconPixmap(name, fg, size / 2));
        return l;
    }
    l->setFixedSize(size, size);
    l->setAlignment(Qt::AlignCenter);
    l->setPixmap(iconPixmap(name, fg, size / 2));
    l->setStyleSheet(QStringLiteral("background:%1; border-radius:%2px;").arg(bg.name(QColor::HexArgb)).arg(size / 2));
    return l;
}

QLabel *muted(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setProperty("role", "muted");
    l->setWordWrap(true);
    return l;
}

void error(QWidget *parent, const QString &text) { QMessageBox::warning(parent, "MediCare Pro", text); }
void info(QWidget *parent, const QString &text) { QMessageBox::information(parent, "MediCare Pro", text); }
bool confirm(QWidget *parent, const QString &text)
{
    return QMessageBox::question(parent, "MediCare Pro", text, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}
bool check(QWidget *parent, const Result &r)
{
    if (!r.ok) error(parent, r.error);
    return r.ok;
}
QString askText(QWidget *parent, const QString &title, const QString &label, bool *ok, const QString &initial)
{
    return QInputDialog::getText(parent, title, label, QLineEdit::Normal, initial, ok).trimmed();
}
bool can(const QString &permission) { return Session::instance().can(permission); }

MoneyEdit::MoneyEdit(QWidget *parent) : QLineEdit(parent)
{
    setValidator(new QRegularExpressionValidator(QRegularExpression("^[0-9,]{0,12}(\\.[0-9]{0,2})?$"), this));
    setPlaceholderText("0.00");
    setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(this, &QLineEdit::textChanged, this, [this] { emit valueChanged(value()); });
}
Paisa MoneyEdit::value() const
{
    Paisa p = 0;
    return text().trimmed().isEmpty() ? 0 : (Money::parse(text(), &p) ? p : 0);
}
bool MoneyEdit::isValid() const
{
    Paisa p = 0;
    return text().trimmed().isEmpty() || Money::parse(text(), &p);
}
void MoneyEdit::setValue(Paisa p) { setText(p == 0 ? QString() : Money::format(p, false).remove(',')); }

QDateEdit *dateEdit(const QDate &d, QWidget *parent)
{
    auto *e = new QDateEdit(d, parent);
    e->setCalendarPopup(true);
    e->setDisplayFormat("dd-MM-yyyy");
    return e;
}

QVariant FormatProxy::data(const QModelIndex &index, int role) const
{
    if (role == Qt::DisplayRole) {
        const QVariant v = QIdentityProxyModel::data(index, Qt::DisplayRole);
        if (formatters.contains(index.column())) return formatters.value(index.column())(v);
        if (money.contains(index.column())) return v.isNull() ? QString() : Money::format(v.toLongLong(), false);
        return v;
    }
    if (role == Qt::TextAlignmentRole && (money.contains(index.column()) || numbers.contains(index.column())))
        return int(Qt::AlignRight | Qt::AlignVCenter);
    if (role == Qt::ForegroundRole && rowColor) {
        const QColor c = rowColor(index);
        if (c.isValid()) return c;
    }
    return QIdentityProxyModel::data(index, role);
}

SqlTable::SqlTable(QWidget *parent) : QTableView(parent)
{
    m_model = new QSqlQueryModel(this);
    m_proxy = new FormatProxy(this);
    m_proxy->setSourceModel(m_model);
    setModel(m_proxy);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setAlternatingRowColors(true);
    verticalHeader()->hide();
    verticalHeader()->setDefaultSectionSize(30);
    horizontalHeader()->setStretchLastSection(true);
    horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    setWordWrap(false);
    connect(this, &QTableView::doubleClicked, this, [this](const QModelIndex &i) {
        emit activatedId(m_model->data(m_model->index(i.row(), 0)).toLongLong());
    });
}

void SqlTable::setQuery(const QString &sql, const QVariantList &binds, const QStringList &headers)
{
    m_sql = sql;
    m_binds = binds;
    m_headers = headers;
    reload();
}

void SqlTable::reload()
{
    QSqlQuery q(Db::get(Db::Pharmacy));
    q.prepare(m_sql);
    for (const auto &b : m_binds) q.addBindValue(b);
    q.exec();
    m_model->setQuery(std::move(q));
    while (m_model->canFetchMore()) m_model->fetchMore();
    for (int i = 0; i < m_headers.size(); ++i) m_model->setHeaderData(i, Qt::Horizontal, m_headers[i]);
    setColumnHidden(0, hideIdColumn);
    resizeColumnsToContents();
    for (int c = 0; c < m_model->columnCount(); ++c)
        if (columnWidth(c) > 320) setColumnWidth(c, 320);
}

qint64 SqlTable::currentId() const { return currentValue(0).toLongLong(); }

QVariant SqlTable::currentValue(int column) const
{
    const QModelIndex i = currentIndex();
    if (!i.isValid()) return {};
    return m_model->data(m_model->index(i.row(), column));
}

DateRangeBar::DateRangeBar(QWidget *parent, int defaultPreset) : QWidget(parent)
{
    auto *h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    m_preset = new QComboBox(this);
    m_preset->addItems({tr("Today"), tr("Yesterday"), tr("This week"), tr("This month"), tr("Last month"), tr("This year"), tr("Custom")});
    m_from = dateEdit(QDate::currentDate(), this);
    m_to = dateEdit(QDate::currentDate(), this);
    h->addWidget(m_preset);
    h->addWidget(new QLabel(tr("From"), this));
    h->addWidget(m_from);
    h->addWidget(new QLabel(tr("To"), this));
    h->addWidget(m_to);
    h->addStretch();
    connect(m_preset, &QComboBox::currentIndexChanged, this, [this](int i) { applyPreset(i); });
    auto custom = [this] {
        if (m_preset->currentIndex() != 6) { QSignalBlocker b(m_preset); m_preset->setCurrentIndex(6); }
        emit changed();
    };
    connect(m_from, &QDateEdit::userDateChanged, this, custom);
    connect(m_to, &QDateEdit::userDateChanged, this, custom);
    m_preset->setCurrentIndex(defaultPreset);
    applyPreset(defaultPreset);
}

void DateRangeBar::applyPreset(int i)
{
    const QDate t = QDate::currentDate();
    QDate a = t, b = t;
    switch (i) {
    case 1: a = b = t.addDays(-1); break;
    case 2: a = t.addDays(1 - t.dayOfWeek()); break;
    case 3: a = QDate(t.year(), t.month(), 1); break;
    case 4: a = QDate(t.year(), t.month(), 1).addMonths(-1); b = QDate(t.year(), t.month(), 1).addDays(-1); break;
    case 5: a = QDate(t.year(), 1, 1); break;
    case 6: return;
    default: break;
    }
    QSignalBlocker b1(m_from), b2(m_to);
    m_from->setDate(a);
    m_to->setDate(b);
    emit changed();
}

QString DateRangeBar::from() const { return qMin(m_from->date(), m_to->date()).toString(Qt::ISODate); }
QString DateRangeBar::to() const { return qMax(m_from->date(), m_to->date()).toString(Qt::ISODate); }
QString DateRangeBar::label() const
{
    return tr("%1 to %2").arg(QDate::fromString(from(), Qt::ISODate).toString("dd-MM-yyyy"),
                              QDate::fromString(to(), Qt::ISODate).toString("dd-MM-yyyy"));
}

static QString defaultDir() { return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation); }

bool saveText(QWidget *parent, const QString &content, const QString &suggestedName, const QString &filter)
{
    const QString path = QFileDialog::getSaveFileName(parent, QObject::tr("Save file"), defaultDir() + "/" + suggestedName, filter);
    if (path.isEmpty()) return false;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { error(parent, f.errorString()); return false; }
    if (path.endsWith(".csv", Qt::CaseInsensitive)) f.write("\xEF\xBB\xBF");
    f.write(content.toUtf8());
    return true;
}

bool exportModelCsv(QWidget *parent, QAbstractItemModel *m, const QString &suggestedName, bool skipFirst)
{
    auto cell = [](QString s) {
        if (!s.isEmpty() && QString("=+@").contains(s.at(0))) s.prepend('\'');
        return '"' + s.replace('"', "\"\"") + '"';
    };
    QStringList lines;
    QStringList h;
    for (int c = skipFirst ? 1 : 0; c < m->columnCount(); ++c) h << cell(m->headerData(c, Qt::Horizontal).toString());
    lines << h.join(',');
    for (int r = 0; r < m->rowCount(); ++r) {
        QStringList row;
        for (int c = skipFirst ? 1 : 0; c < m->columnCount(); ++c) row << cell(m->data(m->index(r, c)).toString());
        lines << row.join(',');
    }
    return saveText(parent, lines.join("\r\n") + "\r\n", suggestedName, QObject::tr("CSV files (*.csv)"));
}

static void setupPrinter(QPrinter &p, double widthMm, const QString &html)
{
    if (widthMm <= 0) {
        p.setPageSize(QPageSize(QPageSize::A4));
        p.setPageMargins(QMarginsF(12, 12, 12, 12), QPageLayout::Millimeter);
        return;
    }
    // Roll paper: page height follows the content so nothing is cut.
    QTextDocument doc;
    { QFont df = doc.defaultFont(); df.setStyleStrategy(QFont::PreferAntialias); df.setHintingPreference(QFont::PreferNoHinting); doc.setDefaultFont(df); }
    doc.setHtml(html);
    const double printableMm = widthMm - 6;
    doc.setTextWidth(printableMm / 25.4 * 96.0);
    const double heightMm = qMax(60.0, doc.size().height() / 96.0 * 25.4 + 12);
    p.setPageSize(QPageSize(QSizeF(widthMm, heightMm), QPageSize::Millimeter, "Roll", QPageSize::ExactMatch));
    p.setPageMargins(QMarginsF(3, 3, 3, 3), QPageLayout::Millimeter);
}

static void render(QPrinter *p, const QString &html)
{
    QTextDocument doc;
    { QFont df = doc.defaultFont(); df.setStyleStrategy(QFont::PreferAntialias); df.setHintingPreference(QFont::PreferNoHinting); doc.setDefaultFont(df); }
    doc.setHtml(html);
    doc.setPageSize(p->pageRect(QPrinter::Point).size());
    doc.print(p);
}

void printHtml(QWidget *parent, const QString &html, double widthMm, bool preview)
{
    QPrinter printer(QPrinter::HighResolution);
    setupPrinter(printer, widthMm, html);
    if (preview) {
        QPrintPreviewDialog dlg(&printer, parent);
        dlg.resize(820, 760);
        QObject::connect(&dlg, &QPrintPreviewDialog::paintRequested, &dlg, [&](QPrinter *p) { render(p, html); });
        dlg.exec();
        return;
    }
    QPrintDialog dlg(&printer, parent);
    if (dlg.exec() == QDialog::Accepted) render(&printer, html);
}

bool saveHtmlAsPdf(QWidget *parent, const QString &html, double widthMm, const QString &suggestedName)
{
    const QString path = QFileDialog::getSaveFileName(parent, QObject::tr("Save PDF"), defaultDir() + "/" + suggestedName,
                                                      QObject::tr("PDF files (*.pdf)"));
    if (path.isEmpty()) return false;
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    setupPrinter(printer, widthMm, html);
    render(&printer, html);
    return true;
}
}
