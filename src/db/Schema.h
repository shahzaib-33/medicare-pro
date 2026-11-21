#pragma once
#include <QStringList>

namespace mcp::Schema {
// Each entry is one migration step. Index + 1 is the schema version.
const QStringList &pharmacyMigrations();
const QStringList &referenceMigrations();
}
