#include "httpclient.h"

namespace {
constexpr int RequestTimeoutMs = 15000;
}

/**********http callbacks********************/
int message_begin_cb(http_parser* parser)
{
    if(!parser) return 1;

    HttpClient* client = (HttpClient*)parser->data;
    client->request_url.clear();
    client->headers.clear();
    client->current_header.clear();
    client->body.clear();
    client->headers_complete = false;
    client->message_complete = false;
    client->filling_header = false;
    return 0;
}

int message_complete_cb(http_parser* parser)
{
    if(!parser) return 1;

    ((HttpClient*)parser->data)->message_complete = true;
    return 0;
}

int headers_complete_cb(http_parser* parser)
{
    if(!parser) return 1;

    HttpClient* client = (HttpClient*)parser->data;
    client->method = parser->method;
    client->status_code = parser->status_code;
    client->headers_complete = true;
    client->keep_alive = http_should_keep_alive(parser) ? true : false;
    return 0;
}

int url_cb(http_parser* parser, const char* buf, size_t len)
{
    ((HttpClient*)parser->data)->request_url.append(QByteArray(buf,len));
    return 0;
}

int header_field_cb(http_parser* parser, const char* buf, size_t len)
{
    HttpClient* client = (HttpClient*)parser->data;
    if(client->filling_header)
    {
        client->current_header.append(QByteArray(buf, len).toLower());
    }
    else
    {
        client->current_header = QByteArray(buf, len).toLower();
        client->filling_header = true;
    }
    return 0;
}

int header_value_cb(http_parser* parser, const char* buf, size_t len)
{
    HttpClient* client = (HttpClient*)parser->data;
    if(!client->current_header.isEmpty())
    {
        if(client->filling_header)
        {
            client->headers.insert(client->current_header, QByteArray());
        }
        client->filling_header = false;
        client->headers[client->current_header].append(buf, len);
    }
    return 0;
}

int body_cb(http_parser* parser, const char* buf, size_t len)
{
    ((HttpClient*)parser->data)->body.append(QByteArray(buf,len));
    return 0;
}

static http_parser_settings parser_settings =
{
    .on_message_begin = message_begin_cb,
    .on_url = url_cb,
    .on_status = NULL,
    .on_header_field = header_field_cb,
    .on_header_value = header_value_cb,
    .on_headers_complete = headers_complete_cb,
    .on_body = body_cb,
    .on_message_complete = message_complete_cb,
    .on_chunk_header = NULL,
    .on_chunk_complete = NULL,
};

/********************************************/

HttpClient::HttpClient(QTcpSocket *sock, QObject *parent): QObject(parent), fd(sock),
    method(HTTP_GET), status_code(200),headers_complete(false),keep_alive(false),parsing_eof(false),
    message_complete(false),filling_header(false), request_bytes(0), requestTimer()
{
    parser = new http_parser;
    http_parser_init(parser,HTTP_REQUEST);
    parser->data = this;
    connect(sock,SIGNAL(readyRead()),this,SLOT(readyRead()));
    connect(sock,SIGNAL(disconnected()),this,SLOT(sockClosed()));
    requestTimer.setSingleShot(true);
    requestTimer.setInterval(RequestTimeoutMs);
    connect(&requestTimer, &QTimer::timeout, this, [this]() {
        if(!fd || fd->state() == QAbstractSocket::UnconnectedState) {
            return;
        }
        message_complete = true;
        fd->write("HTTP/1.1 408 Request Timeout\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
        fd->disconnectFromHost();
    });
    requestTimer.start();
}

HttpClient::~HttpClient()
{
    if(parser)
    {
        parser->data = 0;
        delete parser;
    }
    if(fd)
    {
        disconnect(fd);
        if(fd->state() != QAbstractSocket::UnconnectedState)
        {
            fd->abort();
        }
        delete fd;
        fd = 0;
    }
}

void HttpClient::readyRead()
{
    if(message_complete) return;

    QByteArray data = fd->readAll();
    request_bytes += data.size();
    if(request_bytes > 64 * 1024)
    {
        message_complete = true;
        requestTimer.stop();
        fd->write("HTTP/1.1 413 Payload Too Large\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
        fd->disconnectFromHost();
        return;
    }
    qDebug()<<"Read "<<data.size()<<" bytes from socket "<<fd->socketDescriptor();
    parsing_eof = (data.length()==0);
    const size_t parsed = http_parser_execute(parser,&parser_settings,data.constData(),data.size());
    if(parsed != size_t(data.size()))
    {
        message_complete = true;
        requestTimer.stop();
        qWarning()<<"Invalid HTTP request: "<<http_errno_description(HTTP_PARSER_ERRNO(parser));
        fd->write("HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
        fd->disconnectFromHost();
        return;
    }
    if(message_complete)
    {
        requestTimer.stop();
        emit message();
    }
}

void HttpClient::replied()
{
    headers_complete = false;
    parsing_eof = false;
    message_complete = false;
    filling_header = false;
    request_url.clear();
    headers.clear();
    current_header.clear();
    body.clear();
    request_bytes = 0;
    if(!keep_alive)
    {
        requestTimer.stop();
        fd->disconnectFromHost();
    }
    else
    {
        http_parser_init(parser, HTTP_REQUEST);
        parser->data = this;
        requestTimer.start();
    }
}

void HttpClient::sockClosed()
{
    disconnect(fd);
    emit closing(fd->socketDescriptor());
}
