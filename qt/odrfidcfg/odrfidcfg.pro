include ($$PWD/../commondefines.pri)

QT       += core gui widgets serialport

macx{
    TARGET = ODRFIDConfig
    CONFIG += app_bundle
} else {
    TARGET = odrfidconfig
}
TEMPLATE = app

DEFINES += QT_DEPRECATED_WARNINGS

INCLUDEPATH += $$PWD/../mifare/ \
               $$PWD/../qaux/

SOURCES += \
        main.cpp \
        odrfidconfig.cpp \
        $$PWD/../mifare/qhexlineedit.cpp \
        $$PWD/../mifare/mifare_global.cpp \
        $$PWD/../mifare/lfmemoryprotocol.cpp \
        $$PWD/../mifare/rfidfirmwareversion.cpp \
        $$PWD/../mifare/rfidcardreaderinterface.cpp \
        $$PWD/../mifare/mifareclassicsector.cpp

HEADERS += \
        odrfidconfig.h \
        $$PWD/../qaux/odrfidstyle.h \
        $$PWD/../mifare/qhexlineedit.h \
        $$PWD/../mifare/mifare_global.h \
        $$PWD/../mifare/lfmemoryprotocol.h \
        $$PWD/../mifare/rfidcardreaderinterface.h \
        $$PWD/../mifare/mifaresector.h

FORMS += \
        odrfidconfig.ui

TRANSLATIONS += odrfidcfg_ru.ts

include ($$PWD/../endianrw/endianrw.pri)
include ($$PWD/../qhidapi/qhidapi.pri)
include ($$PWD/../qmaterialfont/qmaterialfont.pri)
include ($$PWD/../themedetector/themedetector.pri)
include ($$PWD/../wdtmon3-mini/qi18n/qi18n.pri)
include ($$PWD/../app_binary.pri)

win32:DISTFILES += version_win.h odrfidconfig.rc

RESOURCES += \
    odrfidcfg.qrc
