isEmpty(QENDIANRW_INCLUDED) {
    QENDIANRW_INCLUDED=1
    HEADERS += $$PWD/winbswap.h
    HEADERS += $$PWD/endianrw.h
    INCLUDEPATH += $$PWD
}
