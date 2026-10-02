TEMPLATE = app
CONFIG += testcase c++11
QT += core testlib

INCLUDEPATH += $$PWD/../../mifare

SOURCES += \
    tst_lfmemoryprotocol.cpp \
    $$PWD/../../mifare/lfmemoryprotocol.cpp
