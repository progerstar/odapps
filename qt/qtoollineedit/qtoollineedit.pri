INCLUDEPATH += $$PWD
HEADERS += $$PWD/qtoollineedit.h
SOURCES += $$PWD/qtoollineedit.cpp

isEmpty(QTOOLLINEEDIT_MATERIAL) {
    RESOURCES += $$PWD/qtoollineedit.qrc
} else {
    DEFINES += QTOOLLINEEDIT_MATERIAL_ICONS=1
}
