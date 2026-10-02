#ifndef Q_MU_HTTP_H_
#define Q_MU_HTTP_H_

#include <QtNetwork>
#include <QTcpSocket>
#include <QObject>
#include <QByteArray>
#include <QUrl>

#include "httpclient.h"

#ifndef SETTINGS_HTTP_PORT
#define SETTINGS_HTTP_PORT "HTTP/Port"
#endif

#ifndef SETTINGS_HTTP_PORT_DEFAULT
#define SETTINGS_HTTP_PORT_DEFAULT 34242
#endif

#ifndef SETTINGS_HTTP_REMOTE_ACCESS
#define SETTINGS_HTTP_REMOTE_ACCESS "HTTP/RemoteAccess"
#endif

class QTcpServer;

class QMUHttpServer : public QObject
{
        Q_OBJECT
    public:
        explicit QMUHttpServer(QObject* parent = NULL);
        ~QMUHttpServer();

        bool isRunning() const;
        inline int clients() const {
            return http ? connections.size() : 0;
        }
    signals:
        void newMessage(HttpClient* client);
    public slots:
        bool start();
        void stop();
    private slots:
        void newConnection();
        void clientClosing(int id);
        void clientMessage();
    private:
        QTcpServer* http;
        QMap<int,HttpClient*> connections;
};

#endif  // Q_MU_HTTP_H_
