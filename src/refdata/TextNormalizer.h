#pragma once
#include <QString>
#include <QStringList>

namespace mcp {
// Text helpers for bilingual (English / Urdu) medicine search.
namespace TextNormalizer {
// Lower case, unify Arabic/Urdu letter variants, drop diacritics and punctuation,
// collapse spaces. Works for both scripts.
QString normalize(const QString &s);

// Splits normalized text into tokens. Keeps number+unit together ("500mg").
QStringList tokens(const QString &s);

// A script independent sound key. "Panadol", "Panadole" and "پیناڈول" all give "pndl".
// Used for spelling tolerant and cross-script matching.
QString phoneticKey(const QString &word);

bool containsUrdu(const QString &s);

// Damerau-Levenshtein distance, capped for speed.
int editDistance(const QString &a, const QString &b, int cap = 3);
}
}
