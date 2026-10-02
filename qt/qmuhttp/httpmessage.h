#ifndef HTTPMESSAGE_H
#define HTTPMESSAGE_H

#include <QObject>
#include <QHash>
#include <QByteArray>
#include <http_parser.h>

class HttpMessage : public QObject
{
        Q_OBJECT
    public:
        explicit HttpMessage(QObject *parent = nullptr);
        HttpMessage(const HttpMessage& other);

        HttpMessage& operator=(const HttpMessage& other);

        http_method method;
        QByteArray request_url;
        QHash<QByteArray,QByteArray> headers;
        QByteArray body;
};

Q_DECLARE_METATYPE(HttpMessage)

#endif // HTTPMESSAGE_H
