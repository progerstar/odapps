include ($$PWD/../commondefines.pri)

QT       += core gui widgets serialport svg

qtHaveModule(serialbus) {
    QT += serialbus
} else:unix:!macx {
    qt5SerialBusRoot = $$(QT5_SERIALBUS_ROOT)
    isEmpty(qt5SerialBusRoot):error("Qt 5 Serial Bus was not found. Run scripts/bootstrap-qt5-serialbus-linux.sh first.")
    !exists($${qt5SerialBusRoot}/include/QtSerialBus/QModbusDevice):error("Invalid QT5_SERIALBUS_ROOT: missing QtSerialBus headers")
    !exists($${qt5SerialBusRoot}/lib/libQt5SerialBus.so):error("Invalid QT5_SERIALBUS_ROOT: missing libQt5SerialBus.so")

    INCLUDEPATH += $${qt5SerialBusRoot}/include \
                   $${qt5SerialBusRoot}/include/QtSerialBus
    LIBS += -L$${qt5SerialBusRoot}/lib -lQt5SerialBus
    DEFINES += QT_SERIALBUS_LIB
} else {
    error("Qt 5 Serial Bus is required to build odrfidconfigM")
}

macx{
    TARGET = ODRFIDConfigM
    CONFIG += app_bundle
} else {
    TARGET = odrfidconfigM
}
TEMPLATE = app

ANDROID_APP_UID = "ru.opendev.odrfidconfigm"
DEFINES += QT_DEPRECATED_WARNINGS

INCLUDEPATH += $$PWD/../mifare/

SOURCES += \
        logwindow.cpp \
        main.cpp \
        odrfidconfig.cpp \
        $$PWD/../mifare/qhexlineedit.cpp \
        commandconsoledialog.cpp

HEADERS += \
        logwindow.h \
        odrfidconfig.h \
        $$PWD/../mifare/qhexlineedit.h \
        commandconsoledialog.h

FORMS += \
        logwindow.ui \
        odrfidconfig.ui \
        commandconsoledialog.ui

TRANSLATIONS += odrfidconfigM_ru.ts


include ($$PWD/../endianrw/endianrw.pri)
include ($$PWD/../app_binary.pri)
include ($$PWD/../RS485/RS485.pri)
include ($$PWD/../qmaterialfont/qmaterialfont.pri)
include ($$PWD/../themedetector/themedetector.pri)
include ($$PWD/../qi18n/qi18n.pri)
include ($$PWD/../qautoclosemessagebox/qautoclosemessagebox.pri)

win32:DISTFILES += version_win.h

RESOURCES += \
    odrfidcfgM.qrc
