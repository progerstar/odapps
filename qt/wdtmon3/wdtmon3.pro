include (distrodefines.pri)

QT += qml quick quickcontrols2 serialport gui widgets

CONFIG += c++11

SOURCES += main.cpp \
    settings.cpp

DEFINES += WITH_QML

RESOURCES += qml.qrc

lupdate_only{
    SOURCES = *.qml
}

DISTFILES += *.qml

include ($$PWD/../qautostarter/qautostarter.pri)

unix:!macx:QMAKE_LFLAGS += '-Wl,-rpath,\'\$$ORIGIN/../lib\',-z,origin'
macx:QMAKE_LFLAGS  = -Wl,-install_name,@executable_path/../Frameworks/ -Wl,-rpath,@executable_path/../Frameworks/

# Additional import path used to resolve QML modules in Qt Creator's code model
QML_IMPORT_PATH =

# Default rules for deployment.
include(deployment.pri)

include($$PWD/utils/pinger/pinger.pri)
include($$PWD/utils/qmlmaterial/qmlmaterial.pri)
include($$PWD/utils/qmlserial/qmlserial.pri)
include($$PWD/utils/qmltoasts/qmltoasts.pri)
include($$PWD/utils/watcher/watcher.pri)


include($$PWD/../qi18n/qi18n.pri)
include($$PWD/../singleapp/singleapp.pri)
include($$PWD/../systemprocess/systemprocess.pri)
include($$PWD/../uptime/uptime.pri)
include($$PWD/../qjournald/qjournald.pri)
include($$PWD/../themedetector/themedetector.pri)
include($$PWD/../qmlsystrayicon/qmlsystrayicon.pri)


HEADERS += settings.h \
           version.h

win32:RC_FILE = wdtmon3.rc
#win32:CONFIG+=console

macx: ICON = wdtmon3.icns

TRANSLATIONS += wdtmon_ru.ts

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
