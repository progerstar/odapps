#ifndef QQMLSYSTRAYICON_H_
#define QQMLSYSTRAYICON_H_

#include <QSystemTrayIcon>
#include <QMenu>
#include <QtQml>

class QQmlSystrayIcon : public QSystemTrayIcon
{
        Q_OBJECT
        Q_PROPERTY(QString iconSource READ iconResource WRITE setIconResource NOTIFY iconChanged)
        Q_PROPERTY(bool available READ isSystemTrayAvailable)
    public:

        static inline void declareQML()
        {
            qmlRegisterType<QQmlSystrayIcon>("ru.opendev.qmlsystray",1,0, "QmlSystray");
        }

        QQmlSystrayIcon(QObject* parent = 0);
        virtual ~QQmlSystrayIcon() {delete ctxMenu;}

        Q_INVOKABLE void addAction(const QString& text, const QString& id);
        Q_INVOKABLE void addSeparator();
        Q_INVOKABLE QString iconResource() const { return iconSource; }
    public slots:
        void setIconResource(const QString& src);
    signals:
        void triggered(const QString& id);
        void iconClicked();
        void iconChanged();
    private slots:
        void m_activated(QSystemTrayIcon::ActivationReason reason);
        void m_act_triggered();

    private:
        QMenu* ctxMenu;
        QString iconSource;
#if defined(Q_OS_DARWIN)
        bool app_active;
#endif
};

#endif  // QQMLSYSTRAYICON_H_
