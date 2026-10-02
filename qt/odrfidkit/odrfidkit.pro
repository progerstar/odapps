include ($$PWD/../commondefines.pri)
include ($$PWD/../mifare/mifare.pri)
include ($$PWD/../qaux/qaux.pri)
include ($$PWD/../qtoollineedit/qtoollineedit.pri)

QT       +=  svg network

macx{
    TARGET = OdRFIDKit
    CONFIG += app_bundle
} else {
    TARGET = odrfidkit
}

ANDROID_APP_UID = "ru.opendev.odrfidkit"

TEMPLATE = app

SOURCES += main.cpp\
           connectionpopup.cpp \
           emclonedialog.cpp \
           lfmemorydialog.cpp \
           qserialportselectdialog.cpp \
           rfidkitwindow.cpp \
           qrfidpreferences.cpp \
           rfidautomodedialog.cpp \
           uidchangedialog.cpp

HEADERS += rfidkitwindow.h \
           connectionpopup.h \
           emclonedialog.h \
           lfmemorydialog.h \
           qserialportselectdialog.h \
           rfidkit_global.h \
           qrfidpreferences.h \
           rfidautomodedialog.h \
           uidchangedialog.h

FORMS    += rfidkitwindow.ui \
            connectionpopup.ui \
            emclonedialog.ui \
            qrfidpreferences.ui \
            qserialportselectdialog.ui \
            rfidautomodedialog.ui \
            uidchangedialog.ui

TRANSLATIONS += odrfidkit_ru.ts

include ($$PWD/../endianrw/endianrw.pri)
isEmpty(DISTRO_ANDROID){
    include ($$PWD/../libhiddfu/libhiddfu.pri)
    include ($$PWD/../qhidapi/qhidapi.pri)
    include ($$PWD/../RS485/RS485.pri)
    include ($$PWD/../themedetector/themedetector.pri)
    include ($$PWD/../qpathparser/qpathparser.pri)
}
include ($$PWD/../qi18n/qi18n.pri)
include ($$PWD/../qslideswitch/qslideswitch.pri)
include ($$PWD/../qmaterialfont/qmaterialfont.pri)
include ($$PWD/../qautoclosemessagebox/qautoclosemessagebox.pri)
include ($$PWD/../app_binary.pri)

RESOURCES += qrfidkit.qrc

win32:DISTFILES += version_win.h

DISTFILES += \
    android/AndroidManifest.xml \
    android/gradle/wrapper/gradle-wrapper.jar \
    android/gradlew \
    android/res/values/libs.xml \
    android/src/HIDIOManager.java \
    android/src/RFIDKitActivity.java \
    android/build.gradle \
    android/gradle/wrapper/gradle-wrapper.properties \
    android/gradlew.bat

!isEmpty(DISTRO_ANDROID) {
    ANDROID_PACKAGE_SOURCE_DIR = $$PWD/android
    QT += androidextras
}
