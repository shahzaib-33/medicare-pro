#pragma once
#include <QString>

namespace mcp {
// All runtime file locations. Everything lives under one data folder so that
// backup and restore are simple and the app never needs admin rights to run.
class AppPaths {
public:
    static void setDataRoot(const QString &root);   // tests and portable mode
    static QString dataRoot();
    static QString pharmacyDb();      // private pharmacy data (never uploaded)
    static QString referenceDb();     // national medicine reference catalog
    static QString backupDir();
    static QString downloadDir();
    static QString settingsFile();
    static QString bundledReferenceDb(); // read-only copy shipped by the installer
};
}
