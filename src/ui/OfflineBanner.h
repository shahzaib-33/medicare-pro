#pragma once
#include <QFrame>
class QLabel;

namespace mcp {
class OfflineBanner : public QFrame {
    Q_OBJECT
public:
    explicit OfflineBanner(QWidget *parent = nullptr);
public slots:
    void setOnline(bool online);
private:
    QLabel *m_label;
};
}
