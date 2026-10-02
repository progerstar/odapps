include (distrodefines.pri)
QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TEMPLATE = app
CONFIG += c++11
win32 {
    TARGET = wdtmon3mini
} else {
    TARGET = wdtmon3-mini
}
macx:CONFIG+=app_bundle

win32:RC_FILE = wdtmon3_lite.rc
macx: ICON = wdtmon3_lite.icns

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
        main.cpp \
        wdtmonlite.cpp \
        logsetupdialog.cpp

win32:HEADERS += version_win.h

HEADERS += \
        wdtmonlite.h \
        logsetupdialog.h

FORMS += \
        wdtmonlite.ui \
        logsetupdialog.ui

HEADERS += httpsetupdialog.h
SOURCES += httpsetupdialog.cpp
FORMS +=   httpsetupdialog.ui

RESOURCES += \
    wdtmon3_lite.qrc

include ($$PWD/qi18n/qi18n.pri)
include ($$PWD/endianrw/endianrw.pri)
include ($$PWD/../libhiddfu/libhiddfu.pri)
include ($$PWD/../qmaterialfont/qmaterialfont.pri)
include ($$PWD/../qhidapi/qhidapi.pri)
include ($$PWD/../qpathparser/qpathparser.pri)
include ($$PWD/../themedetector/themedetector.pri)
include ($$PWD/../odwidgetstyle/odwidgetstyle.pri)
include ($$PWD/../singleapp/singleapp.pri)
include ($$PWD/../uptime/uptime.pri)
include ($$PWD/../qjournald/qjournald.pri)
include ($$PWD/../qautostarter/qautostarter.pri)
include ($$PWD/../qmuhttp/qmuhttp.pri)
include ($$PWD/../qautoclosemessagebox/qautoclosemessagebox.pri)
include ($$PWD/../RS485/RS485.pri)

unix:!macx:QMAKE_LFLAGS += '-Wl,-rpath,\'\$$ORIGIN/../lib\',-z,origin'
macx:QMAKE_LFLAGS  = -Wl,-install_name,@executable_path/../Frameworks/ -Wl,-rpath,@executable_path/../Frameworks/

TRANSLATIONS += wdtmon3_lite_ru.ts

unix:!macx:DESTDIR = $$OUT_PWD/bin

win32{
    OBJECTS_DIR = $$OUT_PWD/.win_obj
    MOC_DIR = $$OUT_PWD/.win_moc
    RCC_DIR = $$OUT_PWD/.win_rcc
}

macx{
    OBJECTS_DIR = $$OUT_PWD/.mac_obj
    MOC_DIR = $$OUT_PWD/.mac_moc
}

unix:!macx{
    OBJECTS_DIR = $$OUT_PWD/.unix_obj
    MOC_DIR = $$OUT_PWD/.unix_moc
}

DISTFILES +=  \
    version_win.h
