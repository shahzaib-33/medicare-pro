#pragma once
#include <QSqlDatabase>
#include "MedicineRecord.h"

namespace mcp {
// Writes search_index rows for one catalog entry. Call inside a transaction.
namespace SearchIndexer {
enum Kind { Word = 1, Phonetic = 2, Code = 3 };
bool reindex(QSqlDatabase db, qint64 medicineId, const MedicineRecord &r, QString *err);
}
}
