#include "httpmessage.h"

HttpMessage::HttpMessage(QObject *parent) : QObject(parent), method(HTTP_GET)
{

}

HttpMessage::HttpMessage(const HttpMessage &other) : QObject(other.parent()),
    method(other.method),request_url(other.request_url),
    headers(other.headers), body(other.body)
{

}

HttpMessage& HttpMessage::operator=(const HttpMessage& other)
{
    if(this != &other)
    {
        method = other.method;
        request_url = other.request_url;
        headers = other.headers;
        body = other.body;
    }
    return *this;
}


