#pragma once
#include <QString>
#include <QVersionNumber>

namespace mcp {
inline QString appVersion() { return QStringLiteral(MEDICARE_VERSION); }
inline QVersionNumber appVersionNumber() { return QVersionNumber::fromString(appVersion()); }
}
