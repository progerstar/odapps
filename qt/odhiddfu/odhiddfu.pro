include ($$PWD/commondefines.pri)
QT       += core gui widgets

macx {
    TARGET = ODHidDFU
} else {
    TARGET = odhiddfu
}
TEMPLATE = app

ANDROID_APP_UID = "ru.opendev.odhiddfu"

DEFINES += QT_DEPRECATED_WARNINGS
unix:!macx:!android{
    DEFINES += DFU_BULK_TRANSFERS
    LIBS += -lusb-1.0
    QMAKE_LFLAGS += '-Wl,-rpath,\'\$$ORIGIN\',-z,origin'
}
android: {
    # Qt creator 4.10.1 makefile broken:
    # Change 'STRIP = ' to:
    # 'STRIP         = /opt/android-sdk-linux/ndk-bundle/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi-strip'
    DEFINES += DFU_BULK_TRANSFERS
}

SOURCES += \
        main.cpp \
        odhiddfumain.cpp

HEADERS += \
        odhiddfumain.h

FORMS += \
      odhiddfumain.ui

# WARNING: define ANDROID_PACKAGE_SOURCE_DIR before qhidapi.pri)
DISTFILES += \
    android/AndroidManifest.xml \
    android/build.gradle \
    android/gradle/wrapper/gradle-wrapper.jar \
    android/gradle/wrapper/gradle-wrapper.properties \
    android/gradlew \
    android/gradlew.bat \
    android/res/values/libs.xml \
    android/res/xml/device_filter.xml

android:message("Building for $$ANDROID_TARGET_ARCH")

contains(ANDROID_TARGET_ARCH,armeabi-v7a) {
    ANDROID_PACKAGE_SOURCE_DIR = $$PWD/android
}

contains(ANDROID_TARGET_ARCH,arm64-v8a) {
    ANDROID_PACKAGE_SOURCE_DIR = $$PWD/android
}

contains(ANDROID_TARGET_ARCH,x86_64) {
    ANDROID_PACKAGE_SOURCE_DIR = $$PWD/android
}

LIBHIDDFU_MATERIAL_ICONS=1
include ($$PWD/../libhiddfu/libhiddfu.pri)
include ($$PWD/../qmaterialfont/qmaterialfont.pri)
include ($$PWD/../qhidapi/qhidapi.pri)
include ($$PWD/../wdtmon3-mini/qi18n/qi18n.pri)
include ($$PWD/../wdtmon3-mini/endianrw/endianrw.pri)
include ($$PWD/../qautoclosemessagebox/qautoclosemessagebox.pri)
include ($$PWD/../RS485/RS485.pri)
include ($$PWD/../themedetector/themedetector.pri)
include ($$PWD/../odwidgetstyle/odwidgetstyle.pri)
include ($$PWD/../qpathparser/qpathparser.pri)

TRANSLATIONS += odhiddfu_ru.ts

include ($$PWD/app_binary.pri)
win32:DISTFILES += version_win.h

RESOURCES += \
    odhiddfu.qrc

ANDROID_ABIS = armeabi-v7a arm64-v8a
