#pragma once
#include <QWidget>
#include "AppContext.h"
class QTableWidget;

namespace mcp {

// Manual backup, backup to a USB drive or folder, verify, restore.
class BackupPage : public QWidget {
    Q_OBJECT
public:
    explicit BackupPage(AppContext *ctx, QWidget *parent = nullptr);
    void reload();
signals:
    void restored();
private:
    QTableWidget *m_table;
};
}
