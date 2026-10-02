#include "qmuhttp.h"

#include <QTcpServer>
#include <QSettings>
#include <QTextStream>

#include <QDebug>

namespace {
constexpr int MaxHttpClients = 64;
}

QMUHttpServer::QMUHttpServer(QObject *parent) : QObject(parent), http(0)
{
#if 0
    static int init = 0;
    if(!init){
        (void)qRegisterMetaType<HttpMessage>();
        init =1;
    }
#endif
}

QMUHttpServer::~QMUHttpServer()
{
    stop();
}

bool QMUHttpServer::isRunning() const
{
    return http ? true : false;
}

bool QMUHttpServer::start()
{
    if(http) stop();

    QSettings set;
    http = new QTcpServer(this);
    connect(http, SIGNAL(newConnection()), this, SLOT(newConnection()));
    const QHostAddress listenAddress = set.value(SETTINGS_HTTP_REMOTE_ACCESS, false).toBool()
            ? QHostAddress::Any : QHostAddress::LocalHost;
    if (!http->listen(listenAddress, set.value(SETTINGS_HTTP_PORT,SETTINGS_HTTP_PORT_DEFAULT).toUInt()))
    {
        qWarning()<<tr("Unable to start the server at %1: %2")
                    .arg(set.value(SETTINGS_HTTP_PORT,SETTINGS_HTTP_PORT_DEFAULT).toUInt())
                    .arg(http->errorString());
        delete http;
        http = 0;
        return false;
    }

    qDebug()<<"HTTP server listening on "<<http->serverAddress()<<":"<<http->serverPort();
    return true;
}

void QMUHttpServer::stop()
{
    if(http)
    {
        QList<int> cids = connections.keys();
        foreach(int id,cids)
        {
            /*
             * MUST close NOW - not 'later'
             */
            delete connections.take(id);
        }
        http->close();
        delete http;
        http=0;
    }
}

void QMUHttpServer::newConnection()
{
    if(!http) return;

    while(http->hasPendingConnections())
    {
        QTcpSocket* clientSocket = http->nextPendingConnection();
        if(!clientSocket)
        {
            break;
        }
        if(connections.size() >= MaxHttpClients)
        {
            qWarning()<<"Rejecting HTTP client: client limit reached";
            clientSocket->disconnectFromHost();
            clientSocket->deleteLater();
            continue;
        }
        const int id = clientSocket->socketDescriptor();
        qDebug()<<"Connected new client with ID "<<id;
        HttpClient* client = new HttpClient(clientSocket, this);
        connections.insert(id, client);
        connect(client,SIGNAL(closing(int)),this, SLOT(clientClosing(int)));
        connect(client,SIGNAL(message()),this, SLOT(clientMessage()));
    }
}

void QMUHttpServer::clientClosing(int id)
{
    HttpClient* client = qobject_cast<HttpClient*>(sender());
    if(client)
    {
        qDebug()<<"Closing client "<<client;
        id = connections.key(client,-1);
        if(id>=0)
        {
            qDebug()<<"Was "<<id;
            connections.remove(id);
            client->deleteLater();
        }
    }
}

void QMUHttpServer::clientMessage()
{
    HttpClient* client = qobject_cast<HttpClient*>(sender());
    if(!client) return;
    {
        qDebug()<<"New message from client "<<client->fd->socketDescriptor();
        QHashIterator<QByteArray,QByteArray> it(client->headers);
        qDebug()<<"URL: "<<QString::fromUtf8(client->request_url);
#if 0
        qDebug()<<"Headers:";
        while(it.hasNext())
        {
            it.next();
            qWarning()<<QString::fromUtf8(it.key())<<": "<<QString::fromUtf8(it.value());
        }
        qWarning()<<"BODY: "<<QString::fromUtf8(client->body);
#endif
    }
    emit newMessage(client);
}
