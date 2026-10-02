#include "qautoclosemessagebox.h"

#include <QPushButton>
#include <QDialogButtonBox>
#include <QDebug>

QMessageBox::StandardButton QAutoCloseMessageBox::critical(QWidget* parent, const QString& text, int timeout, const QString& title,
                                                           QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
    return QAutoCloseMessageBox::critical(parent, (title.isEmpty() && parent ? parent->windowTitle() : title), text, buttons, defaultButton, timeout);
}

QMessageBox::StandardButton QAutoCloseMessageBox::information(QWidget* parent, const QString& text, int timeout, const QString& title,
                                                              QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
    return QAutoCloseMessageBox::information(parent, (title.isEmpty() && parent ? parent->windowTitle() : title), text, buttons, defaultButton, timeout);
}

QMessageBox::StandardButton QAutoCloseMessageBox::question(QWidget* parent, const QString& text, int timeout, const QString& title,
                                                           QMessageBox::StandardButtons buttons,
                                                           QMessageBox::StandardButton defaultButton)
{
    return QAutoCloseMessageBox::question(parent, (title.isEmpty() && parent ? parent->windowTitle() : title), text, buttons, defaultButton, timeout);
}

QMessageBox::StandardButton QAutoCloseMessageBox::warning(QWidget* parent, const QString& text, int timeout, const QString& title,
                                                          QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
    return QAutoCloseMessageBox::warning(parent, (title.isEmpty() && parent ? parent->windowTitle() : title), text, buttons, defaultButton, timeout);
}


QMessageBox::StandardButton QAutoCloseMessageBox::critical(int timeout, QWidget *parent, const QString &title, const QString &text,
                                                           QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
    return QAutoCloseMessageBox::showNewMessageBox(parent, QMessageBox::Critical, title, text, buttons, defaultButton, timeout);
}

QMessageBox::StandardButton QAutoCloseMessageBox::information(int timeout, QWidget *parent, const QString &title, const QString &text,
                                                              QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
    return QAutoCloseMessageBox::showNewMessageBox(parent, QMessageBox::Information, title, text, buttons, defaultButton, timeout);
}

QMessageBox::StandardButton QAutoCloseMessageBox::question(int timeout, QWidget *parent, const QString &title, const QString &text,
                                                           QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
    return QAutoCloseMessageBox::showNewMessageBox(parent, QMessageBox::Question, title, text, buttons, defaultButton, timeout);
}

QMessageBox::StandardButton QAutoCloseMessageBox::warning(int timeout, QWidget *parent, const QString &title, const QString &text,
                                                          QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
    return QAutoCloseMessageBox::showNewMessageBox(parent, QMessageBox::Warning, title, text, buttons, defaultButton, timeout);
}

/********Compatibility*************/

QMessageBox::StandardButton QAutoCloseMessageBox::critical(QWidget *parent, const QString &title, const QString &text,
                                                           QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton, int timeout)
{
    return QAutoCloseMessageBox::showNewMessageBox(parent, QMessageBox::Critical, title, text, buttons, defaultButton, timeout);
}

QMessageBox::StandardButton QAutoCloseMessageBox::information(QWidget *parent, const QString &title, const QString &text,
                                                              QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton, int timeout)
{
    return QAutoCloseMessageBox::showNewMessageBox(parent, QMessageBox::Information, title, text, buttons, defaultButton, timeout);
}

QMessageBox::StandardButton QAutoCloseMessageBox::question(QWidget *parent, const QString &title, const QString &text,
                                                           QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton, int timeout)
{
    return QAutoCloseMessageBox::showNewMessageBox(parent, QMessageBox::Question, title, text, buttons, defaultButton, timeout);
}

QMessageBox::StandardButton QAutoCloseMessageBox::warning(QWidget *parent, const QString &title, const QString &text,
                                                          QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton, int timeout)
{
    return QAutoCloseMessageBox::showNewMessageBox(parent, QMessageBox::Warning, title, text, buttons, defaultButton, timeout);
}

/*
 * Copy-paste from Qt sources (qmessagebox.cpp -> QMessageBoxPrivate::showNewMessageBox
 */
QMessageBox::StandardButton QAutoCloseMessageBox::showNewMessageBox(QWidget *parent, QMessageBox::Icon icon, const QString &title, const QString &text, QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton, int timeout)
{
    QAutoCloseMessageBox msgBox(icon, title, text, QMessageBox::NoButton, parent);
    QDialogButtonBox *buttonBox = msgBox.findChild<QDialogButtonBox*>();
    Q_ASSERT(buttonBox != 0);

    uint mask = QMessageBox::FirstButton;
    while (mask <= QMessageBox::LastButton) {
        uint sb = buttons & mask;
        mask <<= 1;
        if (!sb)
            continue;
        QPushButton *button = msgBox.addButton((QMessageBox::StandardButton)sb);
        // Choose the first accept role as the default
        if (msgBox.defaultButton())
            continue;
        if ((defaultButton == QMessageBox::NoButton && buttonBox->buttonRole(button) == QDialogButtonBox::AcceptRole)
            || (defaultButton != QMessageBox::NoButton && sb == uint(defaultButton)))
            msgBox.setDefaultButton(button);
    }
    msgBox.setTimeout(timeout);
    if (msgBox.exec() == -1)
        return QMessageBox::Cancel;
    return msgBox.standardButton(msgBox.clickedButton());
}

/*****************************************************/

QAutoCloseMessageBox::QAutoCloseMessageBox(QWidget *parent) : QMessageBox(parent), qam_timer(this), qam_timeout(0), qam_ticker(0)
{
    qam_timer.setInterval(1000);
    qam_timer.setSingleShot(false);
    connect(&qam_timer, SIGNAL(timeout()),this, SLOT(d_timeout()));
    connect(this, SIGNAL(finished(int)),this, SLOT(d_finished(int)));
}

QAutoCloseMessageBox::QAutoCloseMessageBox(QMessageBox::Icon icon, const QString &title, const QString &text, QMessageBox::StandardButtons buttons, QWidget *parent, Qt::WindowFlags f) : QMessageBox(icon, title, text, buttons, parent, f), qam_timer(this), qam_timeout(0), qam_ticker(0)
{
    qam_timer.setInterval(1000);
    qam_timer.setSingleShot(false);
    connect(&qam_timer, SIGNAL(timeout()),this, SLOT(d_timeout()));
    connect(this, SIGNAL(finished(int)),this, SLOT(d_finished(int)));
}

int QAutoCloseMessageBox::exec()
{
    timeoutHelper();
    return QMessageBox::exec();
}

void QAutoCloseMessageBox::show()
{
    timeoutHelper();
    QMessageBox::show();
}

void QAutoCloseMessageBox::d_timeout()
{
    if(qam_timeout<=0)
    {
        return;
    }
    if(--qam_ticker <= 0)
    {
        if(this->defaultButton())
        {
            this->defaultButton()->animateClick(1);
        }
        else
        {
            //no default button?
            qam_timer.stop();
        }
    }
    else
    {
        this->defaultButton()->setText(qam_default_button_text + QString(" (%1)").arg(qam_ticker));
    }
}

void QAutoCloseMessageBox::d_finished(int result)
{
    Q_UNUSED(result);
    qWarning()<<"QACMB finished "<<result;
    qam_timer.stop();
}

void QAutoCloseMessageBox::timeoutHelper()
{
    if(this->defaultButton())
    {
        qWarning()<<"QACMB has the default button: "<<this->defaultButton();
        qam_default_button_text = this->defaultButton()->text();
        if(qam_timeout>0)
        {
            qWarning()<<"QACMB setting autoclose timeout: "<<qam_ticker;
            qam_ticker = qam_timeout;
            this->defaultButton()->setText(qam_default_button_text + QString(" (%1)").arg(qam_ticker));
            qam_timer.start();
        }
        else
        {
            qWarning()<<"QACMB timeout not set / disabled";
        }
    }
    else
    {
        qWarning()<<"QCMB no default button";
    }
}
