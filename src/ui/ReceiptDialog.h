#pragma once
#include <QDialog>
class QComboBox;
class QTextBrowser;

namespace mcp {

// Shows a sale or return receipt with print, preview and PDF. Each print is logged.
// The first print is the original. Later prints are marked as duplicate copies.
class ReceiptDialog : public QDialog {
    Q_OBJECT
public:
    ReceiptDialog(qint64 saleId, bool isReturn, QWidget *parent = nullptr);
    // Prints the original receipt straight to the default printer, used after a sale.
    static bool printOriginalDirect(qint64 saleId, QString *err);
    static double widthMm(const QString &width);
private:
    void refresh();
    bool print(bool toPdf);
    qint64 m_id;
    bool m_return;
    QComboBox *m_width, *m_lang;
    QTextBrowser *m_view;
};
}
