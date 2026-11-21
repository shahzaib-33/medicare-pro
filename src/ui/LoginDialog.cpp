#include "LoginDialog.h"
#include "Ui.h"
#include "services/AuthService.h"
#include "services/SettingsService.h"
#include "app/Version.h"
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QVBoxLayout>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QPainter>
#include <QPaintEvent>
#include "Theme.h"

namespace mcp {

LoginDialog::LoginDialog(Mode mode, QWidget *parent) : QDialog(parent), m_mode(mode)
{
    setWindowTitle(tr("MediCare Pro"));
    setWindowIcon(QIcon(":/icons/medicare.png"));
    setMinimumSize(980, 680);
    // The sign in and lock screens fill the screen, so nothing behind them shows.
    setWindowState(Qt::WindowMaximized);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 24, 24, 24);
    outer->addStretch();
    auto *row = new QHBoxLayout;
    row->addStretch();
    const bool classic = Theme::isClassic();
    auto *card = new QFrame(this);
    QHBoxLayout *ch = nullptr;
    QWidget *form = nullptr;
    QVBoxLayout *fv = nullptr;
    if (classic) {
        // A Windows 95/98 style dialog box on a teal desktop.
        const bool xp = Theme::isXp();
        card->setObjectName("classicDialog");
        card->setFrameStyle(xp ? QFrame::NoFrame : QFrame::WinPanel | QFrame::Raised);
        card->setFixedWidth(mode == Mode::FirstRun ? 640 : 520);
        auto *cv = new QVBoxLayout(card);
        cv->setContentsMargins(xp ? 1 : 3, xp ? 1 : 3, xp ? 1 : 3, xp ? 1 : 3);
        cv->setSpacing(0);
        auto *cap = new QLabel(mode == Mode::FirstRun ? tr("MediCare Pro Setup") : mode == Mode::Login ? tr("Log On to MediCare Pro")
                                                                                                    : tr("Unlock Workstation"), card);
        cap->setObjectName("classicDialogCaption");
        cv->addWidget(cap);
        if (xp) {
            // Windows 2003 log on box: a blue banner with the logo across the top.
            auto *banner = new QFrame(card);
            banner->setObjectName("xpBanner");
            banner->setFixedHeight(86);
            auto *bh = new QHBoxLayout(banner);
            bh->setContentsMargins(22, 8, 22, 8);
            bh->setSpacing(14);
            auto *logo = new QLabel(banner);
            logo->setPixmap(QPixmap(":/icons/medicare.png").scaled(52, 52, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            bh->addWidget(logo);
            auto *names = new QVBoxLayout;
            names->setSpacing(2);
            auto *bn = new QLabel("MediCare Pro", banner);
            bn->setObjectName("xpBannerName");
            names->addWidget(bn);
            names->addWidget(new QLabel(tr("Pharmacy Management System") + "   " + tr("Version %1").arg(appVersion()), banner));
            bh->addLayout(names, 1);
            cv->addWidget(banner);
        }
        auto *body = new QWidget(card);
        ch = new QHBoxLayout(body);
        ch->setContentsMargins(xp ? 22 : 10, xp ? 16 : 12, xp ? 22 : 10, xp ? 12 : 10);
        ch->setSpacing(12);
        auto *pic = new QFrame(body);
        pic->setVisible(!xp);
        pic->setObjectName("classicPicture");
        pic->setFixedWidth(130);
        auto *pv = new QVBoxLayout(pic);
        pv->setContentsMargins(8, 12, 8, 8);
        auto *icon = new QLabel(pic);
        icon->setPixmap(QPixmap(":/icons/medicare.png").scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        icon->setAlignment(Qt::AlignCenter);
        auto *nm = new QLabel("MediCare Pro", pic);
        nm->setAlignment(Qt::AlignCenter);
        nm->setStyleSheet("font-weight:bold;");
        auto *vr = new QLabel(tr("Version %1").arg(appVersion()), pic);
        vr->setAlignment(Qt::AlignCenter);
        pv->addWidget(icon);
        pv->addWidget(nm);
        pv->addWidget(vr);
        pv->addStretch();
        ch->addWidget(pic, 0, Qt::AlignTop);
        form = new QWidget(body);
        fv = new QVBoxLayout(form);
        fv->setContentsMargins(0, 0, 0, 0);
        fv->setSpacing(8);
        cv->addWidget(body);
    } else {
        card->setObjectName("authCard");
        card->setFixedWidth(mode == Mode::FirstRun ? 980 : 900);
        auto *shadow = new QGraphicsDropShadowEffect(card);
        shadow->setBlurRadius(60);
        shadow->setOffset(0, 18);
        shadow->setColor(QColor(30, 27, 75, 45));
        card->setGraphicsEffect(shadow);
        ch = new QHBoxLayout(card);
        ch->setContentsMargins(10, 10, 10, 10);
        ch->setSpacing(0);
        ch->addWidget(brandPanel());
        form = new QWidget(card);
        form->setObjectName("authRoot");
        fv = new QVBoxLayout(form);
        fv->setContentsMargins(46, 36, 46, 28);
        fv->setSpacing(12);
    }
    m_error = new QLabel(form);
    m_error->setObjectName("authError");
    m_error->setWordWrap(true);
    QWidget *content = mode == Mode::FirstRun ? buildFirstRun() : mode == Mode::Login ? buildLogin() : buildUnlock();
    fv->addWidget(content);
    fv->addWidget(m_error);
    fv->addStretch();
    if (!classic) {
        auto *ver = new QLabel(tr("Version %1. Works fully offline.").arg(appVersion()), form);
        ver->setObjectName("authVersion");
        fv->addWidget(ver);
    }
    ch->addWidget(form, 1);
    row->addWidget(card);
    row->addStretch();
    outer->addLayout(row);
    outer->addStretch();
}

void LoginDialog::paintEvent(QPaintEvent *)
{
    // Soft background with blurred capsules, drawn in code so no image file is needed.
    QPainter p(this);
    if (Theme::isXp()) {
        // Windows 2003 log on screen: a deep blue background.
        QLinearGradient g(0, 0, 0, height());
        g.setColorAt(0, QColor("#5a7edc"));
        g.setColorAt(0.6, QColor("#3c5fc6"));
        g.setColorAt(1, QColor("#2f4fb0"));
        p.fillRect(rect(), g);
        return;
    }
    if (Theme::isClassic()) { p.fillRect(rect(), QColor("#008080")); return; }
    p.setRenderHint(QPainter::Antialiasing);
    const bool dark = Theme::isDark();
    QLinearGradient g(0, 0, width(), height());
    g.setColorAt(0, dark ? QColor("#0f1220") : QColor("#eef1fb"));
    g.setColorAt(0.5, dark ? QColor("#141833") : QColor("#f6f7fc"));
    g.setColorAt(1, dark ? QColor("#1b1440") : QColor("#e6e9fb"));
    p.fillRect(rect(), g);
    struct Cap { double x, y, w, angle; const char *color; };
    static const Cap caps[] = {{0.06, 0.12, 170, 35, "#fbbf24"}, {0.18, 0.78, 210, -25, "#f87171"}, {0.86, 0.16, 190, -40, "#818cf8"},
                               {0.93, 0.70, 230, 20, "#5eead4"}, {0.40, 0.06, 120, 70, "#f9a8d4"}, {0.62, 0.92, 150, -60, "#fbbf24"},
                               {0.03, 0.50, 120, 80, "#a5b4fc"}, {0.75, 0.45, 100, 15, "#fca5a5"}, {0.30, 0.40, 90, -15, "#99f6e4"}};
    p.setPen(Qt::NoPen);
    for (const Cap &c : caps) {
        p.save();
        p.translate(c.x * width(), c.y * height());
        p.rotate(c.angle);
        for (int k = 3; k >= 0; --k) {
            QColor col(c.color);
            col.setAlpha(dark ? 10 + k * 4 : 18 + k * 8);
            const double grow = (3 - k) * 10.0;
            const double w = c.w + grow * 2, h = c.w * 0.42 + grow * 2;
            p.setBrush(col);
            p.drawRoundedRect(QRectF(-w / 2, -h / 2, w, h), h / 2, h / 2);
        }
        p.restore();
    }
}

QWidget *LoginDialog::brandPanel()
{
    auto *w = new QFrame(this);
    w->setObjectName("authBrand");
    w->setFixedWidth(340);
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(34, 38, 34, 30);
    v->setSpacing(10);
    auto *icon = new QLabel(w);
    icon->setPixmap(QPixmap(":/icons/medicare.png").scaled(68, 68, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    auto *name = new QLabel("MediCare Pro", w);
    name->setObjectName("authBrandName");
    auto *tag = new QLabel(tr("Pharmacy management software for Pakistan"), w);
    tag->setObjectName("authTagline");
    tag->setWordWrap(true);
    v->addWidget(icon);
    v->addSpacing(6);
    v->addWidget(name);
    v->addWidget(tag);
    v->addSpacing(18);
    for (const QString &f : {tr("Fast billing with barcode scanning"), tr("Batch and expiry tracking"),
                             tr("English and Urdu"), tr("Works fully offline"), tr("Daily automatic backup")}) {
        auto *h = new QHBoxLayout;
        h->setSpacing(10);
        auto *check = new QLabel(w);
        check->setPixmap(ui::iconPixmap("circle-check", QColor("#c7d2fe"), 18));
        auto *t = new QLabel(f, w);
        t->setObjectName("authFeature");
        t->setWordWrap(true);
        h->addWidget(check, 0, Qt::AlignTop);
        h->addWidget(t, 1);
        v->addLayout(h);
    }
    v->addStretch();
    auto *foot = new QLabel(tr("by SoftzenAgency"), w);
    foot->setObjectName("authFooterBrand");
    v->addWidget(foot);
    return w;
}

QWidget *LoginDialog::heading(const QString &title, const QString &subtitle, QWidget *parent)
{
    auto *w = new QWidget(parent);
    w->setObjectName("authRoot");
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 10);
    v->setSpacing(4);
    auto *t = new QLabel(title, w);
    t->setObjectName("authTitle");
    t->setVisible(!title.isEmpty());
    auto *s = new QLabel(subtitle, w);
    s->setObjectName("authSub");
    s->setWordWrap(true);
    v->addWidget(t);
    v->addWidget(s);
    return w;
}

QWidget *LoginDialog::field(const QString &label, QWidget *edit, QWidget *parent)
{
    auto *w = new QWidget(parent);
    w->setObjectName("authRoot");
    auto *l = new QLabel(label, w);
    l->setObjectName("authLabel");
    if (Theme::isClassic()) {
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        l->setFixedWidth(110);
        l->setBuddy(edit);
        h->addWidget(l);
        h->addWidget(edit, 1);
        return w;
    }
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(5);
    v->addWidget(l);
    v->addWidget(edit);
    return w;
}

QWidget *LoginDialog::buildFirstRun()
{
    auto *w = new QWidget(this);
    w->setObjectName("authRoot");
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(10);
    v->addWidget(heading(tr("Set up your pharmacy"),
                         tr("Welcome. Create the owner account to start. This account can manage users, prices and backups."), w));
    auto *f = new QGridLayout;
    f->setHorizontalSpacing(14);
    f->setVerticalSpacing(10);
    auto *pharmacy = new QLineEdit(w);
    auto *fullName = new QLineEdit(w);
    auto *user = new QLineEdit(w);
    auto *pass = new QLineEdit(w);
    auto *pass2 = new QLineEdit(w);
    pass->setEchoMode(QLineEdit::Password);
    pass2->setEchoMode(QLineEdit::Password);
    user->setText("owner");
    f->addWidget(field(tr("Pharmacy name"), pharmacy, w), 0, 0);
    f->addWidget(field(tr("Your full name"), fullName, w), 0, 1);
    f->addWidget(field(tr("Username"), user, w), 1, 0, 1, 2);
    f->addWidget(field(tr("Password"), pass, w), 2, 0);
    f->addWidget(field(tr("Repeat password"), pass2, w), 2, 1);
    v->addLayout(f);
    v->addWidget(ui::muted(tr("Use at least 8 characters with letters and numbers. Keep it safe. There is no online reset."), w));
    auto *ok = ui::button(tr("Create owner account"), w, true);
    if (Theme::isClassic()) {
        auto *h = new QHBoxLayout;
        h->addStretch();
        h->addWidget(ok);
        v->addLayout(h);
    } else {
        v->addWidget(ok);
    }
    connect(ok, &QPushButton::clicked, this, [=, this] {
        if (pharmacy->text().trimmed().isEmpty() || fullName->text().trimmed().isEmpty()) {
            m_error->setText(tr("Enter the pharmacy name and your name."));
            return;
        }
        if (pass->text() != pass2->text()) { m_error->setText(tr("The two passwords are different.")); return; }
        const Result r = AuthService::createFirstOwner(user->text(), fullName->text(), pass->text());
        if (!r.ok) { m_error->setText(r.error); return; }
        const Result l = AuthService::login(user->text(), pass->text());
        if (!l.ok) { m_error->setText(l.error); return; }
        PharmacyProfile p = SettingsService::profile();
        p.name = pharmacy->text().trimmed();
        SettingsService::saveProfile(p);
        accept();
    });
    return w;
}

QWidget *LoginDialog::buildLogin()
{
    auto *w = new QWidget(this);
    w->setObjectName("authRoot");
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(14);
    const QString shop = SettingsService::profile().name;
    if (Theme::isClassic())
        v->addWidget(heading(QString(), tr("Type a user name and password to log on to %1.").arg(shop.isEmpty() ? QStringLiteral("MediCare Pro") : shop), w));
    else
        v->addWidget(heading(tr("Welcome back"), shop.isEmpty() ? tr("Sign in to continue.") : tr("Sign in to %1.").arg(shop), w));
    auto *user = new QLineEdit(w);
    auto *pass = new QLineEdit(w);
    user->setPlaceholderText(tr("Enter your username"));
    pass->setPlaceholderText(tr("Enter your password"));
    pass->setEchoMode(QLineEdit::Password);
    if (!Theme::isClassic()) {
        user->addAction(ui::icon("user-round", Theme::muted()), QLineEdit::LeadingPosition);
        pass->addAction(ui::icon("lock", Theme::muted()), QLineEdit::LeadingPosition);
    }
    v->addWidget(field(tr("Username"), user, w));
    v->addWidget(field(tr("Password"), pass, w));
    v->addSpacing(6);
    auto *ok = ui::button(Theme::isClassic() ? tr("OK") : tr("Sign in"), w, true);
    ok->setDefault(true);
    ok->setCursor(Qt::PointingHandCursor);
    if (Theme::isClassic()) {
        auto *h = new QHBoxLayout;
        auto *cancel = ui::button(tr("Cancel"), w);
        for (auto *b : {ok, cancel}) b->setMinimumWidth(80);
        h->addStretch();
        h->addWidget(ok);
        h->addWidget(cancel);
        v->addLayout(h);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    } else {
        v->addWidget(ok);
    }
    user->setFocus();
    auto go = [=, this] {
        const Result r = AuthService::login(user->text(), pass->text());
        pass->clear();
        if (!r.ok) { m_error->setText(r.error); return; }
        if (Session::instance().user().mustChangePassword) {
            ChangePasswordDialog c(true, this);
            if (c.exec() != QDialog::Accepted) { AuthService::logout(); m_error->setText(tr("Set a new password to continue.")); return; }
        }
        accept();
    };
    connect(ok, &QPushButton::clicked, this, go);
    connect(pass, &QLineEdit::returnPressed, this, go);
    connect(user, &QLineEdit::returnPressed, pass, qOverload<>(&QWidget::setFocus));
    return w;
}

QWidget *LoginDialog::buildUnlock()
{
    auto *w = new QWidget(this);
    w->setObjectName("authRoot");
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(14);
    if (Theme::isClassic())
        v->addWidget(heading(QString(), tr("This workstation is locked. Only %1 or the owner can unlock it. Type the password to unlock.")
                                            .arg(Session::instance().user().fullName), w));
    else
        v->addWidget(heading(tr("Screen locked"), tr("Locked. %1 is signed in.").arg(Session::instance().user().fullName), w));
    auto *pass = new QLineEdit(w);
    pass->setEchoMode(QLineEdit::Password);
    pass->setPlaceholderText(tr("Enter your password"));
    if (!Theme::isClassic()) pass->addAction(ui::icon("lock", Theme::muted()), QLineEdit::LeadingPosition);
    v->addWidget(field(tr("Password"), pass, w));
    v->addSpacing(6);
    auto *unlock = ui::button(Theme::isClassic() ? tr("OK") : tr("Unlock"), w, true);
    unlock->setDefault(true);
    auto *other = ui::button(tr("Sign in as another user"), w);
    if (Theme::isClassic()) {
        auto *h = new QHBoxLayout;
        unlock->setMinimumWidth(80);
        h->addStretch();
        h->addWidget(unlock);
        h->addWidget(other);
        v->addLayout(h);
    } else {
        v->addWidget(unlock);
        v->addWidget(other);
    }
    pass->setFocus();
    auto go = [=, this] {
        const Result r = AuthService::verifyCurrentUser(pass->text());
        pass->clear();
        if (r.ok) { accept(); return; }
        if (++m_unlockFails >= 5) { m_switchUser = true; accept(); return; }
        m_error->setText(r.error);
    };
    connect(unlock, &QPushButton::clicked, this, go);
    connect(pass, &QLineEdit::returnPressed, this, go);
    connect(other, &QPushButton::clicked, this, [this] { m_switchUser = true; accept(); });
    return w;
}


ChangePasswordDialog::ChangePasswordDialog(bool forced, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Change password"));
    setMinimumWidth(400);
    auto *v = new QVBoxLayout(this);
    if (forced) v->addWidget(ui::muted(tr("Your password was set by the owner. Choose your own password now."), this));
    auto *f = new QFormLayout;
    auto *old = new QLineEdit(this);
    auto *p1 = new QLineEdit(this);
    auto *p2 = new QLineEdit(this);
    for (auto *e : {old, p1, p2}) e->setEchoMode(QLineEdit::Password);
    f->addRow(tr("Current password"), old);
    f->addRow(tr("New password"), p1);
    f->addRow(tr("Repeat new password"), p2);
    v->addLayout(f);
    auto *err = new QLabel(this);
    err->setStyleSheet("color:#b42318");
    err->setWordWrap(true);
    v->addWidget(err);
    auto *ok = ui::button(tr("Save password"), this, true);
    v->addWidget(ok);
    connect(ok, &QPushButton::clicked, this, [=, this] {
        if (p1->text() != p2->text()) { err->setText(tr("The two passwords are different.")); return; }
        const Result r = AuthService::changePassword(old->text(), p1->text());
        if (!r.ok) { err->setText(r.error); return; }
        accept();
    });
}
}
