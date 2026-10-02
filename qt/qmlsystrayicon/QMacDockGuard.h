#ifndef QMACDOCKGUARD_H_
#define QMACDOCKGUARD_H_

#include <QtGlobal>
#include <QObject>

#ifdef Q_OS_DARWIN

class QMacDockGuard : public QObject
{
        Q_OBJECT
    public:
        ~QMacDockGuard(){}

        static QMacDockGuard* instance();
        inline void notifyDockClick() {
            emit dockClicked();
        }

    signals:
        void dockClicked();
    private:
        QMacDockGuard();
};

#endif // Q_OS_DARWIN

#endif  // QMACDOCKGUARD_H_
