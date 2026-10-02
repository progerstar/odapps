#ifndef HTTPCLIENT_H
#define HTTPCLIENT_H

#include <QObject>
#include <QHash>
#include <QByteArray>
#include <QTcpSocket>
#include <QTimer>

#include <http_parser.h>

class HttpClient : public QObject
{
        Q_OBJECT
    public:
        explicit HttpClient(QTcpSocket* sock, QObject* parent = nullptr);
        ~HttpClient();

        QTcpSocket* fd;
        http_parser* parser;

        uint method;
        int status_code;

        bool headers_complete;
        bool keep_alive;
        bool parsing_eof;
        bool message_complete;

        QByteArray request_url;
        QHash<QByteArray,QByteArray> headers;
        bool filling_header;
        QByteArray current_header;
        QByteArray body;
        qint64 request_bytes;
    signals:
        void closing(int id);
        void message();
    public slots:
        void replied();
    private slots:
        void readyRead();
        void sockClosed();
    private:
        QTimer requestTimer;
};

#endif // HTTPCLIENT_H
