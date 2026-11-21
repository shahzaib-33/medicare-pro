#include "OfflineBanner.h"
#include <QHBoxLayout>
#include <QLabel>

namespace mcp {
OfflineBanner::OfflineBanner(QWidget *parent) : QFrame(parent)
{
    setObjectName("offlineBanner");
    auto *l = new QHBoxLayout(this);
    l->setContentsMargins(16, 6, 16, 6);
    m_label = new QLabel(tr("Offline Mode — All local pharmacy operations are available."), this);
    l->addWidget(m_label);
    l->addStretch();
    hide();
}

void OfflineBanner::setOnline(bool online) { setVisible(!online); }
}
