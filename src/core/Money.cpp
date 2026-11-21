#include "Money.h"
#include <QLocale>
#include <QRegularExpression>
#include <QtNumeric>

namespace mcp::Money {

QString format(Paisa p, bool withSymbol)
{
    const bool neg = p < 0;
    const quint64 a = neg ? quint64(-p) : quint64(p);
    QString rupees = QString::number(a / 100);
    // thousands separators
    for (int i = rupees.size() - 3; i > 0; i -= 3) rupees.insert(i, ',');
    const QString s = QStringLiteral("%1%2.%3").arg(neg ? "-" : "", rupees).arg(a % 100, 2, 10, QChar('0'));
    return withSymbol ? "Rs " + s : s;
}

bool parse(const QString &textIn, Paisa *out)
{
    QString t = textIn.trimmed();
    t.remove(QRegularExpression("(?i)^(rs\\.?|pkr)\\s*"));
    t.remove(',');
    t.remove(' ');
    static const QRegularExpression re("^(-)?(\\d{1,13})(?:\\.(\\d{1,2}))?$");
    const auto m = re.match(t);
    if (!m.hasMatch()) return false;
    qint64 v = m.captured(2).toLongLong() * 100;
    QString frac = m.captured(3);
    if (frac.size() == 1) frac += '0';
    if (!frac.isEmpty()) v += frac.toLongLong();
    *out = m.captured(1).isEmpty() ? v : -v;
    return true;
}

Paisa percentOf(Paisa amount, int bp)
{
    // Amounts stay far below 9e14 paisa, so amount * 10000 fits in 64 bits.
    const qint64 x = amount * bp;
    qint64 q = x / 10000;
    const qint64 r = x % 10000;
    if (r * 2 >= 10000) q += 1;
    else if (r * 2 <= -10000) q -= 1;
    return q;
}

Paisa times(Paisa unit, qint64 qty)
{
    Paisa r = 0;
    if (qMulOverflow(unit, qty, &r)) return 0;
    return r;
}

QList<Paisa> allocate(Paisa total, const QList<Paisa> &weights)
{
    QList<Paisa> parts;
    Paisa sumW = 0;
    for (Paisa w : weights) sumW += w;
    if (sumW == 0) { for (int i = 0; i < weights.size(); ++i) parts << 0; if (!parts.isEmpty()) parts[0] = total; return parts; }
    Paisa used = 0;
    for (int i = 0; i < weights.size(); ++i) {
        const Paisa p = (i == weights.size() - 1) ? total - used : Paisa((long double)total * weights[i] / sumW);
        parts << p;
        used += p;
    }
    return parts;
}
}
