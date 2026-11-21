#include "MainWindow.h"
#include "BackupPage.h"
#include "CatalogBrowserDialog.h"
#include "DashboardPage.h"
#include "InventoryPage.h"
#include "LoginDialog.h"
#include "MedicinesPage.h"
#include "OfflineBanner.h"
#include "OperationsPages.h"
#include "PartiesPage.h"
#include "PosPage.h"
#include "PurchasesPage.h"
#include "ReportsPage.h"
#include "SalesPage.h"
#include "SettingsPage.h"
#include "UpdateCenterPage.h"
#include "update/UpdateClient.h"
#include "UsersPage.h"
#include "Ui.h"
#include "Theme.h"
#include "app/Version.h"
#include "net/ConnectivityMonitor.h"
#include "services/AuthService.h"
#include "services/SettingsService.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QListWidget>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTime>
#include <QToolBar>
#include <QToolButton>
#include <QPushButton>
#include <QTreeWidget>
#include "db/Database.h"
#include <QDate>
#include <QLocale>
#include <QStyle>
#include <QToolButton>

namespace mcp {

MainWindow::MainWindow(AppContext *ctx, QWidget *parent) : QMainWindow(parent), m_ctx(ctx)
{
    setMinimumSize(1024, 640);
    setWindowIcon(QIcon(":/icons/medicare.png"));
    connect(AppEvents::instance(), &AppEvents::navigate, this, &MainWindow::showPage);
    m_idle.setSingleShot(true);
    connect(&m_idle, &QTimer::timeout, this, [this] { if (Session::instance().loggedIn() && isVisible()) emit lockRequested(); });
    qApp->installEventFilter(this);
}

bool MainWindow::eventFilter(QObject *o, QEvent *e)
{
    switch (e->type()) {
    case QEvent::KeyPress:
    case QEvent::MouseButtonPress:
    case QEvent::Wheel:
        if (m_idle.interval() > 0 && m_idle.isActive()) m_idle.start();
        break;
    default:
        break;
    }
    return QMainWindow::eventFilter(o, e);
}

void MainWindow::applyAutoLock()
{
    const int minutes = SettingsService::profile().autoLockMinutes;
    m_idle.stop();
    if (minutes > 0) {
        m_idle.setInterval(minutes * 60 * 1000);
        m_idle.start();
    } else {
        m_idle.setInterval(0);
    }
}

void MainWindow::showPage(int page)
{
    if (page < 0 || page >= Pages::Count || !allowed(page)) return;
    if (m_tree && page < m_treeItems.size() && m_treeItems[page]) { m_tree->setCurrentItem(m_treeItems[page]); return; }
    if (!m_taskLinks.isEmpty()) {
        openPage(page);
        for (int i = 0; i < m_taskLinks.size(); ++i)
            if (m_taskLinks[i]) m_taskLinks[i]->setChecked(i == page);
        return;
    }
    if (m_nav && page < m_items.size() && m_items[page] && !m_items[page]->isHidden()) { m_nav->setCurrentItem(m_items[page]); return; }
    openPage(page);
}

void MainWindow::openPage(int row)
{
    if (!m_stack || row < 0 || row >= Pages::Count || !allowed(row)) return;
    buildPage(row);
    m_stack->setCurrentIndex(row);
}

void MainWindow::buildPage(int row)
{
    if (!m_stack || row < 0 || row >= Pages::Count || !allowed(row)) return;
    if (!m_built[row]) {
        const int keep = m_stack->currentIndex();
        QWidget *placeholder = m_stack->widget(row);
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QWidget *page = createPage(row);
        QApplication::restoreOverrideCursor();
        m_stack->insertWidget(row, page);
        m_stack->removeWidget(placeholder);
        placeholder->deleteLater();
        m_built[row] = true;
        if (keep >= 0 && keep != row) m_stack->setCurrentIndex(keep);
    }
}

bool MainWindow::allowed(int page) const
{
    auto can = [](const char *p) { return Session::instance().can(p); };
    switch (page) {
    case Pages::Dashboard: return can("dashboard.view");
    case Pages::Pos: return can("pos.sell");
    case Pages::Sales: return can("sales.view");
    case Pages::Medicines: return can("medicines.view");
    case Pages::Inventory: return can("inventory.view");
    case Pages::Purchases: return can("purchases.view");
    case Pages::Suppliers: return can("suppliers.view");
    case Pages::Customers: return can("customers.view");
    case Pages::Prescriptions: return can("prescriptions.view");
    case Pages::Expenses: return can("expenses.view");
    case Pages::Shifts: return can("shifts.own") || can("shifts.manage");
    case Pages::Reports: return can("reports.sales") || can("reports.financial") || can("reports.stock") || can("audit.view");
    case Pages::Catalog: return can("medicines.view");
    case Pages::MedicineUpdates: return can("updates.manage") || can("refdata.import");
    case Pages::InfoUpdates: return can("updates.manage");
    case Pages::Users: return can("users.manage") || can("employees.manage");
    case Pages::Backup: return can("backup.manage");
    case Pages::Settings: return true;
    default: return false;
    }
}

static QWidget *scrolled(QWidget *w)
{
    auto *s = new QScrollArea;
    s->setWidgetResizable(true);
    s->setFrameShape(QFrame::NoFrame);
    s->setWidget(w);
    return s;
}

QWidget *MainWindow::createPage(int page)
{
    switch (page) {
    case Pages::Dashboard: return scrolled(new DashboardPage(m_ctx));
    case Pages::Pos: return new PosPage(m_ctx);
    case Pages::Sales: return new SalesPage(m_ctx);
    case Pages::Medicines: return new MedicinesPage(m_ctx);
    case Pages::Inventory: return new InventoryPage(m_ctx);
    case Pages::Purchases: return new PurchasesPage(m_ctx);
    case Pages::Suppliers: return new PartiesPage(PartiesPage::Kind::Supplier, m_ctx);
    case Pages::Customers: return new PartiesPage(PartiesPage::Kind::Customer, m_ctx);
    case Pages::Prescriptions: return new PrescriptionsPage(m_ctx);
    case Pages::Expenses: return new ExpensesPage(m_ctx);
    case Pages::Shifts: return new ShiftsPage(m_ctx);
    case Pages::Reports: return new ReportsPage(m_ctx);
    case Pages::Catalog: {
        auto *c = new CatalogBrowserDialog(m_ctx, Session::instance().can("medicines.edit"));
        c->setEmbedded();
        return c;
    }
    case Pages::MedicineUpdates: return new UpdateCenterPage(m_ctx, UpdateCenterPage::Mode::Medicine);
    case Pages::InfoUpdates: return new UpdateCenterPage(m_ctx, UpdateCenterPage::Mode::Information);
    case Pages::Users: return new UsersPage(m_ctx);
    case Pages::Backup: {
        auto *b = new BackupPage(m_ctx);
        connect(b, &BackupPage::restored, this, &MainWindow::signOutRequested);
        return b;
    }
    case Pages::Settings: {
        auto *s = new SettingsPage(m_ctx);
        connect(s, &SettingsPage::languageChanged, this, &MainWindow::languageChangeRequested);
        connect(s, &SettingsPage::themeChanged, this, &MainWindow::themeChangeRequested);
        connect(s, &SettingsPage::profileChanged, this, &MainWindow::applyAutoLock);
        return scrolled(s);
    }
    default: return new QWidget;
    }
}

static QString initials(const QString &name)
{
    QString out;
    for (const QString &part : name.split(' ', Qt::SkipEmptyParts)) {
        out += part.at(0).toUpper();
        if (out.size() == 2) break;
    }
    return out.isEmpty() ? QStringLiteral("?") : out;
}

void MainWindow::rebuild()
{
    setWindowTitle(tr("MediCare Pro %1 by SoftzenAgency").arg(appVersion()) + " - " + SettingsService::profile().name);
    const int current = m_stack ? m_stack->currentIndex() : -1;
    m_nav = nullptr;
    m_tree = nullptr;
    m_items.clear();
    m_treeItems.clear();
    m_taskLinks.clear();
    m_sidePanel = nullptr;
    m_clock.stop();
    m_clock.disconnect();
    if (m_toolbar) { removeToolBar(m_toolbar); m_toolbar->deleteLater(); m_toolbar = nullptr; }
    menuBar()->clear();
    if (Theme::isClassic()) {
        menuBar()->show();
        rebuildClassic();
    } else {
        menuBar()->hide();
        setStatusBar(nullptr);
        rebuildModern();
    }
    int start = current >= 0 && allowed(current) ? current : -1;
    if (start < 0) start = allowed(Pages::Dashboard) ? Pages::Dashboard : allowed(Pages::Pos) ? Pages::Pos : Pages::Settings;
    showPage(start);
    // The medicine update page runs the daily online check and installs prices, even when nobody opens it.
    if (m_ctx->updates && m_ctx->updates->config().autoInstallReference) buildPage(Pages::MedicineUpdates);
    applyAutoLock();
}

void MainWindow::rebuildModern()
{
    auto *central = new QWidget(this);
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Sidebar: logo, grouped menu with icons, version line.
    auto *side = new QWidget(central);
    side->setObjectName("sidebarPanel");
    side->setAttribute(Qt::WA_StyledBackground);
    side->setFixedWidth(qApp->layoutDirection() == Qt::RightToLeft ? 284 : 266);
    auto *sv = new QVBoxLayout(side);
    sv->setContentsMargins(0, 0, 0, 0);
    sv->setSpacing(0);
    auto *brandRow = new QWidget(side);
    auto *bh = new QHBoxLayout(brandRow);
    bh->setContentsMargins(22, 14, 16, 6);
    bh->setSpacing(10);
    auto *logo = new QLabel(brandRow);
    logo->setPixmap(QPixmap(":/icons/medicare.png").scaled(34, 34, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    auto *brandText = new QVBoxLayout;
    brandText->setSpacing(0);
    auto *brand = new QLabel(QStringLiteral("MediCare Pro"), brandRow);
    brand->setObjectName("sidebarBrand");
    auto *brandSub = new QLabel(SettingsService::profile().name, brandRow);
    brandSub->setObjectName("sidebarSub");
    brandText->addWidget(brand);
    brandText->addWidget(brandSub);
    bh->addWidget(logo);
    bh->addLayout(brandText, 1);
    sv->addWidget(brandRow);

    m_nav = new QListWidget(side);
    m_nav->setObjectName("sidebar");
    m_nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nav->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nav->setTextElideMode(Qt::ElideRight);
    m_nav->setIconSize(QSize(18, 18));
    m_nav->setFocusPolicy(Qt::NoFocus);
    m_nav->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    struct Entry { int page; QString icon; QString text; };
    struct Section { QString title; QList<Entry> entries; };
    const QList<Section> sections = {
        {tr("MAIN"), {{Pages::Dashboard, "layout-dashboard", tr("Dashboard")}, {Pages::Pos, "shopping-cart", tr("Point of Sale")},
                      {Pages::Sales, "receipt", tr("Sales history")}}},
        {tr("STOCK"), {{Pages::Medicines, "pill", tr("Medicines")}, {Pages::Inventory, "package", tr("Inventory and expiry")},
                       {Pages::Purchases, "truck", tr("Purchases")}, {Pages::Catalog, "database", tr("Pakistan Medicine Database")}}},
        {tr("PEOPLE"), {{Pages::Suppliers, "building-2", tr("Suppliers")}, {Pages::Customers, "users", tr("Customers")},
                        {Pages::Prescriptions, "clipboard-list", tr("Prescriptions")}}},
        {tr("MONEY"), {{Pages::Expenses, "wallet", tr("Expenses")}, {Pages::Shifts, "banknote", tr("Cash shift")},
                       {Pages::Reports, "chart-column", tr("Reports")}}},
        {tr("SYSTEM"), {{Pages::MedicineUpdates, "refresh-cw", tr("Medicine Database Updates")},
                        {Pages::InfoUpdates, "cloud-download", tr("Pharmacy Information Update Center")},
                        {Pages::Users, "shield-check", tr("Users and permissions")}, {Pages::Backup, "hard-drive", tr("Backup and restore")},
                        {Pages::Settings, "settings", tr("Settings")}}}};
    m_items = QVector<QListWidgetItem *>(Pages::Count, nullptr);
    const QColor iconColor = Theme::sidebarText();
    for (const Section &sec : sections) {
        auto *header = new QListWidgetItem(sec.title, m_nav);
        header->setFlags(Qt::NoItemFlags);
        header->setData(Qt::UserRole, -1);
        header->setSizeHint(QSize(0, 24));
        QFont hf = header->font();
        hf.setBold(true);
        hf.setPointSizeF(qApp->layoutDirection() == Qt::RightToLeft ? 9 : 7.5);
        hf.setLetterSpacing(QFont::PercentageSpacing, 108);
        header->setFont(hf);
        header->setForeground(Theme::muted());
        bool any = false;
        for (const Entry &e : sec.entries) {
            auto *it = new QListWidgetItem(ui::icon(e.icon, iconColor, Qt::white, 18), e.text, m_nav);
            it->setData(Qt::UserRole, e.page);
            it->setToolTip(e.text);
            it->setSizeHint(QSize(0, qApp->layoutDirection() == Qt::RightToLeft ? 34 : 32));
            m_items[e.page] = it;
            it->setHidden(!allowed(e.page));
            any = any || allowed(e.page);
        }
        header->setHidden(!any);
    }
    sv->addWidget(m_nav, 1);
    root->addWidget(side);
    auto *line = new QFrame(central);
    line->setObjectName("sidebarLine");
    root->addWidget(line);

    // Right side: top bar, offline banner, pages.
    auto *right = new QWidget(central);
    auto *rv = new QVBoxLayout(right);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(0);
    auto *top = new QWidget(right);
    top->setObjectName("topBar");
    top->setAttribute(Qt::WA_StyledBackground);
    auto *th = new QHBoxLayout(top);
    th->setContentsMargins(24, 10, 20, 10);
    th->setSpacing(12);
    auto *greet = new QVBoxLayout;
    greet->setSpacing(0);
    const QLocale dateLocale(qApp->layoutDirection() == Qt::RightToLeft ? QLocale::Urdu : QLocale::English, QLocale::Pakistan);
    auto *date = new QLabel(dateLocale.toString(QDate::currentDate(), QLocale::LongFormat), top);
    date->setObjectName("topDate");
    const UserInfo &u = Session::instance().user();
    auto *welcome = new QLabel(tr("Welcome, %1").arg(u.fullName.section(' ', 0, 0)), top);
    welcome->setObjectName("topWelcome");
    greet->addWidget(date);
    greet->addWidget(welcome);
    th->addLayout(greet);
    th->addStretch();
    m_status = new QLabel(top);
    th->addWidget(m_status);
    auto *avatar = new QLabel(initials(u.fullName), top);
    avatar->setObjectName("avatar");
    avatar->setFixedSize(38, 38);
    avatar->setAlignment(Qt::AlignCenter);
    auto *who = new QVBoxLayout;
    who->setSpacing(0);
    m_user = new QLabel(u.fullName, top);
    m_user->setObjectName("topUser");
    auto *role = new QLabel(QCoreApplication::translate("Roles", u.roleName.toUtf8().constData()), top);
    role->setObjectName("topRole");
    who->addWidget(m_user);
    who->addWidget(role);
    th->addSpacing(6);
    th->addWidget(avatar);
    th->addLayout(who);
    auto topButton = [&](const QString &iconName, const QString &text) {
        auto *b = new QToolButton(top);
        b->setObjectName("topAction");
        b->setIcon(QIcon(ui::iconPixmap(iconName, Theme::sidebarText(), 16)));
        b->setText(text);
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };
    auto *lock = topButton("lock", tr("Lock"));
    auto *out = topButton("log-out", tr("Sign out"));
    th->addSpacing(8);
    th->addWidget(lock);
    th->addWidget(out);
    rv->addWidget(top);
    m_banner = new OfflineBanner(right);
    rv->addWidget(m_banner);
    m_stack = new QStackedWidget(right);
    rv->addWidget(m_stack, 1);
    root->addWidget(right, 1);

    // Pages are created when first opened. This keeps start up fast on older PCs.
    m_built = QVector<bool>(Pages::Count, false);
    for (int i = 0; i < Pages::Count; ++i) m_stack->addWidget(new QWidget);
    connect(m_nav, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item) openPage(item->data(Qt::UserRole).toInt());
    });
    connect(lock, &QToolButton::clicked, this, &MainWindow::lockRequested);
    connect(out, &QToolButton::clicked, this, &MainWindow::signOutRequested);
    auto setOnline = [this](bool online) {
        m_status->setObjectName(online ? "statusOnline" : "statusOffline");
        m_status->setText(online ? tr("Online") : tr("Offline"));
        m_status->style()->unpolish(m_status);
        m_status->style()->polish(m_status);
    };
    if (m_ctx->network) {
        connect(m_ctx->network, &ConnectivityMonitor::onlineChanged, m_banner, &OfflineBanner::setOnline);
        connect(m_ctx->network, &ConnectivityMonitor::onlineChanged, this, setOnline);
        m_banner->setOnline(m_ctx->network->isOnline());
        setOnline(m_ctx->network->isOnline());
    } else {
        setOnline(false);
    }
    QWidget *old = centralWidget();
    setCentralWidget(central);
    if (old) old->deleteLater();
}
}

namespace mcp {

// Classic theme: menu bar, icon toolbar, tree navigation, workspace and status bar, as in 1990s Windows software.
void MainWindow::rebuildClassic()
{
    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(2, 2, 2, 0);
    root->setSpacing(0);
    auto *split = new QSplitter(Qt::Horizontal, central);
    split->setChildrenCollapsible(false);
    split->setHandleWidth(4);

    if (Theme::isXp()) {
        m_sidePanel = buildTaskPane(split);
    } else {
        // Left navigation: tree view with folders, like Windows Explorer.
        m_tree = new QTreeWidget(split);
        m_tree->setObjectName("classicTree");
        m_tree->setHeaderLabel(tr("Pharmacy Modules"));
        m_tree->setRootIsDecorated(true);
        m_tree->setIndentation(16);
        m_tree->setUniformRowHeights(true);
        m_tree->setMinimumWidth(200);
        m_tree->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
        const QIcon folder = style()->standardIcon(QStyle::SP_DirClosedIcon);
        const QIcon folderOpen = style()->standardIcon(QStyle::SP_DirOpenIcon);
        QIcon folderIcon;
        folderIcon.addPixmap(folder.pixmap(16), QIcon::Normal, QIcon::Off);
        folderIcon.addPixmap(folderOpen.pixmap(16), QIcon::Normal, QIcon::On);
        struct Entry { int page; QString icon; QString color; QString text; };
        struct Group { QString title; QList<Entry> entries; };
        const QList<Group> groups = {
            {tr("Sales"), {{Pages::Dashboard, "layout-dashboard", "#000080", tr("Dashboard")},
                           {Pages::Pos, "shopping-cart", "#008000", tr("Point of Sale / Medicine Billing")},
                           {Pages::Sales, "receipt", "#000080", tr("Sales history")},
                           {Pages::Shifts, "banknote", "#008000", tr("Cash shift")}}},
            {tr("Inventory"), {{Pages::Medicines, "pill", "#800000", tr("Medicine Inventory")},
                               {Pages::Inventory, "package", "#808000", tr("Expiry, Batch and Barcode")},
                               {Pages::Catalog, "database", "#008080", tr("Medicine Database")}}},
            {tr("Purchasing"), {{Pages::Purchases, "truck", "#000080", tr("Purchase Management")},
                                {Pages::Suppliers, "building-2", "#800080", tr("Suppliers")}}},
            {tr("Customers"), {{Pages::Customers, "users", "#000080", tr("Customers")},
                               {Pages::Prescriptions, "clipboard-list", "#800000", tr("Prescriptions")}}},
            {tr("Accounts"), {{Pages::Expenses, "wallet", "#808000", tr("Expenses")},
                              {Pages::Reports, "chart-column", "#008080", tr("Stock and Sales Reports")}}},
            {tr("Administration"), {{Pages::Users, "shield-check", "#000080", tr("User Management")},
                                    {Pages::Backup, "hard-drive", "#404040", tr("Backup and Restore")},
                                    {Pages::MedicineUpdates, "refresh-cw", "#008000", tr("Medicine Database Updates")},
                                    {Pages::InfoUpdates, "cloud-download", "#000080", tr("Software Updates")},
                                    {Pages::Settings, "settings", "#404040", tr("Settings")}}}};
        m_treeItems = QVector<QTreeWidgetItem *>(Pages::Count, nullptr);
        auto *top = new QTreeWidgetItem(m_tree, {SettingsService::profile().name.isEmpty() ? QStringLiteral("MediCare Pro") : SettingsService::profile().name});
        top->setIcon(0, QIcon(QPixmap(":/icons/medicare.png").scaled(16, 16, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        top->setData(0, Qt::UserRole, -1);
        for (const Group &g : groups) {
            auto *gi = new QTreeWidgetItem(top, {g.title});
            gi->setIcon(0, folderIcon);
            gi->setData(0, Qt::UserRole, -1);
            bool any = false;
            for (const Entry &e : g.entries) {
                auto *it = new QTreeWidgetItem(gi, {e.text});
                it->setIcon(0, QIcon(ui::iconPixmap(e.icon, QColor(e.color), 16)));
                it->setData(0, Qt::UserRole, e.page);
                it->setHidden(!allowed(e.page));
                any = any || allowed(e.page);
                m_treeItems[e.page] = it;
            }
            gi->setHidden(!any);
        }
        m_tree->expandAll();
        connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
            if (item) openPage(item->data(0, Qt::UserRole).toInt());
        });
        m_sidePanel = m_tree;
    }

    // Workspace: a sunken frame that holds the open module.
    auto *work = new QFrame(split);
    work->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    auto *wv = new QVBoxLayout(work);
    wv->setContentsMargins(2, 2, 2, 2);
    wv->setSpacing(0);
    m_banner = new OfflineBanner(work);
    wv->addWidget(m_banner);
    m_stack = new QStackedWidget(work);
    wv->addWidget(m_stack, 1);
    split->addWidget(m_sidePanel);
    split->addWidget(work);
    split->setStretchFactor(1, 1);
    split->setSizes({Theme::isXp() ? 215 : 250, 1100});
    root->addWidget(split, 1);

    m_built = QVector<bool>(Pages::Count, false);
    for (int i = 0; i < Pages::Count; ++i) m_stack->addWidget(new QWidget);

    // Status bar: user, date, time, database and connection.
    auto *sb = new QStatusBar(this);
    sb->setSizeGripEnabled(true);
    auto panel = [sb](const QString &text, int stretch = 0) {
        auto *l = new QLabel(text, sb);
        l->setObjectName("statusPanel");
        sb->addPermanentWidget(l, stretch);
        return l;
    };
    const UserInfo &u = Session::instance().user();
    m_user = panel(tr("User: %1 (%2)").arg(u.fullName, QCoreApplication::translate("Roles", u.roleName.toUtf8().constData())), 1);
    auto *date = panel(QDate::currentDate().toString("dd-MM-yyyy"));
    auto *time = panel(QTime::currentTime().toString("hh:mm:ss AP"));
    const bool dbOk = Db::get(Db::Pharmacy).isOpen();
    panel(dbOk ? tr("Database: Connected") : tr("Database: Not connected"));
    m_status = panel(QString());
    setStatusBar(sb);
    m_clock.setInterval(1000);
    connect(&m_clock, &QTimer::timeout, this, [date, time] {
        date->setText(QDate::currentDate().toString("dd-MM-yyyy"));
        time->setText(QTime::currentTime().toString("hh:mm:ss AP"));
    });
    m_clock.start();
    auto setOnline = [this](bool online) { if (m_status) m_status->setText(online ? tr("Online") : tr("Offline")); };
    if (m_ctx->network) {
        connect(m_ctx->network, &ConnectivityMonitor::onlineChanged, m_banner, &OfflineBanner::setOnline);
        connect(m_ctx->network, &ConnectivityMonitor::onlineChanged, m_status, setOnline);
        m_banner->setOnline(m_ctx->network->isOnline());
        setOnline(m_ctx->network->isOnline());
    } else {
        setOnline(false);
    }

    buildMenus();
    QWidget *old = centralWidget();
    setCentralWidget(central);
    if (old) old->deleteLater();
}

// Windows 2003 task pane: blue panel with groups of links, like the Explorer side bar.
QWidget *MainWindow::buildTaskPane(QWidget *parent)
{
    auto *scroll = new QScrollArea(parent);
    scroll->setObjectName("xpTaskPane");
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumWidth(205);
    scroll->setMaximumWidth(280);
    auto *body = new QWidget(scroll);
    body->setObjectName("xpTaskPaneBody");
    auto *v = new QVBoxLayout(body);
    v->setContentsMargins(10, 8, 10, 8);
    v->setSpacing(8);
    struct Entry { int page; QString icon; QString text; };
    struct Group { QString title; QList<Entry> entries; };
    const QList<Group> groups = {
        {tr("Sales Tasks"), {{Pages::Dashboard, "layout-dashboard", tr("Dashboard")},
                             {Pages::Pos, "shopping-cart", tr("Point of Sale")},
                             {Pages::Sales, "receipt", tr("Sales history")},
                             {Pages::Shifts, "banknote", tr("Cash shift")}}},
        {tr("Inventory"), {{Pages::Medicines, "pill", tr("Medicine Inventory")},
                           {Pages::Inventory, "package", tr("Expiry, Batch and Barcode")},
                           {Pages::Catalog, "database", tr("Medicine Database")}}},
        {tr("Purchasing"), {{Pages::Purchases, "truck", tr("Purchase Management")},
                            {Pages::Suppliers, "building-2", tr("Suppliers")}}},
        {tr("Customers"), {{Pages::Customers, "users", tr("Customers")},
                           {Pages::Prescriptions, "clipboard-list", tr("Prescriptions")}}},
        {tr("Accounts"), {{Pages::Expenses, "wallet", tr("Expenses")},
                          {Pages::Reports, "chart-column", tr("Stock and Sales Reports")}}},
        {tr("Administration"), {{Pages::Users, "shield-check", tr("User Management")},
                                {Pages::Backup, "hard-drive", tr("Backup and Restore")},
                                {Pages::MedicineUpdates, "refresh-cw", tr("Medicine Database Updates")},
                                {Pages::InfoUpdates, "cloud-download", tr("Software Updates")},
                                {Pages::Settings, "settings", tr("Settings")}}}};
    m_taskLinks = QVector<QToolButton *>(Pages::Count, nullptr);
    auto addGroup = [&](const QString &title, bool main, QWidget *content) {
        auto *g = new QWidget(body);
        auto *gv = new QVBoxLayout(g);
        gv->setContentsMargins(0, 0, 0, 0);
        gv->setSpacing(0);
        auto *head = new QPushButton(title, g);
        head->setObjectName(main ? "xpTaskHeaderMain" : "xpTaskHeader");
        head->setCursor(Qt::PointingHandCursor);
        head->setFocusPolicy(Qt::NoFocus);
        head->setToolTip(tr("Show or hide this group"));
        gv->addWidget(head);
        gv->addWidget(content);
        connect(head, &QPushButton::clicked, content, [content] { content->setVisible(!content->isVisible()); });
        v->addWidget(g);
    };
    bool first = true;
    for (const Group &gr : groups) {
        auto *box = new QFrame(body);
        box->setObjectName("xpTaskBody");
        auto *bv = new QVBoxLayout(box);
        bv->setContentsMargins(9, 4, 9, 5);
        bv->setSpacing(0);
        bool any = false;
        for (const Entry &e : gr.entries) {
            if (!allowed(e.page)) continue;
            auto *link = new QToolButton(box);
            link->setObjectName("xpTaskLink");
            link->setText(e.text);
            link->setIcon(QIcon(ui::iconPixmap(e.icon, QColor("#215dc6"), 16)));
            link->setIconSize(QSize(16, 16));
            link->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            link->setCheckable(true);
            link->setAutoExclusive(false);
            link->setCursor(Qt::PointingHandCursor);
            link->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            const int page = e.page;
            connect(link, &QToolButton::clicked, this, [this, page] { showPage(page); });
            bv->addWidget(link);
            m_taskLinks[page] = link;
            any = true;
        }
        if (!any) { delete box; continue; }
        addGroup(gr.title, first, box);
        first = false;
    }
    // Details: who is signed in and which pharmacy, like the XP "Details" box.
    auto *det = new QFrame(body);
    det->setObjectName("xpTaskBody");
    auto *dv = new QVBoxLayout(det);
    dv->setContentsMargins(9, 6, 9, 7);
    const UserInfo &u = Session::instance().user();
    const QString shop = SettingsService::profile().name.isEmpty() ? QStringLiteral("MediCare Pro") : SettingsService::profile().name;
    auto *info = new QLabel(QStringLiteral("<b>%1</b><br>%2<br>%3").arg(shop.toHtmlEscaped(), u.fullName.toHtmlEscaped(),
        QCoreApplication::translate("Roles", u.roleName.toUtf8().constData()).toHtmlEscaped()), det);
    info->setObjectName("xpTaskDetail");
    info->setWordWrap(true);
    dv->addWidget(info);
    addGroup(tr("Details"), false, det);
    v->addStretch();
    scroll->setWidget(body);
    return scroll;
}

void MainWindow::openReport(const QString &code)
{
    showPage(Pages::Reports);
    if (auto *r = m_stack ? qobject_cast<ReportsPage *>(m_stack->widget(Pages::Reports)) : nullptr) r->selectReport(code);
}

void MainWindow::sendKeyToFocus(QKeySequence::StandardKey key)
{
    QWidget *w = QApplication::focusWidget();
    const QList<QKeySequence> seq = QKeySequence::keyBindings(key);
    if (!w || seq.isEmpty()) return;
    const QKeyCombination k = seq.first()[0];
    QKeyEvent press(QEvent::KeyPress, k.key(), k.keyboardModifiers());
    QKeyEvent release(QEvent::KeyRelease, k.key(), k.keyboardModifiers());
    QApplication::sendEvent(w, &press);
    QApplication::sendEvent(w, &release);
}

void MainWindow::buildMenus()
{
    QMenuBar *mb = menuBar();
    auto go = [this](QMenu *m, const QString &text, int page, const QString &icon = {}, const QString &color = "#000080",
                     const QKeySequence &key = {}) {
        QAction *a = m->addAction(text);
        if (!icon.isEmpty()) a->setIcon(QIcon(ui::iconPixmap(icon, QColor(color), 16)));
        if (!key.isEmpty()) a->setShortcut(key);
        a->setEnabled(allowed(page));
        connect(a, &QAction::triggered, this, [this, page] { showPage(page); });
        return a;
    };
    auto report = [this](QMenu *m, const QString &text, const QString &code) {
        QAction *a = m->addAction(text);
        a->setEnabled(allowed(Pages::Reports));
        connect(a, &QAction::triggered, this, [this, code] { openReport(code); });
    };
    auto edit = [this](QMenu *m, const QString &text, QKeySequence::StandardKey key) {
        QAction *a = m->addAction(text + "\t" + QKeySequence(key).toString(QKeySequence::NativeText));
        connect(a, &QAction::triggered, this, [this, key] { sendKeyToFocus(key); });
    };

    QMenu *file = mb->addMenu(tr("&File"));
    go(file, tr("&New Sale"), Pages::Pos, "shopping-cart", "#008000", QKeySequence(Qt::CTRL | Qt::Key_N));
    file->addSeparator();
    go(file, tr("&Back Up Database..."), Pages::Backup, "hard-drive", "#404040");
    file->addSeparator();
    QAction *lock = file->addAction(QIcon(ui::iconPixmap("lock", QColor("#800000"), 16)), tr("&Lock Workstation"));
    lock->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    connect(lock, &QAction::triggered, this, &MainWindow::lockRequested);
    QAction *out = file->addAction(QIcon(ui::iconPixmap("log-out", QColor("#000080"), 16)), tr("Log &Off..."));
    connect(out, &QAction::triggered, this, &MainWindow::signOutRequested);
    file->addSeparator();
    QAction *exit = file->addAction(tr("E&xit"));
    exit->setShortcut(QKeySequence(Qt::ALT | Qt::Key_F4));
    connect(exit, &QAction::triggered, this, [] { qApp->closeAllWindows(); qApp->quit(); });

    QMenu *editMenu = mb->addMenu(tr("&Edit"));
    edit(editMenu, tr("&Undo"), QKeySequence::Undo);
    editMenu->addSeparator();
    edit(editMenu, tr("Cu&t"), QKeySequence::Cut);
    edit(editMenu, tr("&Copy"), QKeySequence::Copy);
    edit(editMenu, tr("&Paste"), QKeySequence::Paste);
    edit(editMenu, tr("&Delete"), QKeySequence::Delete);
    editMenu->addSeparator();
    edit(editMenu, tr("Select &All"), QKeySequence::SelectAll);

    QMenu *view = mb->addMenu(tr("&View"));
    QAction *tb = view->addAction(tr("&Toolbar"));
    tb->setCheckable(true);
    tb->setChecked(true);
    QAction *st = view->addAction(tr("&Status Bar"));
    st->setCheckable(true);
    st->setChecked(true);
    connect(st, &QAction::toggled, this, [this](bool on) { if (statusBar()) statusBar()->setVisible(on); });
    QAction *nav = view->addAction(tr("&Navigation Panel"));
    nav->setCheckable(true);
    nav->setChecked(true);
    connect(nav, &QAction::toggled, this, [this](bool on) { if (m_sidePanel) m_sidePanel->setVisible(on); });
    view->addSeparator();
    go(view, tr("&Dashboard"), Pages::Dashboard, "layout-dashboard");
    QAction *refresh = view->addAction(tr("&Refresh"));
    refresh->setShortcut(QKeySequence(Qt::Key_F5));
    connect(refresh, &QAction::triggered, this, [] { emit AppEvents::instance()->pharmacyChanged(); });

    QMenu *inv = mb->addMenu(tr("&Inventory"));
    go(inv, tr("&Medicine Inventory"), Pages::Medicines, "pill", "#800000");
    go(inv, tr("&Expiry Management"), Pages::Inventory, "clock", "#808000");
    go(inv, tr("&Batch and Barcode Management"), Pages::Inventory, "package", "#808000");
    go(inv, tr("Medicine &Database"), Pages::Catalog, "database", "#008080");
    inv->addSeparator();
    go(inv, tr("&Purchase Management"), Pages::Purchases, "truck");
    go(inv, tr("&Suppliers"), Pages::Suppliers, "building-2", "#800080");

    QMenu *sales = mb->addMenu(tr("&Sales"));
    go(sales, tr("&Point of Sale"), Pages::Pos, "shopping-cart", "#008000");
    go(sales, tr("Sales &History"), Pages::Sales, "receipt");
    sales->addSeparator();
    go(sales, tr("&Customers"), Pages::Customers, "users");
    go(sales, tr("P&rescriptions"), Pages::Prescriptions, "clipboard-list", "#800000");
    go(sales, tr("Cash &Shift"), Pages::Shifts, "banknote", "#008000");
    go(sales, tr("&Expenses"), Pages::Expenses, "wallet", "#808000");

    QMenu *rep = mb->addMenu(tr("&Reports"));
    go(rep, tr("&All Reports..."), Pages::Reports, "chart-column", "#008080");
    rep->addSeparator();
    report(rep, tr("&Sales Report"), "sales_daily");
    report(rep, tr("Sales by &Medicine"), "sales_by_medicine");
    report(rep, tr("S&tock Report"), "stock_valuation");
    report(rep, tr("&Low Stock Report"), "low_stock");
    report(rep, tr("E&xpiry Report"), "expiry");
    report(rep, tr("&Profit and Loss"), "profit_loss");
    report(rep, tr("&Expenses Report"), "expenses");

    QMenu *tools = mb->addMenu(tr("&Tools"));
    go(tools, tr("&User Management"), Pages::Users, "shield-check");
    go(tools, tr("&Backup and Restore"), Pages::Backup, "hard-drive", "#404040");
    tools->addSeparator();
    go(tools, tr("&Medicine Database Updates"), Pages::MedicineUpdates, "refresh-cw", "#008000");
    go(tools, tr("&Software Updates"), Pages::InfoUpdates, "cloud-download");
    tools->addSeparator();
    go(tools, tr("&Options..."), Pages::Settings, "settings", "#404040");

    QMenu *help = mb->addMenu(tr("&Help"));
    QAction *keys = help->addAction(tr("&Keyboard Shortcuts"));
    keys->setShortcut(QKeySequence(Qt::Key_F1));
    connect(keys, &QAction::triggered, this, [this] {
        QMessageBox::information(this, tr("Keyboard Shortcuts"),
            tr("Point of Sale\n"
               "F2\tSearch medicine by name\n"
               "F4\tBarcode input\n"
               "F6\tHold bill\n"
               "F7\tHeld bills\n"
               "F8\tCash received\n"
               "F9\tSave sale\n"
               "F12\tPrint invoice\n"
               "Delete\tRemove selected line\n\n"
               "Everywhere\n"
               "Ctrl+N\tNew sale\n"
               "Ctrl+L\tLock workstation\n"
               "F5\tRefresh\n"
               "F1\tThis list"));
    });
    help->addSeparator();
    QAction *about = help->addAction(tr("&About MediCare Pro..."));
    connect(about, &QAction::triggered, this, [this] {
        QMessageBox box(this);
        box.setWindowTitle(tr("About MediCare Pro"));
        box.setIconPixmap(QPixmap(":/icons/medicare.png").scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        box.setText(tr("MediCare Pro\nVersion %1\n\nPharmacy management software for Pakistan.\nby SoftzenAgency").arg(appVersion()));
        box.exec();
    });

    // Icon toolbar under the menu bar.
    m_toolbar = new QToolBar(tr("Standard"), this);
    m_toolbar->setObjectName("classicToolbar");
    m_toolbar->setMovable(false);
    // Windows 2003 puts larger colour icons with the label beside them.
    const int tbIcon = Theme::isXp() ? 24 : 20;
    m_toolbar->setIconSize(QSize(tbIcon, tbIcon));
    m_toolbar->setToolButtonStyle(Theme::isXp() ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonTextUnderIcon);
    auto tool = [this, tbIcon](const QString &icon, const QString &color, const QString &text, int page) {
        QAction *a = m_toolbar->addAction(QIcon(ui::iconPixmap(icon, QColor(color), tbIcon)), text);
        a->setEnabled(allowed(page));
        connect(a, &QAction::triggered, this, [this, page] { showPage(page); });
    };
    tool("shopping-cart", "#008000", tr("Billing"), Pages::Pos);
    tool("receipt", "#000080", tr("Sales"), Pages::Sales);
    m_toolbar->addSeparator();
    tool("pill", "#800000", tr("Medicines"), Pages::Medicines);
    tool("package", "#808000", tr("Stock"), Pages::Inventory);
    tool("database", "#008080", tr("Database"), Pages::Catalog);
    tool("truck", "#000080", tr("Purchase"), Pages::Purchases);
    m_toolbar->addSeparator();
    tool("users", "#000080", tr("Customers"), Pages::Customers);
    tool("chart-column", "#008080", tr("Reports"), Pages::Reports);
    tool("hard-drive", "#404040", tr("Backup"), Pages::Backup);
    m_toolbar->addSeparator();
    QAction *tlock = m_toolbar->addAction(QIcon(ui::iconPixmap("lock", QColor("#800000"), tbIcon)), tr("Lock"));
    connect(tlock, &QAction::triggered, this, &MainWindow::lockRequested);
    QAction *texit = m_toolbar->addAction(QIcon(ui::iconPixmap("log-out", QColor("#000080"), tbIcon)), tr("Log Off"));
    connect(texit, &QAction::triggered, this, &MainWindow::signOutRequested);
    addToolBar(Qt::TopToolBarArea, m_toolbar);
    connect(tb, &QAction::toggled, m_toolbar, &QToolBar::setVisible);
}
}
