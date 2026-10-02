TEMPLATE = subdirs
CONFIG += ordered

SUBDIRS += \
    odrfidkit_app \
    odrfidcfg_app \
    odrfidcfg_m_app \
    odhiddfu_app \
    wdtmon3_app \
    wdtmon3_mini_app \
    iosenmon_app

odrfidkit_app.file = $$PWD/odrfidkit/odrfidkit.pro
odrfidcfg_app.file = $$PWD/odrfidcfg/odrfidcfg.pro
odrfidcfg_m_app.file = $$PWD/odrfidcfgM/odrfidcfgM.pro
odhiddfu_app.file = $$PWD/odhiddfu/odhiddfu.pro
wdtmon3_app.file = $$PWD/wdtmon3/wdtmon3.pro
wdtmon3_mini_app.file = $$PWD/wdtmon3-mini/wdtmon3_lite.pro
iosenmon_app.file = $$PWD/iosenmon/iosenmon.pro
