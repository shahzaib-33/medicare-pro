#include "TextNormalizer.h"
#include <QHash>
#include <vector>

namespace mcp::TextNormalizer {

bool containsUrdu(const QString &s)
{
    for (QChar c : s)
        if (c.unicode() >= 0x0600 && c.unicode() <= 0x06FF) return true;
    return false;
}

static QChar unifyArabic(QChar c)
{
    switch (c.unicode()) {
    case 0x064A: return QChar(0x06CC); // Arabic yeh -> Farsi yeh
    case 0x0649: return QChar(0x06CC); // alef maksura -> yeh
    case 0x0643: return QChar(0x06A9); // Arabic kaf -> keheh
    case 0x0647: return QChar(0x06C1); // heh -> heh goal
    case 0x0629: return QChar(0x06C1); // teh marbuta -> heh goal
    case 0x06C0: return QChar(0x06C1);
    case 0x0622: return QChar(0x0627); // alef madda -> alef
    case 0x0623: return QChar(0x0627);
    case 0x0625: return QChar(0x0627);
    case 0x06D3: return QChar(0x06D2);
    case 0x0624: return QChar(0x0648);
    default: return c;
    }
}

QString normalize(const QString &input)
{
    const QString s = input.normalized(QString::NormalizationForm_KC);
    QString out;
    out.reserve(s.size());
    bool lastSpace = true;
    for (QChar c : s) {
        const ushort u = c.unicode();
        // Urdu digits -> ASCII digits
        if (u >= 0x06F0 && u <= 0x06F9) c = QChar('0' + (u - 0x06F0));
        else if (u >= 0x0660 && u <= 0x0669) c = QChar('0' + (u - 0x0660));
        // diacritics, tatweel, zero width chars
        if ((u >= 0x064B && u <= 0x065F) || u == 0x0670 || u == 0x0640 || u == 0x200C || u == 0x200D
            || c.category() == QChar::Mark_NonSpacing)
            continue;
        c = unifyArabic(c);
        if (c.isLetterOrNumber() || c == '.' || c == '%' || c == '/') {
            // keep '.' only inside numbers (2.5mg)
            if (c == '.' && (out.isEmpty() || !out.back().isDigit())) { c = ' '; }
            else { out += c.toLower(); lastSpace = false; continue; }
        }
        if (!lastSpace) { out += ' '; lastSpace = true; }
    }
    return out.trimmed();
}

QStringList tokens(const QString &s)
{
    QStringList raw = normalize(s).split(' ', Qt::SkipEmptyParts);
    QStringList out;
    // Join "500 mg" into "500mg" so strength search works either way.
    static const QStringList units = {"mg", "g", "mcg", "ml", "iu", "%", "kg", "l", "u", "mmol"};
    for (int i = 0; i < raw.size(); ++i) {
        QString t = raw[i];
        if (i + 1 < raw.size() && !t.isEmpty() && t.back().isDigit() && units.contains(raw[i + 1])) {
            t += raw[i + 1];
            ++i;
        }
        out << t;
    }
    return out;
}

// Map each script to one consonant skeleton. Vowels, h, w/v and y are dropped
// because Urdu spellings of English brand names do not carry them reliably.
QString phoneticKey(const QString &wordIn)
{
    const QString w = normalize(wordIn);
    QString k;
    auto push = [&k](QChar c) { if (k.isEmpty() || k.back() != c) k += c; };
    for (int i = 0; i < w.size(); ++i) {
        const QChar c = w[i];
        const QChar n = i + 1 < w.size() ? w[i + 1] : QChar();
        const ushort u = c.unicode();
        if (u < 128) {
            switch (c.toLatin1()) {
            case 'a': case 'e': case 'i': case 'o': case 'u': case 'y': case 'h': case 'w': case 'v': break;
            case 'p': if (n == 'h') { push('f'); ++i; } else push('p'); break;
            case 'c':
                if (n == 'h') { push('C'); ++i; }
                else if (n == 'k') { push('k'); ++i; }
                else if (n == 'e' || n == 'i' || n == 'y') push('s');
                else push('k');
                break;
            case 's': if (n == 'h') { push('S'); ++i; } else push('s'); break;
            case 'k': if (n == 'h') ++i; push('k'); break;
            case 'g': if (n == 'h') ++i; push((n == 'e' || n == 'i') ? 'j' : 'g'); break;
            case 't': if (n == 'h') ++i; push('t'); break;
            case 'q': push('k'); break;
            case 'x': push('k'); push('s'); break;
            case 'z': push('s'); break;
            default: if (c.isLetter()) push(c); break; // b d f j l m n r
            }
            continue;
        }
        switch (u) {
        case 0x0628: push('b'); break;               // ب
        case 0x067E: push('p'); break;               // پ
        case 0x062A: case 0x0679: case 0x0637: push('t'); break; // ت ٹ ط
        case 0x062B: case 0x0633: case 0x0635: push('s'); break; // ث س ص
        case 0x062C: push('j'); break;               // ج
        case 0x0686: push('C'); break;               // چ
        case 0x062E: case 0x0642: case 0x06A9: push('k'); break; // خ ق ک
        case 0x062F: case 0x0688: push('d'); break;  // د ڈ
        case 0x0630: case 0x0632: case 0x0636: case 0x0638: case 0x0698: push('s'); break; // ذ ز ض ظ ژ
        case 0x0631: case 0x0691: push('r'); break;  // ر ڑ
        case 0x0634: push('S'); break;               // ش
        case 0x063A: case 0x06AF: push('g'); break;  // غ گ
        case 0x0641: push('f'); break;               // ف
        case 0x0644: push('l'); break;               // ل
        case 0x0645: push('m'); break;               // م
        case 0x0646: case 0x06BA: push('n'); break;  // ن ں
        default: break;                              // vowels, alef, waw, yeh, heh, ain
        }
    }
    return k;
}

int editDistance(const QString &a, const QString &b, int cap)
{
    const int n = a.size(), m = b.size();
    if (qAbs(n - m) > cap) return cap + 1;
    std::vector<std::vector<int>> d(n + 1, std::vector<int>(m + 1));
    for (int i = 0; i <= n; ++i) d[i][0] = i;
    for (int j = 0; j <= m; ++j) d[0][j] = j;
    for (int i = 1; i <= n; ++i) {
        int rowMin = cap + 1;
        for (int j = 1; j <= m; ++j) {
            const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            d[i][j] = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1])
                d[i][j] = std::min(d[i][j], d[i - 2][j - 2] + 1);
            rowMin = std::min(rowMin, d[i][j]);
        }
        if (rowMin > cap) return cap + 1;
    }
    return d[n][m];
}
}
