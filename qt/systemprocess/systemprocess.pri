INCLUDEPATH += $$PWD

QT += core concurrent

HEADERS += $$PWD/systemprocess.h \
    $$PWD/systemprocesslistmodel.h
SOURCES += $$PWD/systemprocess.cpp \
    $$PWD/systemprocesslistmodel.cpp

win32 {
    LIBS += -lpsapi
    DEFINES += PSAPI_VERSION=1
    #QT       += axcontainer

    #fix for MinGW
    DEFINES += _WIN32_WINNT=0x0600
}

contains(QT,qml){
  lupdate_only{
    SOURCES += $$PWD/SystemProcessList.qml
  }
  DISTFILES += $$PWD/SystemProcessList.qml

  RESOURCES += $$PWD/systemprocess.qrc
}
