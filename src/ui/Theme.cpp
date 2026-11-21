#include "Theme.h"
#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QStyleFactory>
#include <QStyle>

namespace mcp::Theme {

static bool g_dark = false;
static bool g_classic = false;
static bool g_xp = false;
bool isDark() { return g_dark; }
bool isClassic() { return g_classic || g_xp; }
bool isXp() { return g_xp; }
Mode fromString(const QString &name)
{
    if (name == "light") return Mode::Light;
    if (name == "dark") return Mode::Dark;
    if (name == "classic") return Mode::Classic;
    return Mode::Xp;
}

// Windows 2003 / XP look: blue title bars with rounded tops, beige windows, soft buttons and a blue task pane.
static QString xpSheet(bool urdu)
{
    QString css = QStringLiteral(R"(
QMainWindow, QDialog, QMessageBox { background: #ece9d8; }
QMenuBar { background: #ece9d8; border-bottom: 1px solid #ffffff; }
QMenuBar::item { padding: 3px 8px; background: transparent; }
QMenuBar::item:selected, QMenuBar::item:pressed { background: #316ac5; color: #ffffff; }
QMenu { background: #ffffff; border: 1px solid #aca899; padding: 2px; }
QMenu::item { padding: 4px 26px 4px 28px; }
QMenu::item:selected { background: #316ac5; color: #ffffff; }
QMenu::item:disabled { color: #aca899; }
QMenu::separator { height: 1px; background: #aca899; margin: 3px 2px; }
QMenu::icon { padding-left: 6px; }
QToolBar#classicToolbar { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #fdfdfb, stop:0.85 #ece9d8, stop:1 #e3dfcf);
    border: none; border-bottom: 1px solid #c5c2b2; spacing: 1px; padding: 2px; }
QToolBar#classicToolbar QToolButton { padding: 3px 6px; border: 1px solid transparent; border-radius: 3px; color: #000000; }
QToolBar#classicToolbar QToolButton:hover { border-color: #c1d2ee; background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffffff, stop:1 #e1eaf8); }
QToolBar#classicToolbar QToolButton:pressed { border-color: #316ac5; background: #c1d2ee; }
QToolBar#classicToolbar QToolButton:disabled { color: #aca899; }
QToolBar::separator { width: 1px; background: #c5c2b2; margin: 5px 5px; }
QPushButton { min-height: 19px; padding: 2px 12px; color: #000000; border: 1px solid #003c74; border-radius: 3px;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffffff, stop:0.6 #f3f3ef, stop:1 #d6d0c5); }
QPushButton:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffe7a2, stop:0.12 #ffffff, stop:0.85 #f0eee6, stop:1 #f8b330); }
QPushButton:pressed, QPushButton:checked { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #e2e1da, stop:1 #f4f3ee); }
QPushButton:default, QPushButton[role="primary"] {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #cee7ff, stop:0.12 #ffffff, stop:0.85 #f0eee6, stop:1 #6982ee); }
QPushButton:default:hover, QPushButton[role="primary"]:hover {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffe7a2, stop:0.12 #ffffff, stop:0.85 #f0eee6, stop:1 #f8b330); }
QPushButton:disabled { color: #aca899; border-color: #c9c7ba; background: #f5f4ea; }
QToolButton { border: 1px solid transparent; border-radius: 3px; padding: 2px; }
QToolButton:hover { border-color: #c1d2ee; background: #e1eaf8; }
QLineEdit, QSpinBox, QDoubleSpinBox, QDateEdit, QDateTimeEdit, QTimeEdit, QComboBox, QPlainTextEdit, QTextEdit, QTextBrowser {
    background: #ffffff; color: #000000; border: 1px solid #7f9db9; padding: 2px 3px; selection-background-color: #316ac5; selection-color: #ffffff; }
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QDateEdit:disabled, QComboBox:disabled { background: #ebebe4; color: #aca899; border-color: #c9c7ba; }
QLineEdit:read-only { background: #f7f7f2; }
QComboBox { padding-right: 20px; }
QComboBox::drop-down, QDateEdit::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 16px; margin: 1px;
    border: 1px solid #b9ccf5; border-radius: 2px; background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #d3e0fd, stop:1 #9fb8f1); }
QComboBox::drop-down:hover, QDateEdit::drop-down:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #e6eeff, stop:1 #b5caf8); }
QComboBox::down-arrow, QDateEdit::down-arrow { image: url(:/icons/ui/chevron-down-gray.png); width: 9px; height: 9px; }
QComboBox QAbstractItemView { background: #ffffff; border: 1px solid #7f9db9; selection-background-color: #316ac5; selection-color: #ffffff; }
QSpinBox::up-button, QDoubleSpinBox::up-button, QDateTimeEdit::up-button, QTimeEdit::up-button {
    subcontrol-origin: border; subcontrol-position: top right; width: 15px; margin: 1px 1px 0 0; border: 1px solid #b9ccf5; border-radius: 2px;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #d3e0fd, stop:1 #9fb8f1); }
QSpinBox::down-button, QDoubleSpinBox::down-button, QDateTimeEdit::down-button, QTimeEdit::down-button {
    subcontrol-origin: border; subcontrol-position: bottom right; width: 15px; margin: 0 1px 1px 0; border: 1px solid #b9ccf5; border-radius: 2px;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #d3e0fd, stop:1 #9fb8f1); }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow, QDateTimeEdit::up-arrow, QTimeEdit::up-arrow { image: url(:/icons/ui/chevron-up-gray.png); width: 7px; height: 7px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow, QDateTimeEdit::down-arrow, QTimeEdit::down-arrow { image: url(:/icons/ui/chevron-down-gray.png); width: 7px; height: 7px; }
QCheckBox, QRadioButton { spacing: 6px; background: transparent; }
QCheckBox::indicator { width: 13px; height: 13px; border: 1px solid #1c5180; background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #dcdcd7, stop:1 #ffffff); }
QCheckBox::indicator:hover { border-color: #f8b330; }
QCheckBox::indicator:checked { image: url(:/icons/xp/check.png); }
QCheckBox::indicator:disabled { border-color: #cac8bb; background: #f5f4ea; }
QRadioButton::indicator { width: 13px; height: 13px; border: 1px solid #1c5180; border-radius: 7px; background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #dcdcd7, stop:1 #ffffff); }
QRadioButton::indicator:checked { background: qradialgradient(cx:0.5, cy:0.5, radius:0.5, fx:0.4, fy:0.4, stop:0 #5ee05a, stop:0.4 #21a121, stop:0.48 #ffffff, stop:1 #e8e8e2); }
QGroupBox { border: 1px solid #d0d0bf; border-radius: 4px; margin-top: 9px; padding: 8px 6px 6px 6px; background: transparent; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 9px; padding: 0 3px; color: #0046d5; }
QTabWidget::pane { border: 1px solid #919b9c; background: #fcfcfe; top: -1px; }
QTabBar::tab { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffffff, stop:1 #ecebe5); border: 1px solid #919b9c; border-bottom: none;
    padding: 3px 10px; margin-right: 1px; border-top-left-radius: 3px; border-top-right-radius: 3px; }
QTabBar::tab:selected { background: #fcfcfe; border-top: 3px solid #ffc83c; }
QTabBar::tab:!selected:hover { border-top: 2px solid #ffc83c; }
QHeaderView { background: #ffffff; }
QHeaderView::section { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffffff, stop:1 #ebeadb); color: #000000; border: none;
    border-right: 1px solid #d6d2c2; border-bottom: 3px solid #d6d2c2; padding: 3px 6px; }
QHeaderView::section:hover { border-bottom: 3px solid #f9b119; }
QTableView, QTableWidget, QTreeView, QTreeWidget, QListView, QListWidget { background: #ffffff; color: #000000; border: 1px solid #7f9db9;
    gridline-color: #ecebe6; alternate-background-color: #f7f7f2; selection-background-color: #316ac5; selection-color: #ffffff; }
QTableCornerButton::section { background: #ebeadb; border: none; }
QScrollBar:vertical { background: #fbfbf8; width: 17px; margin: 17px 0 17px 0; border-left: 1px solid #eeede5; }
QScrollBar:horizontal { background: #fbfbf8; height: 17px; margin: 0 17px 0 17px; border-top: 1px solid #eeede5; }
QScrollBar::handle:vertical { min-height: 24px; margin: 1px; border: 1px solid #a6bcee; border-radius: 3px;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #cad8fc, stop:1 #b2c6f5); }
QScrollBar::handle:horizontal { min-width: 24px; margin: 1px; border: 1px solid #a6bcee; border-radius: 3px;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #cad8fc, stop:1 #b2c6f5); }
QScrollBar::handle:hover { border-color: #7f9db9; }
QScrollBar::add-line, QScrollBar::sub-line { border: 1px solid #a6bcee; border-radius: 3px; background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #d3e0fd, stop:1 #b2c6f5); }
QScrollBar::add-line:vertical { height: 16px; subcontrol-position: bottom; subcontrol-origin: margin; }
QScrollBar::sub-line:vertical { height: 16px; subcontrol-position: top; subcontrol-origin: margin; }
QScrollBar::add-line:horizontal { width: 16px; subcontrol-position: right; subcontrol-origin: margin; }
QScrollBar::sub-line:horizontal { width: 16px; subcontrol-position: left; subcontrol-origin: margin; }
QScrollBar::up-arrow:vertical { image: url(:/icons/ui/chevron-up-gray.png); width: 8px; height: 8px; }
QScrollBar::down-arrow:vertical { image: url(:/icons/ui/chevron-down-gray.png); width: 8px; height: 8px; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QProgressBar { border: 1px solid #686868; border-radius: 3px; background: #ffffff; text-align: center; color: #000000; min-height: 14px; }
QProgressBar::chunk { margin: 1px; width: 7px; background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #9cf59a, stop:0.5 #2fb12e, stop:1 #6ce06b); }
QSplitter::handle { background: #ece9d8; }
QStatusBar { background: #ece9d8; border-top: 1px solid #ffffff; }
QStatusBar::item { border: none; }
QStatusBar QLabel#statusPanel { border: 1px solid; border-color: #aca899 #ffffff #ffffff #aca899; padding: 1px 6px; }
QToolTip { background: #ffffe1; color: #000000; border: 1px solid #000000; }
QLabel[role="h1"], QLabel#classicCaption { color: #ffffff; font-weight: bold; font-size: @CAP@; padding: 5px 10px;
    border-top-left-radius: 6px; border-top-right-radius: 6px;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #0997ff, stop:0.08 #0053ee, stop:0.4 #0050ee, stop:0.88 #0066ff, stop:0.95 #005bff, stop:1 #003dd7); }
QLabel[role="h2"] { font-weight: bold; color: #215dc6; }
QLabel[role="stat"] { font-weight: bold; font-size: @BIG@; color: #0c32a6; }
QLabel[role="muted"] { color: #555555; }
QLabel[role="pill"] { color: #215dc6; font-weight: bold; }
QFrame[role="card"], QFrame#statCard, QGroupBox[role="card"] { background: #f7f6ef; border: 1px solid #d0d0bf; border-radius: 4px; }
QFrame[role="cardHero"] { border-radius: 4px; background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #0c32a6, stop:1 #3d78e0); }
QFrame[role="cardHero"] QLabel { color: #ffffff; background: transparent; }
QFrame#statCard QLabel#statValue { font-weight: bold; font-size: @BIG@; color: #0c32a6; }
QFrame#statCard QLabel#statCaption { color: #333333; }
QFrame#offlineBanner { background: #ffffe1; border: 1px solid #aca899; }
QFrame#offlineBanner QLabel { color: #000000; }
QFrame#flagBanner { background: #ffffe1; border: 1px solid #e3a21a; }
QFrame#flagBanner QLabel { color: #8a3a00; }
QScrollArea#xpTaskPane, QWidget#xpTaskPaneBody { border: none;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #7ba2e7, stop:1 #6375d6); }
QPushButton#xpTaskHeader { text-align: left; font-weight: bold; color: #215dc6; border: none; min-height: 17px; padding: 4px 10px;
    border-top-left-radius: 5px; border-top-right-radius: 5px; background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #ffffff, stop:1 #c6d3f7); }
QPushButton#xpTaskHeader:hover { color: #428eff; background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #ffffff, stop:1 #c6d3f7); }
QPushButton#xpTaskHeaderMain { text-align: left; font-weight: bold; color: #ffffff; border: none; min-height: 17px; padding: 4px 10px;
    border-top-left-radius: 5px; border-top-right-radius: 5px; background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #0c32a6, stop:1 #2862d9); }
QPushButton#xpTaskHeaderMain:hover { color: #c6d3f7; background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #0c32a6, stop:1 #2862d9); }
QFrame#xpTaskBody { background: #d6dff7; border: 1px solid #ffffff; border-top: none; }
QToolButton#xpTaskLink { border: none; background: transparent; color: #215dc6; padding: 1px 1px; text-align: left; }
QScrollArea#xpTaskPane QScrollBar:vertical { background: #8fb0ec; border-left: none; }
QToolButton#xpTaskLink:hover { color: #428eff; text-decoration: underline; background: transparent; }
QToolButton#xpTaskLink:checked { color: #0c32a6; font-weight: bold; background: transparent; }
QToolButton#xpTaskLink:focus { border: 1px dotted #0c32a6; }
QLabel#xpTaskDetail { color: #000000; background: transparent; }
QFrame#classicDialog { background: #ece9d8; border: 1px solid #0831d9; border-top-left-radius: 8px; border-top-right-radius: 8px; }
QLabel#classicDialogCaption { color: #ffffff; font-weight: bold; font-size: @CAP@; padding: 6px 9px;
    border-top-left-radius: 7px; border-top-right-radius: 7px;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #0997ff, stop:0.08 #0053ee, stop:0.4 #0050ee, stop:0.88 #0066ff, stop:0.95 #005bff, stop:1 #003dd7); }
QFrame#xpBanner { border: none; border-bottom: 3px solid #f39c1f; background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #0c32a6, stop:1 #3d78e0); }
QFrame#xpBanner QLabel { color: #ffffff; background: transparent; }
QLabel#xpBannerName { font-size: @BANNER@; font-weight: bold; }
QLabel#authTitle { font-weight: bold; color: #215dc6; }
QLabel#authError { color: #c00000; }
QLabel#authVersion { color: #555555; }
QLabel#authLabel { color: #000000; }
QLabel#posTotal { color: #ffffff; font-family: "Tahoma", "Liberation Sans", "Arial"; font-size: 20pt; font-weight: bold; padding: 4px 12px;
    border: 1px solid #7f9db9; border-radius: 3px; background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #0b2e75, stop:1 #164aa8); }
QLabel#posChange { color: #1f6b2a; font-family: "Tahoma", "Liberation Sans", "Arial"; font-size: 14pt; font-weight: bold; padding: 2px 10px;
    border: 1px solid #7f9db9; border-radius: 3px; background: #f0fff0; }
)");
    css.replace("@BIG@", urdu ? "13pt" : "11pt").replace("@CAP@", urdu ? "11pt" : "9pt").replace("@BANNER@", urdu ? "20pt" : "18pt");
    return css;
}

// Windows 95/98 look. The native "Windows" style draws the raised and sunken 3D bevels,
// so the style sheet only adds captions, panels and a few colours.
static QString classicSheet(bool urdu)
{
    QString css = QStringLiteral(R"(
QLabel[role="h1"], QLabel#classicCaption { background: #000080; color: #ffffff; font-weight: bold; padding: 3px 6px; }
QLabel[role="h2"] { font-weight: bold; }
QLabel[role="stat"] { font-weight: bold; font-size: @BIG@; color: #000080; }
QLabel[role="muted"] { color: #404040; }
QLabel[role="pill"] { color: #000080; font-weight: bold; }
QFrame[role="card"], QFrame#statCard, QGroupBox[role="card"] {
    background: #c0c0c0; border-style: solid; border-width: 2px; border-color: #ffffff #808080 #808080 #ffffff; }
QFrame[role="cardHero"] { background: #000080; }
QFrame[role="cardHero"] QLabel { color: #ffffff; }
QFrame#statCard QLabel#statValue { font-weight: bold; font-size: @BIG@; color: #000080; }
QFrame#statCard QLabel#statCaption { color: #000000; }
QFrame#offlineBanner { background: #ffffe1; border-style: solid; border-width: 1px; border-color: #808080 #ffffff #ffffff #808080; }
QFrame#offlineBanner QLabel { color: #000000; }
QFrame#flagBanner { background: #ffffe1; border: 1px solid #000000; }
QFrame#flagBanner QLabel { color: #800000; }
QStatusBar QLabel#statusPanel { border-style: solid; border-width: 1px; border-color: #808080 #ffffff #ffffff #808080; padding: 1px 6px; }
QToolBar#classicToolbar { spacing: 1px; padding: 1px; }
QToolBar#classicToolbar QToolButton { padding: 2px 4px; }
QTreeWidget#classicTree { background: #ffffff; }
QFrame#classicDialog { background: #c0c0c0; }
QLabel#classicDialogCaption { background: #000080; color: #ffffff; font-weight: bold; padding: 3px 5px; }
QFrame#classicPicture { background: #008080; border-style: solid; border-width: 1px; border-color: #808080 #ffffff #ffffff #808080; }
QFrame#classicPicture QLabel { color: #ffffff; }
QLabel#authTitle { font-weight: bold; }
QLabel#authError { color: #800000; }
QLabel#authVersion { color: #404040; }
QLabel#authLabel { color: #000000; }
QToolTip { background: #ffffe1; color: #000000; border: 1px solid #000000; }
QLabel#posTotal { background: #000000; color: #00ff00; font-family: "Courier New", "Liberation Mono", monospace; font-size: 18pt; font-weight: bold;
    padding: 2px 8px; border-style: solid; border-width: 2px; border-color: #808080 #ffffff #ffffff #808080; }
QLabel#posChange { background: #000000; color: #ffff00; font-family: "Courier New", "Liberation Mono", monospace; font-size: 12pt; font-weight: bold;
    padding: 1px 8px; border-style: solid; border-width: 2px; border-color: #808080 #ffffff #ffffff #808080; }
)");
    css.replace("@BIG@", urdu ? "13pt" : "11pt");
    return css;
}
QColor muted() { return g_dark ? QColor("#9ca3af") : QColor("#6b7280"); }
QColor sidebarText() { return g_dark ? QColor("#c7cbe0") : QColor("#4b5563"); }

QString styleSheet(Mode mode, bool urdu)
{
    const bool dark = mode == Mode::Dark;
    const QString bg = dark ? "#0f1220" : "#f4f6fb";
    const QString surface = dark ? "#181c2e" : "#ffffff";
    const QString text = dark ? "#e5e7eb" : "#111827";
    const QString muted = dark ? "#9ca3af" : "#6b7280";
    const QString border = dark ? "#2a3047" : "#e5e7eb";
    const QString primary = dark ? "#6366f1" : "#4338ca";
    const QString primaryHover = dark ? "#4f46e5" : "#3730a3";
    const QString soft = dark ? "#23284a" : "#eef0ff";
    const QString hover = dark ? "#20253a" : "#f3f4f8";
    // Latin text uses Inter. Urdu uses Noto Naskh Arabic, which stays clear at small sizes in tables and forms.
    // Page titles in Urdu use Noto Nastaliq Urdu, the style Pakistani readers expect for headings.
    const QString family = urdu ? R"("Inter", "Noto Naskh Arabic")" : R"("Inter", "Segoe UI")";
    const QString size = urdu ? "11pt" : "10pt";
    const QString titleFamily = urdu ? R"("Inter", "Noto Nastaliq Urdu")" : family;
    const QString titleSize = urdu ? "21pt" : "18pt";
    QString css = QStringLiteral(R"(
QWidget { background: %1; color: %3; font-family: @FAMILY@; font-size: @SIZE@; }
QLabel { background: transparent; }
QLabel[role="muted"] { color: %4; }
QLabel[role="h1"] { font-family: @TFAMILY@; font-size: @TSIZE@; font-weight: 700; }
QLabel[role="h2"] { font-size: 12pt; font-weight: 600; }
QLabel[role="stat"] { font-size: 18pt; font-weight: 700; color: %3; }
QLabel[role="pill"] { background: %8; color: %6; border-radius: 10px; padding: 3px 10px; font-weight: 600; }
QFrame[role="card"] { background: %2; border: 1px solid %5; border-radius: 14px; }
QFrame[role="card"] QLabel { background: transparent; }
QFrame[role="cardHero"] { background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 %6, stop:1 #7c3aed); border-radius: 16px; }
QFrame[role="cardHero"] QLabel { color: white; }

QWidget#sidebarPanel { background: %2; border: none; }
QFrame#sidebarLine { background: %5; max-width: 1px; min-width: 1px; border: none; }
QLabel#sidebarBrand { color: %3; font-size: 15pt; font-weight: 700; }
QLabel#sidebarSub { color: %4; font-size: 8pt; }
QListWidget#sidebar { background: %2; border: none; border-radius: 0; outline: 0; padding: 0; }
QListWidget#sidebar::item { padding: 6px 10px; margin: 1px 12px; border-radius: 10px; color: @SIDETEXT@; border: none; }
QListWidget#sidebar::item:selected { background: %6; color: white; }
QListWidget#sidebar::item:hover:!selected { background: %9; }
QListWidget#sidebar::item:disabled { color: %4; background: transparent; padding: 8px 10px 2px 10px; font-size: 8pt; font-weight: 700; }
QListWidget#sidebar QScrollBar:vertical { background: transparent; width: 6px; }
QListWidget#sidebar QScrollBar::handle:vertical { background: %5; border-radius: 3px; min-height: 30px; }
QListWidget#sidebar QScrollBar::add-line, QListWidget#sidebar QScrollBar::sub-line { height: 0; }
QListWidget#sidebar QScrollBar::add-page, QListWidget#sidebar QScrollBar::sub-page { background: transparent; }

QWidget#topBar { background: %2; border-bottom: 1px solid %5; }
QWidget#topBar QLabel { background: transparent; }
QWidget#topBar QLabel#topDate { color: %4; font-size: 9pt; }
QWidget#topBar QLabel#topWelcome { font-size: 13pt; font-weight: 700; }
QWidget#topBar QLabel#avatar { background: %8; color: %6; border-radius: 19px; font-weight: 700; font-size: 11pt; }
QWidget#topBar QLabel#topUser { font-weight: 600; }
QWidget#topBar QLabel#topRole { color: %4; font-size: 9pt; }
QToolButton#topAction { background: transparent; border: 1px solid %5; border-radius: 10px; padding: 6px 12px; }
QToolButton#topAction:hover { background: %9; border-color: %6; }
QWidget#topBar QLabel#statusOnline { background: #e8f7ee; color: #157f3c; border-radius: 11px; padding: 3px 10px; font-weight: 600; font-size: 9pt; }
QWidget#topBar QLabel#statusOffline { background: #fff4d6; color: #8a6100; border-radius: 11px; padding: 3px 10px; font-weight: 600; font-size: 9pt; }

QFrame#statCard { background: %2; border: 1px solid %5; border-radius: 14px; }
QFrame#statCard QLabel { background: transparent; }
QFrame#statCard QLabel#statValue { font-size: 17pt; font-weight: 700; }
QFrame#statCard QLabel#statCaption { color: %4; }

QPushButton { background: %2; border: 1px solid %5; border-radius: 9px; padding: 7px 14px; }
QPushButton:hover { border-color: %6; background: %9; }
QPushButton:disabled { color: %4; }
QPushButton[role="primary"] { background: %6; color: white; border: none; font-weight: 600; }
QPushButton[role="primary"]:hover { background: %7; }
QPushButton[role="primary"]:disabled { background: %5; color: %4; }
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QDateEdit, QPlainTextEdit, QTextEdit {
    background: %2; border: 1px solid %5; border-radius: 9px; padding: 6px 9px; selection-background-color: %6; selection-color: white; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus, QDateEdit:focus, QPlainTextEdit:focus { border: 1px solid %6; }
QTableView, QTreeWidget, QListWidget { background: %2; border: 1px solid %5; border-radius: 12px;
    gridline-color: %5; selection-background-color: %8; selection-color: %3; alternate-background-color: %1; }
QTableView::item { padding: 4px 6px; }
QListWidget::item { padding: 6px 8px; border-radius: 6px; }
QListWidget::item:selected { background: %8; color: %6; }
QTableView::item:selected { background: %8; color: %3; }
QHeaderView { background: transparent; }
QHeaderView::section { background: %2; color: %4; border: none; border-bottom: 1px solid %5; padding: 8px 6px; font-weight: 600; }
QTableCornerButton::section { background: %2; border: none; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: %5; border-radius: 4px; min-height: 30px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: %5; border-radius: 4px; min-width: 30px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QProgressBar { border: 1px solid %5; border-radius: 6px; background: %2; text-align: center; height: 16px; }
QProgressBar::chunk { background: %6; border-radius: 6px; }
QTabWidget::pane { border: 1px solid %5; border-radius: 12px; background: %2; top: -1px; }
QTabBar::tab { padding: 8px 16px; background: transparent; color: %4; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { border-bottom: 2px solid %6; color: %6; font-weight: 600; }
QTabBar::tab:hover:!selected { color: %3; }
QFrame#offlineBanner { background: #fff4d6; border-bottom: 1px solid #f0d58a; }
QFrame#offlineBanner QLabel { color: #6b5300; }
QFrame#flagBanner { background: #fdeaea; border-radius: 10px; }
QFrame#flagBanner QLabel { color: #8a1f1f; }
QGroupBox { background: %2; border: 1px solid %5; border-radius: 14px; margin-top: 16px; padding: 14px 12px 10px 12px; font-weight: 600; }
QGroupBox::title { subcontrol-origin: margin; left: 14px; padding: 0 6px; color: %3; }
QGroupBox QLabel, QGroupBox QCheckBox { font-weight: 400; background: transparent; }
QCheckBox, QRadioButton { background: transparent; spacing: 8px; }
QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #c3c7d4; border-radius: 5px; background: %2; }
QCheckBox::indicator:hover { border-color: %6; }
QCheckBox::indicator:checked { background: %6; border-color: %6; image: url(:/icons/ui/check-white.png); }
QComboBox::down-arrow, QDateEdit::down-arrow { image: url(:/icons/ui/chevron-down-gray.png); width: 12px; height: 12px; }
QComboBox::drop-down, QDateEdit::drop-down { border: none; width: 24px; subcontrol-origin: padding; }
QSpinBox::up-button, QDoubleSpinBox::up-button { border: none; width: 20px; subcontrol-position: top right; }
QSpinBox::down-button, QDoubleSpinBox::down-button { border: none; width: 20px; subcontrol-position: bottom right; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(:/icons/ui/chevron-up-gray.png); width: 10px; height: 10px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(:/icons/ui/chevron-down-gray.png); width: 10px; height: 10px; }
QComboBox QAbstractItemView { background: %2; border: 1px solid %5; selection-background-color: %8; selection-color: %3; outline: 0; }
QToolTip { background: %3; color: %2; border: none; padding: 5px 8px; border-radius: 6px; }

QWidget#authRoot { background: transparent; }
QFrame#authCard { background: %2; border-radius: 22px; }
QFrame#authCard QLabel, QFrame#authCard QCheckBox, QRadioButton { background: transparent; spacing: 8px; }
QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #c3c7d4; border-radius: 5px; background: %2; }
QCheckBox::indicator:hover { border-color: %6; }
QCheckBox::indicator:checked { background: %6; border-color: %6; image: url(:/icons/ui/check-white.png); }
QComboBox::down-arrow, QDateEdit::down-arrow { image: url(:/icons/ui/chevron-down-gray.png); width: 12px; height: 12px; }
QComboBox::drop-down, QDateEdit::drop-down { border: none; width: 24px; subcontrol-origin: padding; }
QSpinBox::up-button, QDoubleSpinBox::up-button { border: none; width: 20px; subcontrol-position: top right; }
QSpinBox::down-button, QDoubleSpinBox::down-button { border: none; width: 20px; subcontrol-position: bottom right; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(:/icons/ui/chevron-up-gray.png); width: 10px; height: 10px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(:/icons/ui/chevron-down-gray.png); width: 10px; height: 10px; }
QComboBox QAbstractItemView { background: %2; border: 1px solid %5; selection-background-color: %8; selection-color: %3; outline: 0; }
QFrame#authBrand { background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #4338ca, stop:1 #6d28d9); border-radius: 22px; }
QFrame#authBrand QLabel { color: white; background: transparent; }
QLabel#authBrandName { font-size: 22pt; font-weight: 700; }
QLabel#authTagline { color: #e0e3ff; font-size: 11pt; }
QLabel#authFeature { color: #f1f2ff; font-size: 10pt; }
QLabel#authFooterBrand { color: #c7cbff; font-size: 9pt; }
QLabel#authTitle { font-family: @TFAMILY@; font-size: 20pt; font-weight: 700; }
QLabel#authSub { color: %4; }
QLabel#authLabel { font-weight: 600; font-size: 9pt; color: %3; }
QFrame#authCard QLineEdit { padding: 10px 12px; border-radius: 10px; background: %1; }
QFrame#authCard QLineEdit:focus { background: %2; }
QFrame#authCard QPushButton[role="primary"] { padding: 11px; border-radius: 10px; font-size: 11pt; }
QLabel#authError { color: #b42318; }
QLabel#authVersion { color: %4; font-size: 8pt; }
)").arg(bg, surface, text, muted, border, primary, primaryHover, soft, hover);
    css.replace("@FAMILY@", family).replace("@SIZE@", size).replace("@TFAMILY@", titleFamily).replace("@TSIZE@", titleSize)
       .replace("@SIDETEXT@", dark ? "#c7cbe0" : "#4b5563");
    return css;
}

void apply(QApplication &app, Mode mode, bool urdu)
{
    g_dark = mode == Mode::Dark;
    g_classic = mode == Mode::Classic;
    g_xp = mode == Mode::Xp;
    if (g_classic || g_xp) {
        // Windows 98 uses the native 3D bevels. Windows 2003 draws its soft look with the style sheet over Fusion.
        app.setStyle(QStyleFactory::create(g_xp ? "Fusion" : "Windows"));
        QPalette p;
        const QColor face(g_xp ? "#ece9d8" : "#c0c0c0");
        for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
            p.setColor(group, QPalette::Window, face);
            p.setColor(group, QPalette::Button, face);
            p.setColor(group, QPalette::Base, Qt::white);
            p.setColor(group, QPalette::AlternateBase, QColor("#f0f0f0"));
            p.setColor(group, QPalette::WindowText, Qt::black);
            p.setColor(group, QPalette::Text, Qt::black);
            p.setColor(group, QPalette::ButtonText, Qt::black);
            p.setColor(group, QPalette::Light, Qt::white);
            p.setColor(group, QPalette::Midlight, QColor("#dfdfdf"));
            p.setColor(group, QPalette::Mid, QColor("#a0a0a0"));
            p.setColor(group, QPalette::Dark, QColor("#808080"));
            p.setColor(group, QPalette::Shadow, Qt::black);
            p.setColor(group, QPalette::Highlight, QColor("#000080"));
            p.setColor(group, QPalette::HighlightedText, Qt::white);
            p.setColor(group, QPalette::ToolTipBase, QColor("#ffffe1"));
            p.setColor(group, QPalette::ToolTipText, Qt::black);
            p.setColor(group, QPalette::Link, QColor("#000080"));
            p.setColor(group, QPalette::PlaceholderText, QColor("#808080"));
        }
        if (g_xp) {
            for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
                p.setColor(group, QPalette::Midlight, QColor("#f5f4ea"));
                p.setColor(group, QPalette::Mid, QColor("#c5c2b2"));
                p.setColor(group, QPalette::Dark, QColor("#aca899"));
                p.setColor(group, QPalette::Shadow, QColor("#716f64"));
                p.setColor(group, QPalette::Highlight, QColor("#316ac5"));
                p.setColor(group, QPalette::Link, QColor("#215dc6"));
                p.setColor(group, QPalette::AlternateBase, QColor("#f7f7f2"));
            }
        }
        for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
            p.setColor(QPalette::Disabled, role, QColor(g_xp ? "#aca899" : "#808080"));
        app.setPalette(p);
        QFont f;
        const QStringList families = QFontDatabase::families();
        // Small system font, like MS Sans Serif. Urdu keeps a clear Naskh font.
        QStringList list;
        const QList<const char *> names = g_xp ? QList<const char *>{"Tahoma", "Liberation Sans", "Arial"}
                                               : QList<const char *>{"Microsoft Sans Serif", "MS Sans Serif", "Tahoma", "Liberation Sans", "Arial"};
        for (const char *name : names)
            if (families.contains(QString::fromLatin1(name))) list << QString::fromLatin1(name);
        if (families.contains("Noto Naskh Arabic")) list << "Noto Naskh Arabic";
        f.setFamilies(list);
        f.setPointSizeF(urdu ? 10 : 8);
        f.setStyleStrategy(urdu || g_xp ? QFont::PreferAntialias : QFont::NoAntialias);
        f.setHintingPreference(QFont::PreferFullHinting);
        app.setFont(f);
        app.setLayoutDirection(urdu ? Qt::RightToLeft : Qt::LeftToRight);
        app.setStyleSheet(g_xp ? xpSheet(urdu) : classicSheet(urdu));
        return;
    }
    app.setStyle(QStyleFactory::create("Fusion"));
    app.setPalette(app.style()->standardPalette());
    QFont f = app.font();
    const QStringList families = QFontDatabase::families();
    // Inter for Latin text, then a clear Urdu font. Windows fonts are the fallback when a bundled font is missing.
    QStringList list = {"Inter"};
    for (const char *name : {"Noto Naskh Arabic", "Segoe UI", "Urdu Typesetting"})
        if (families.contains(QString::fromLatin1(name))) list << QString::fromLatin1(name);
    f.setFamilies(list);
    f.setPointSizeF(urdu ? 11 : 10);
    f.setHintingPreference(QFont::PreferNoHinting);
    app.setFont(f);
    app.setLayoutDirection(urdu ? Qt::RightToLeft : Qt::LeftToRight);
    app.setStyleSheet(styleSheet(mode, urdu));
}
}
