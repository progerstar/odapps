isEmpty(RS_485_INCLUDED) {

RS_485_INCLUDED=1

INCLUDEPATH += $$PWD
HEADERS     += $$PWD/uartcommunicator.h $$PWD/qcomport.h
SOURCES     += $$PWD/uartcommunicator.cpp
QT += serialport

}

FORMS += \
    $$PWD/rs485setupdialog.ui

HEADERS += \
    $$PWD/rs485setupdialog.h

SOURCES += \
    $$PWD/rs485setupdialog.cpp
