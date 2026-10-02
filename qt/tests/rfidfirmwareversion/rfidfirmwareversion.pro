TEMPLATE = app
CONFIG += testcase c++11
QT += core testlib

INCLUDEPATH += \
    $$PWD/../../mifare \
    $$PWD/../../endianrw

SOURCES += \
    tst_rfidfirmwareversion.cpp \
    $$PWD/../../mifare/rfidfirmwareversion.cpp
