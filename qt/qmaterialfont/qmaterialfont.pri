isEmpty(MATERIAL_FONT_INCLUDED) {

MATERIAL_FONT_INCLUDED = 1

QT += svg

DEFINES += MATERIAL_VERSION="\\\"v127\\\""

INCLUDEPATH += $$PWD
HEADERS += $$PWD/qmaterialfont.h $$PWD/qsvgiconengine.h
SOURCES += $$PWD/qmaterialfont.cpp $$PWD/qsvgiconengine.cpp

RESOURCES += \
    $$PWD/qmaterialfont.qrc

}
