#include "AppPaths.h"
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

namespace mcp {
static QString g_root;

void AppPaths::setDataRoot(const QString &root) { g_root = root; QDir().mkpath(root); }

QString AppPaths::dataRoot()
{
    if (g_root.isEmpty()) {
        // Portable mode: a "data" folder beside the exe wins when it exists.
        const QString portable = QCoreApplication::applicationDirPath() + "/data";
        if (QDir(portable).exists())
            g_root = portable;
        else
            g_root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        QDir().mkpath(g_root);
    }
    return g_root;
}

static QString sub(const QString &name)
{
    const QString p = AppPaths::dataRoot() + '/' + name;
    QDir().mkpath(p);
    return p;
}

QString AppPaths::pharmacyDb() { return dataRoot() + "/pharmacy.db"; }
QString AppPaths::referenceDb() { return dataRoot() + "/reference.db"; }
QString AppPaths::backupDir() { return sub("backups"); }
QString AppPaths::downloadDir() { return sub("downloads"); }
QString AppPaths::settingsFile() { return dataRoot() + "/settings.ini"; }
QString AppPaths::bundledReferenceDb()
{
    return QCoreApplication::applicationDirPath() + "/refdata/reference_seed.db";
}
}
