isEmpty(QMLSYSTRAYICON_PRI_INCLUDED) {

QMLSYSTRAYICON_PRI_INCLUDED = 1

INCLUDEPATH += $$PWD

QT += widgets

HEADERS += $$PWD/qqmlsystrayicon.h
SOURCES += $$PWD/qqmlsystrayicon.cpp

# objc_msgSend changed -> handle via QEvent::ApplicationActivate
# HEADERS += $$PWD/QMacDockGuard.h
# SORUCES += $$PWD/QMacDockGuard.cpp

}
