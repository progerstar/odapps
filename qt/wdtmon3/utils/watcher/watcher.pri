isEmpty(QWATCHER_PRI_INCLUDED) {

QWATCHER_PRI_INCLUDED=1

QT += concurrent

INCLUDEPATH += $$PWD

HEADERS += $$PWD/watcher.hpp

SOURCES += $$PWD/watcher.cpp
}
