include (distrodefines.pri)

QT += qml quick quickcontrols2 serialport gui widgets svg sql xml

CONFIG += c++11

macx{
    TARGET = IOSenMon
    CONFIG += app_bundle
} else {
    TARGET = iosenmon
}
CONFIG -= console

SOURCES += main.cpp \
    iotrunner.cpp \
    hidsensorinterface.cpp \
    ds18b20sensor.cpp \
    sensorlistmodel.cpp \
    loglistmodel.cpp \
    hdc1080sensor.cpp \
    dhtsensor.cpp

DEFINES += WITH_QML

RESOURCES += qml.qrc

lupdate_only{
    SOURCES = *.qml
}

include ($$PWD/endianrw/endianrw.pri)

include ($$PWD/../qi18n/qi18n.pri)
include ($$PWD/../qhidapi/qhidapi.pri)
include ($$PWD/../singleapp/singleapp.pri)
include ($$PWD/../uptime/uptime.pri)
include ($$PWD/../qautostarter/qautostarter.pri)
include ($$PWD/../qjournald/qjournald.pri)
include ($$PWD/../qmuhttp/qmuhttp.pri)
include ($$PWD/../qmlsettings/qmlsettings.pri)
include ($$PWD/../qmlsystrayicon/qmlsystrayicon.pri)

unix:!macx:QMAKE_LFLAGS += '-Wl,-rpath,\'\$$ORIGIN/../lib\',-z,origin'
macx:QMAKE_LFLAGS  = -Wl,-install_name,@executable_path/../Frameworks/ -Wl,-rpath,@executable_path/../Frameworks/

# Additional import path used to resolve QML modules in Qt Creator's code model
QML_IMPORT_PATH =

# Default rules for deployment.
include(deployment.pri)

HEADERS += \
    iotrunner.h \
    hidsensorinterface.h \
    ds18b20sensor.h \
    sensorlistmodel.h \
    loglistmodel.h \
    iosenmon_global.h \
    hdc1080sensor.h \
    dhtsensor.h

win32:RC_FILE = iosenmon.rc
#win32:CONFIG+=console

macx: ICON = iosenmon.icns

TRANSLATIONS += iosenmon_ru.ts

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

win32:DISTFILES += version_win.h

isEmpty(OD_DEBUG):!isEmpty($(STRIP)) {
    isEmpty(QMAKE_POST_LINK) {
        QMAKE_POST_LINK = $(STRIP) $(TARGET)
    } else {
        QMAKE_POST_LINK += && $(STRIP) $(TARGET)
    }
    message("stripping with $(STRIP) utility")
}
