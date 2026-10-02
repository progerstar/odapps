isEmpty(LIBHIDDFU_INCLUDED) {
LIBHIDDFU_INCLUDED=1

QT += svg
INCLUDEPATH += $$PWD

HEADERS += \
    $$PWD/hiddfu.h \
    $$PWD/odhid_global.h \
    $$PWD/hiddfu_runner.h \
    $$PWD/droparea.h \
    $$PWD/hiddfu_uart_worker.h \
    $$PWD/hiddfu_usb_worker.h

SOURCES += \
    $$PWD/hiddfu.cpp \
    $$PWD/odhid_global.cpp \
    $$PWD/hiddfu_runner.cpp \
    $$PWD/droparea.cpp \
    $$PWD/hiddfu_uart_worker.cpp \
    $$PWD/hiddfu_usb_worker.cpp

FORMS += $$PWD/hiddfu.ui

!isEmpty(LIBHIDDFU_MATERIAL_ICONS){
    DEFINES += HIDDFU_USE_MATERIAL_ICONS=1
    RESOURCES += $$PWD/libhiddfu_min.qrc
} else {
    RESOURCES += $$PWD/libhiddfu.qrc
}

}

