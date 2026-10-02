#include "singleapp.h"

#include <QCryptographicHash>
#include <QMessageBox>
#include <QFile>

SingleAppHandler::SingleAppHandler(QCoreApplication *a) : QObject(a), app(a), sharedMemory(0), appServer(0)
{

}

SingleAppHandler::~SingleAppHandler()
{
    if(appServer)
    {
        clearClients();
        appServer->close();
        delete appServer;
    }
    if(sharedMemory)
    {
        sharedMemory->detach();
        delete sharedMemory;
    }
}

QString SingleAppHandler::appHash() const
{
    return QString::fromLatin1(QCryptographicHash::hash(app->applicationName().toUtf8(),QCryptographicHash::Sha256)
                               .toHex().toLower());
}

bool SingleAppHandler::isSingle()
{
    QString app_hash = appHash();
    qWarning()<<"Checking shared memory "<<app_hash;

    QSystemSemaphore semaphore(app_hash+"_semaphore", 1);
    semaphore.acquire();

#ifndef Q_OS_WIN32
    //linux shared memory garbage collector
    QSharedMemory nix_fix_shared_memory(app_hash);
    if(nix_fix_shared_memory.attach()){
        nix_fix_shared_memory.detach();
    }
#endif

    sharedMemory = new QSharedMemory(app_hash);
    bool is_running;
    if (sharedMemory->attach())
    {
        qWarning()<<"An instance of "<<app->applicationName()<<" is already running!";
        //already running
        is_running = true;
    }
    else
    {
        //1st instance
        sharedMemory->create(1);
        is_running = false;
    }
    semaphore.release();

    if(is_running)
    {
        return false;
    }

    appServer = new QLocalServer(this);
    connect(appServer,&QLocalServer::newConnection,this,&SingleAppHandler::processConnection);
#ifdef Q_OS_UNIX
    QFile("/tmp/"+app_hash).remove();
    appServer->listen("/tmp/"+app_hash);
#else
    appServer->listen(app_hash);
#endif
    return true;
}

void SingleAppHandler::sendMessage(const QString &msg)
{
    QLocalSocket sock;
#ifdef Q_OS_UNIX
    sock.connectToServer("/tmp/"+appHash());
#else
    sock.connectToServer(appHash());
#endif
    if(!sock.waitForConnected(500))
    {
        qWarning()<<"Cannot connect to app server - is it running?";
        return;
    }
    sock.write(QString(msg+"\n").toUtf8());
    sock.flush();
    sock.close();
    qWarning()<<"Sent "<<msg<<" to other instance";
}

void SingleAppHandler::processConnection()
{
    while(appServer->hasPendingConnections ())
    {
        QLocalSocket *client = appServer->nextPendingConnection();
        connect(client, SIGNAL(disconnected()), this, SLOT(processDisconnected()));
        connect(client,SIGNAL(readyRead()),this,SLOT(clientReadyRead()));
        appClients.insert(client,QString());
    }
}

void SingleAppHandler::processDisconnected()
{
    QLocalSocket *client = qobject_cast<QLocalSocket*>(sender());

    if (!client)
    {
        return;
    }

    appClients.remove(client);
    client->deleteLater();
}

void SingleAppHandler::clientReadyRead()
{
    QLocalSocket *client = qobject_cast<QLocalSocket*>(sender());

    if (!client)
    {
        return;
    }

    QString& clientBuffer = appClients[client];
    clientBuffer+= QString::fromUtf8(client->readAll());
    int nl_index;
    QString msg;
    while(clientBuffer.contains("\n\n")) clientBuffer.replace("\n\n","\n");
    while((nl_index = clientBuffer.indexOf('\n'))>0)
    {
        msg = clientBuffer.left(nl_index);
        clientBuffer = clientBuffer.mid(nl_index+1);
        //qWarning()<<"AppServer got "<<msg;
        emit incomingMessage(msg);
    }
}


void SingleAppHandler::clearClients()
{
    QList<QLocalSocket*> clients = appClients.keys();
    foreach(QLocalSocket* client, clients)
    {
        if(client)
        {
            if(client->isValid ())
            {
                client->close ();
            }
            client->deleteLater();
        }
    }

    appClients.clear();
}
