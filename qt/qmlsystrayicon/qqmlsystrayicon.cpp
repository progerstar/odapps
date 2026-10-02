#include <qqmlsystrayicon.h>

#include <QAction>
#include <QApplication>
#include <QDebug>

#ifdef Q_OS_DARWIN
#include <QMacDockGuard.h>
#endif

QQmlSystrayIcon::QQmlSystrayIcon(QObject *parent) : QSystemTrayIcon(parent), ctxMenu(new QMenu)
#if defined(Q_OS_DARWIN)
  , app_active(true)
#endif
{
    connect(this,SIGNAL(activated(QSystemTrayIcon::ActivationReason)),this,SLOT(m_activated(QSystemTrayIcon::ActivationReason)));
    QSystemTrayIcon::setContextMenu(ctxMenu);

#if defined(Q_OS_DARWIN)
    connect(qApp, &QApplication::applicationStateChanged, this, [=](Qt::ApplicationState s){
        if(s == Qt::ApplicationActive) {
            if(app_active) {
                app_active = false;
            } else {
                emit triggered("@osx_dock");
            }
        }
    });
#endif
}

void QQmlSystrayIcon::addAction(const QString& text, const QString& id)
{
    QAction* newact = new QAction(text,this);
    newact->setData(id);
    connect(newact,SIGNAL(triggered()),this,SLOT(m_act_triggered()));
    ctxMenu->addAction(newact);
}

void QQmlSystrayIcon::addSeparator()
{
    ctxMenu->addSeparator();
}

void QQmlSystrayIcon::setIconResource(const QString &src)
{
    QIcon icn(QStringLiteral(":/")+src);
    QSystemTrayIcon::setIcon(icn);
    iconSource = src;
    emit iconChanged();
}

void QQmlSystrayIcon::m_activated(QSystemTrayIcon::ActivationReason reason)
{
#ifndef Q_OS_DARWIN
    if(reason==QSystemTrayIcon::Trigger)
    {
        emit iconClicked();
    }
#else
    (void)reason;
#endif
}

void QQmlSystrayIcon::m_act_triggered()
{
    QAction* src = qobject_cast<QAction*>(sender());
    if(src && !src->data().toString().isEmpty())
    {
        emit triggered(src->data().toString());
    }
}
