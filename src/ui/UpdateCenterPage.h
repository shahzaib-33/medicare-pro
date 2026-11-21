#pragma once
#include <QMap>
#include <QWidget>
#include "AppContext.h"
#include "update/ReferenceUpdater.h"
#include "update/UpdateManifest.h"
class QLabel;
class QPushButton;
class QProgressBar;
class QTableWidget;
class QCheckBox;

namespace mcp {

// Mode Medicine: "Medicine Database Updates".
// Mode Information: "Pharmacy Information Update Center" (all update types and software updates).
class UpdateCenterPage : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Medicine, Information };
    UpdateCenterPage(AppContext *ctx, Mode mode, QWidget *parent = nullptr);

public slots:
    void refresh();
    void check();
    void downloadAll();
    void installAll();
    void viewChanges();
    void importCsv();
    void restorePrevious();
    void installSoftware();

private:
    AppContext *m_ctx;
    Mode m_mode;
    QMap<QString, QLabel *> m_stat;
    QLabel *m_status;
    QProgressBar *m_progress;
    QPushButton *m_check, *m_download, *m_install, *m_changes;
    QCheckBox *m_auto;
    QTableWidget *m_history, *m_items;
    QLabel *m_swCurrent = nullptr, *m_swAvailable = nullptr, *m_swNotes = nullptr;
    QPushButton *m_swDownload = nullptr, *m_swInstall = nullptr;

    QVector<UpdateItem> m_plan;               // reference items to apply, in order
    QMap<QString, QString> m_downloaded;      // item id -> verified file
    QVector<ReferencePackage> m_packages;     // parsed, ready to install
    QVector<QString> m_packageSha;
    QString m_softwareFile;
    UpdateItem m_softwareItem;
    int m_downloadIndex = -1;

    QWidget *buildMedicine();
    QWidget *buildInformation();
    void planReferenceUpdates();
    void downloadNext();
    void parseDownloaded();
    void loadHistory();
    void setStatus(const QString &s);
    bool updatesConfigured() const;
};
}
