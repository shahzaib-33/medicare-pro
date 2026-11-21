#pragma once
#include "Common.h"

namespace mcp {

// Builds receipt HTML for QTextDocument. Pure text, so it is testable without a printer.
namespace ReceiptBuilder {
// width: "58", "80" or "A4". language: "en" or "ur". copyNumber 0 = original, 1+ = marked as a copy.
QString saleHtml(qint64 saleId, const QString &width, const QString &language, int copyNumber);
QString returnHtml(qint64 returnId, const QString &width, const QString &language);
}
}
