#pragma once
#include <QString>
#include <QtGlobal>

namespace mcp {
// Money is stored as whole paisa (1 rupee = 100 paisa) in a 64 bit integer.
// No floating point is ever used for financial totals.
using Paisa = qint64;

namespace Money {
// "1,234.50" or "Rs 1,234.50"
QString format(Paisa p, bool withSymbol = true);
// Parses "1234.5", "1,234.50", "Rs 12" into paisa. Returns false on bad input.
bool parse(const QString &text, Paisa *out);
inline Paisa fromRupees(qint64 rupees) { return rupees * 100; }
// Converts a 2 decimal UI spin box value. Only used at the input edge, never in totals.
inline Paisa fromInput(double rupees) { return qRound64(rupees * 100.0); }
inline double toInput(Paisa p) { return double(p) / 100.0; }

// Percentage in basis points (1% = 100 bp). Rounds half away from zero.
Paisa percentOf(Paisa amount, int basisPoints);
// a * qty with overflow guard.
Paisa times(Paisa unit, qint64 qty);
// Splits total across weights proportionally. Sum of parts equals total exactly.
QList<Paisa> allocate(Paisa total, const QList<Paisa> &weights);
}
}
