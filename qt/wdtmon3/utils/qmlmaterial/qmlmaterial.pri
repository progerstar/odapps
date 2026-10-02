isEmpty(QMLMATERIAL_PRI_INCLUDED) {

QMLMATERIAL_PRI_INCLUDED=1

INCLUDEPATH += $$PWD
lupdate_only{
    SOURCES += $$PWD/*.qml
}

DISTFILES += \
    $$PWD/*.qml

RESOURCES += $$PWD/qmlmaterial.qrc

}

DISTFILES += \
    $$PWD/MaterialCard.qml \
    $$PWD/MaterialSlider.qml


