#pragma once
#include <QSqlDatabase>
#include <QStringList>
#include <QVector>
#include "MedicineRecord.h"

namespace mcp {
struct SearchResult {
    QVector<CatalogHit> hits;
    QStringList suggestions;   // "did you mean" terms when few results
    int totalCandidates = 0;
};

// Bilingual search over the local reference catalog. Fully offline.
class MedicineSearch {
public:
    explicit MedicineSearch(QSqlDatabase referenceDb) : m_db(referenceDb) {}
    SearchResult search(const QString &query, int limit = 200) const;
    bool loadRecord(qint64 id, MedicineRecord *out) const;

private:
    QSqlDatabase m_db;
};
}
