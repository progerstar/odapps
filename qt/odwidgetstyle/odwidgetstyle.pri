isEmpty(ODWIDGETSTYLE_PRI_INCLUDED) {
    ODWIDGETSTYLE_PRI_INCLUDED = 1

    QT += widgets
    INCLUDEPATH += $$PWD

    HEADERS += $$PWD/odwidgetstyle.h
    SOURCES += $$PWD/odwidgetstyle.cpp
    RESOURCES += $$PWD/odwidgetstyle.qrc
    DISTFILES += $$PWD/opendev.qss
}
