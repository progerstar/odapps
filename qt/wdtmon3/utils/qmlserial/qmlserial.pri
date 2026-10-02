isEmpty(QMLSERIAL_PRI_INCLUDED) {

QMLSERIAL_PRI_INCLUDED=1

INCLUDEPATH += $$PWD

HEADERS += $$PWD/ql-channel.hpp \
           $$PWD/ql-channel-serial.hpp

SOURCES += $$PWD/ql-channel.cpp \
           $$PWD/ql-channel-serial.cpp
}
