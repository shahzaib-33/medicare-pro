#pragma once
#include <QFrame>
#include "AppContext.h"
class QLabel;
class QPushButton;

namespace mcp {
// Dashboard card: PAKISTAN MEDICINE DATABASE
class MedicineDbCard : public QFrame {
    Q_OBJECT
public:
    MedicineDbCard(AppContext *ctx, QWidget *parent = nullptr);
public slots:
    void refresh();
signals:
    void browseRequested();
    void historyRequested();
    void updateNowRequested();
private:
    AppContext *m_ctx;
    QLabel *m_total, *m_version, *m_lastUpdate, *m_newAvail, *m_pending, *m_conn, *m_coverage;
};
}
