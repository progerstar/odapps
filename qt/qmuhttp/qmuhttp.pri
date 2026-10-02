INCLUDEPATH += $$PWD

QT += network

HEADERS += $$PWD/qmuhttp.h \
    $$PWD/httpclient.h \
    $$PWD/httpmessage.h
SOURCES += $$PWD/qmuhttp.cpp \
    $$PWD/httpclient.cpp \
    $$PWD/httpmessage.cpp

INCLUDEPATH += $$PWD/http-parser

HEADERS += $$PWD/http-parser/http_parser.h
SOURCES += $$PWD/http-parser/http_parser.c
