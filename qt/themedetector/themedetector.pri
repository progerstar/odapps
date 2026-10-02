isEmpty(THEMEDETECTOR_PRI_INCLUDED) {
    THEMEDETECTOR_PRI_INCLUDED = 1
    INCLUDEPATH += $$PWD
    HEADERS += $$PWD/themedetector.h
    SOURCES += $$PWD/themedetector.cpp

    macx{
        LIBS += -framework AppKit
        OBJECTIVE_SOURCES += $$PWD/themedetector_mac_p.mm
    }
}
