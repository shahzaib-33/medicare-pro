#pragma once
#include <QWidget>
#include "AppContext.h"

namespace mcp {
class SettingsPage : public QWidget {
    Q_OBJECT
public:
    SettingsPage(AppContext *ctx, QWidget *parent = nullptr);
signals:
    void languageChanged(const QString &lang);
    void themeChanged(const QString &theme);
    void profileChanged();
};
}
