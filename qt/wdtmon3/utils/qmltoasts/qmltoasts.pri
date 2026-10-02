isEmpty(QMLTOASTS_PRI_INCLUDED) {

QMLTOASTS_PRI_INCLUDED=1

INCLUDEPATH += $$PWD

lupdate_only{
    SOURCES += $$PWD/Toast.qml \
               $$PWD/ToastManager.qml
}

RESOURCES += $$PWD/qmltoasts.qrc

}
