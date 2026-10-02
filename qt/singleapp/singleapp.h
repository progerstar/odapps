#ifndef Q_SINGLEAPP_H_
#define Q_SINGLEAPP_H_

#include <QSharedMemory>
#include <QSystemSemaphore>
#include <QCoreApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QDebug>
#include <QHash>

class SingleAppHandler : public QObject
{
        Q_OBJECT
    public:
        explicit SingleAppHandler(QCoreApplication* a);
        ~SingleAppHandler();
        QString appHash() const;

        bool isSingle();
        void sendMessage(const QString& msg);
    signals:
        void incomingMessage(const QString& msg);
    private slots:
        void processConnection();
        void processDisconnected();
        void clientReadyRead();
    private:
        QCoreApplication* app;
        QSharedMemory* sharedMemory;
        QLocalServer*  appServer;
        QHash<QLocalSocket*,QString> appClients;


        void clearClients();
};

#endif  // Q_SINGLEAPP_H_
