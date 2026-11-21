#pragma once
#include <QMainWindow>
#include <QTimer>
#include <QVector>
#include <QKeySequence>
#include "AppContext.h"
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class QToolBar;
class QToolButton;

namespace mcp {
class OfflineBanner;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(AppContext *ctx, QWidget *parent = nullptr);
    void showPage(int page);
    void applyAutoLock();
signals:
    void languageChangeRequested(const QString &lang);
    void themeChangeRequested(const QString &theme);
    void signOutRequested();
    void lockRequested();
public slots:
    void rebuild();   // after a language change or a new sign in
protected:
    bool eventFilter(QObject *o, QEvent *e) override;
private:
    QWidget *createPage(int page);
    void openPage(int page);
    void buildPage(int page);
    void rebuildModern();
    void rebuildClassic();
    void buildMenus();
    void openReport(const QString &code);
    void sendKeyToFocus(QKeySequence::StandardKey key);
    bool allowed(int page) const;
    AppContext *m_ctx;
    QListWidget *m_nav = nullptr;
    QStackedWidget *m_stack = nullptr;
    OfflineBanner *m_banner = nullptr;
    QLabel *m_user = nullptr;
    QLabel *m_status = nullptr;
    QVector<QListWidgetItem *> m_items;   // sidebar item for each page
    QTreeWidget *m_tree = nullptr;          // classic theme navigation
    QVector<QTreeWidgetItem *> m_treeItems;
    QVector<QToolButton *> m_taskLinks;     // Windows 2003 task pane links, one per page
    QWidget *m_sidePanel = nullptr;         // tree or task pane, hidden by View > Navigation Panel
    QWidget *buildTaskPane(QWidget *parent);
    QToolBar *m_toolbar = nullptr;
    QTimer m_clock;
    QVector<bool> m_built;
    QTimer m_idle;
};
}
