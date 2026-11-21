#include "ReceiptDialog.h"
#include "Ui.h"
#include "services/ReceiptBuilder.h"
#include "services/SalesService.h"
#include "services/SettingsService.h"
#include <QComboBox>
#include <QPrinter>
#include <QPrinterInfo>
#include <QTextBrowser>
#include <QTextDocument>

namespace mcp {

double ReceiptDialog::widthMm(const QString &w) { return w == "58" ? 58 : w == "80" ? 80 : 0; }

ReceiptDialog::ReceiptDialog(qint64 id, bool isReturn, QWidget *parent) : QDialog(parent), m_id(id), m_return(isReturn)
{
    setWindowTitle(isReturn ? tr("Return receipt") : tr("Receipt"));
    resize(520, 720);
    const PharmacyProfile p = SettingsService::profile();
    auto *v = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    m_width = new QComboBox(this);
    m_width->addItem(tr("58 mm roll"), "58");
    m_width->addItem(tr("80 mm roll"), "80");
    m_width->addItem(tr("A4 page"), "A4");
    m_width->setCurrentIndex(qMax(0, m_width->findData(p.receiptWidth)));
    m_lang = new QComboBox(this);
    m_lang->addItem("English", "en");
    m_lang->addItem(QString::fromUtf8("اردو"), "ur");
    m_lang->setCurrentIndex(qMax(0, m_lang->findData(p.receiptLanguage)));
    top->addWidget(new QLabel(tr("Paper"), this));
    top->addWidget(m_width);
    top->addWidget(new QLabel(tr("Language"), this));
    top->addWidget(m_lang);
    top->addStretch();
    v->addLayout(top);
    m_view = new QTextBrowser(this);
    {
        // The receipt preview always uses smooth text, so it matches the printed paper in every theme.
        QFont f = m_view->font();
        f.setStyleStrategy(QFont::PreferAntialias);
        f.setHintingPreference(QFont::PreferNoHinting);
        m_view->setFont(f);
        m_view->document()->setDefaultFont(f);
    }
    v->addWidget(m_view, 1);
    auto *h = new QHBoxLayout;
    auto *preview = ui::button(tr("Print preview"), this);
    auto *pdf = ui::button(tr("Save as PDF"), this);
    auto *printBtn = ui::button(tr("Print"), this, true);
    auto *close = ui::button(tr("Close"), this);
    h->addWidget(preview);
    h->addWidget(pdf);
    h->addStretch();
    h->addWidget(printBtn);
    h->addWidget(close);
    v->addLayout(h);
    connect(m_width, &QComboBox::currentIndexChanged, this, &ReceiptDialog::refresh);
    connect(m_lang, &QComboBox::currentIndexChanged, this, &ReceiptDialog::refresh);
    connect(preview, &QPushButton::clicked, this, [this] {
        const QString w = m_width->currentData().toString();
        const QString html = m_return ? ReceiptBuilder::returnHtml(m_id, w, m_lang->currentData().toString())
                                      : ReceiptBuilder::saleHtml(m_id, w, m_lang->currentData().toString(), 0);
        ui::printHtml(this, html, widthMm(w), true);
    });
    connect(pdf, &QPushButton::clicked, this, [this] { print(true); });
    connect(printBtn, &QPushButton::clicked, this, [this] { print(false); });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    refresh();
}

void ReceiptDialog::refresh()
{
    const QString w = m_width->currentData().toString();
    const QString lang = m_lang->currentData().toString();
    m_view->setHtml(m_return ? ReceiptBuilder::returnHtml(m_id, w, lang) : ReceiptBuilder::saleHtml(m_id, w, lang, 0));
    m_view->setLayoutDirection(lang == "ur" ? Qt::RightToLeft : Qt::LeftToRight);
}

bool ReceiptDialog::print(bool toPdf)
{
    const QString w = m_width->currentData().toString();
    const QString lang = m_lang->currentData().toString();
    QString html;
    if (m_return) {
        html = ReceiptBuilder::returnHtml(m_id, w, lang);
    } else {
        int copy = 0;
        if (!ui::check(this, SalesService::logPrint(m_id, toPdf ? "pdf" : w, &copy))) return false;
        html = ReceiptBuilder::saleHtml(m_id, w, lang, copy);
    }
    if (toPdf) return ui::saveHtmlAsPdf(this, html, widthMm(w), (m_return ? "return_" : "receipt_") + QString::number(m_id) + ".pdf");
    ui::printHtml(this, html, widthMm(w), false);
    return true;
}

bool ReceiptDialog::printOriginalDirect(qint64 saleId, QString *err)
{
    if (QPrinterInfo::availablePrinters().isEmpty()) { *err = tr("No printer is installed."); return false; }
    const PharmacyProfile p = SettingsService::profile();
    int copy = 0;
    const Result r = SalesService::logPrint(saleId, p.receiptWidth, &copy);
    if (!r.ok) { *err = r.error; return false; }
    const QString html = ReceiptBuilder::saleHtml(saleId, p.receiptWidth, p.receiptLanguage, copy);
    QPrinter printer(QPrinterInfo::defaultPrinter(), QPrinter::HighResolution);
    const double mm = widthMm(p.receiptWidth);
    if (mm > 0) {
        QTextDocument doc;
        { QFont df = doc.defaultFont(); df.setStyleStrategy(QFont::PreferAntialias); df.setHintingPreference(QFont::PreferNoHinting); doc.setDefaultFont(df); }
        doc.setHtml(html);
        doc.setTextWidth((mm - 6) / 25.4 * 96.0);
        const double hmm = qMax(60.0, doc.size().height() / 96.0 * 25.4 + 12);
        printer.setPageSize(QPageSize(QSizeF(mm, hmm), QPageSize::Millimeter, "Roll", QPageSize::ExactMatch));
        printer.setPageMargins(QMarginsF(3, 3, 3, 3), QPageLayout::Millimeter);
    }
    QTextDocument doc;
    { QFont df = doc.defaultFont(); df.setStyleStrategy(QFont::PreferAntialias); df.setHintingPreference(QFont::PreferNoHinting); doc.setDefaultFont(df); }
    doc.setHtml(html);
    doc.setPageSize(printer.pageRect(QPrinter::Point).size());
    doc.print(&printer);
    return true;
}
}
