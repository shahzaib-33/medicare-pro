#pragma once
#include <QSqlDatabase>
#include <QString>
#include "UpdateManifest.h"

namespace mcp {

struct StartupUpdateReport {
    bool hadPendingUpdate = false;
    bool succeeded = false;
    QString fromVersion, toVersion, backupDir, message;
};

// Software update flow:
// check (UpdateClient) -> admin confirms -> backup -> app closes -> signed installer runs ->
// app restarts -> startup check confirms the new version launched.
class SoftwareUpdater {
public:
    explicit SoftwareUpdater(QString pharmacyConnection, QString referenceConnection)
        : m_ph(std::move(pharmacyConnection)), m_ref(std::move(referenceConnection)) {}

    static bool isNewer(const UpdateItem &item);

    // Creates a backup of both databases and records the pending update.
    // Returns the backup folder, or empty on failure.
    QString prepare(const UpdateItem &item, const QString &verifiedInstaller, QString *err);

    // Starts the verified installer and returns true. The caller must then quit the app.
    bool launchInstaller(const QString &verifiedInstaller, QString *err);

    // Call once at every start. Confirms or reports the result of a previous update.
    StartupUpdateReport checkAfterStart();

    static QString markerPath();

private:
    QString m_ph, m_ref;
};
}
