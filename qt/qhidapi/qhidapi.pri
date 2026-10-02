isEmpty(LD_HIDAPI_INCLUDED){

LD_HIDAPI_INCLUDED = 1

unix:!macx:!android: {
    SOURCES += $$PWD/lin/hid.c
    LIBS += -ludev
    CONFIG += link_pkgconfig
    packagesExist(libusb-1.0) {
        PKGCONFIG += libusb-1.0
    } else {
        error("libusb-1.0 was not found through pkg-config")
    }
}
macx: {
    SOURCES += $$PWD/osx/hid.c
    LIBS += -framework IOKit -framework CoreFoundation
    CONFIG += link_pkgconfig
    packagesExist(libusb-1.0) {
        PKGCONFIG += libusb-1.0
    } else {
        error("libusb-1.0 was not found through pkg-config")
    }
}
win32: {
    SOURCES += $$PWD/win/hid.c
    LIBS +=  -lsetupapi -lhid
    include($$PWD/winusb/winusb.pri)
}

android: {
    QT += androidextras
    DEFINES += HIDPROXY_NO_LIBUSB=1
    include ($$PWD/../qmakeAndroidSourcesHelper/functions.pri)

    isEmpty(ANDROID_PACKAGE_SOURCE_DIR) {
        error("Define ANDROID_PACKAGE_SOURCE_DIR before inclusion")
    }
    isEmpty(ANDROID_APP_UID) {
        error("Define the ANDROID_APP_UID")
    }
    OUTPUT_JAVA_PATH = $$replace(ANDROID_APP_UID, \., /)
    message("Deploying auxiliary java files to src/$${OUTPUT_JAVA_PATH}")
    QMAKE_EXTRA_TARGETS += $$copyAndroidSources("HIDAPI_JAVA_DEVICE", "src/$${OUTPUT_JAVA_PATH}", $$shell_path($$PWD/android/HIDIODevice.java))
    QMAKE_EXTRA_TARGETS += $$copyAndroidSources("HIDAPI_JAVA_MANAGER", "src/$${OUTPUT_JAVA_PATH}", $$shell_path($$PWD/android/HIDIOManager.java))

    DISTFILES += \
        $$PWD/android/HIDIODevice.java \
        $$PWD/android/HIDIOManager.java
}

HEADERS += $$PWD/hidapi.h
INCLUDEPATH += $$PWD

}

HEADERS += $$PWD/hidproxy.h
SOURCES += $$PWD/hidproxy.cpp

android: {
    HEADERS += $$PWD/hidproxy_android.h \
               $$PWD/androidjnihelper.h
    SOURCES += $$PWD/hidproxy_android.cpp \
               $$PWD/androidjnihelper.cpp
} else {
    HEADERS += $$PWD/hidproxy_desktop.h
    SOURCES += $$PWD/hidproxy_desktop.cpp
}
