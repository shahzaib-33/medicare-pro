#pragma once
#include <QColor>
#include <QString>
class QApplication;

namespace mcp::Theme {
enum class Mode { Light, Dark, Classic, Xp };
Mode fromString(const QString &name);   // "xp" (default), "classic", "light" or "dark"
void apply(QApplication &app, Mode mode, bool urdu);
QString styleSheet(Mode mode, bool urdu = false);

// Brand colours used by code that paints or tints icons.
inline const QColor primary() { return QColor("#4338ca"); }
inline const QColor primarySoft() { return QColor("#eef0ff"); }
QColor muted();
QColor sidebarText();
bool isDark();
bool isClassic();   // desktop layout with menus, toolbar and status bar (Windows 98 or Windows 2003 look)
bool isXp();        // Windows 2003 / XP look: blue title bars, beige windows, blue task pane
}
