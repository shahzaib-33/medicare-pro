#pragma once
#include <QDialog>
class QLineEdit;
class QLabel;
class QStackedWidget;

namespace mcp {

// First run owner setup, sign in, and the lock screen.
class LoginDialog : public QDialog {
    Q_OBJECT
public:
    enum class Mode { FirstRun, Login, Unlock };
    explicit LoginDialog(Mode mode, QWidget *parent = nullptr);
    bool switchUserRequested() const { return m_switchUser; }
private:
    QWidget *buildFirstRun();
    QWidget *buildLogin();
    QWidget *buildUnlock();
    QWidget *brandPanel();
    QWidget *heading(const QString &title, const QString &subtitle, QWidget *parent);
    QWidget *field(const QString &label, QWidget *edit, QWidget *parent);
protected:
    void paintEvent(QPaintEvent *e) override;
private:
    Mode m_mode;
    QLabel *m_error = nullptr;
    int m_unlockFails = 0;
    bool m_switchUser = false;
};

// Forced after an owner creates or resets an account, and available from the user menu.
class ChangePasswordDialog : public QDialog {
    Q_OBJECT
public:
    explicit ChangePasswordDialog(bool forced, QWidget *parent = nullptr);
};
}
