INCLUDEPATH += $$PWD

QT += core gui widgets serialport svg
isEmpty(DISTRO_ANDROID){
    QT += sql
}

#IMPORTANT:
CONFIG += rtti

INCLUDEPATH += $$PWD/../qhidapi/

HEADERS += $$PWD/bitfieldeditor.h \
           $$PWD/em4100data.h \
           $$PWD/lfmemoryprotocol.h \
           $$PWD/mifareblockrawtablemodel.h \
           $$PWD/mifareblockrawtablecolors.h \
           $$PWD/mifarentagconfigeditor.h \
           $$PWD/mifareultralightconfig_common.h \
           $$PWD/mifareultralightev1configeditor.h \
           $$PWD/mifareultralightpasswordeditor.h \
           $$PWD/qnumberedit.h \
           $$PWD/rawtablemodelitemdelegate.h \
           $$PWD/mifareclassictrailereditor.h \
           $$PWD/mifareclassickeyedit.h \
           $$PWD/rfidcardreaderinterface.h \
           $$PWD/mifareclassicvalueeditor.h \
           $$PWD/qhexlineedit.h \
           $$PWD/mifareblock.h \
           $$PWD/rfidcardreaderoperator.h \
           $$PWD/rfidtableview.h \
           $$PWD/lockbitseditor.h \
           $$PWD/mifaresector.h \
           $$PWD/mifarekeydatabase.h \
           $$PWD/qtexttoolbutton.h \
           $$PWD/rfidcardcdcatreader.h \
           $$PWD/rfidcardhidreader.h \
           $$PWD/mifare_global.h \
           $$PWD/rfidcardsimulatorreader.h \

SOURCES += $$PWD/bitfieldeditor.cpp \
           $$PWD/em4100data.cpp \
           $$PWD/lfmemoryprotocol.cpp \
           $$PWD/mifareblockrawtablemodel.cpp \
           $$PWD/mifareblockrawtablecolors.cpp \
           $$PWD/mifarentagconfigeditor.cpp \
           $$PWD/mifarentageeprom.cpp \
           $$PWD/mifareultralighteepromev1.cpp \
           $$PWD/mifareultralightev1configeditor.cpp \
           $$PWD/mifareultralightpasswordeditor.cpp \
           $$PWD/qnumberedit.cpp \
           $$PWD/rawtablemodelitemdelegate.cpp \
           $$PWD/mifareclassicsector.cpp \
           $$PWD/mifareclassictrailereditor.cpp \
           $$PWD/rfidfirmwareversion.cpp \
           $$PWD/rfidcardreaderinterface.cpp \
           $$PWD/rfidcardreaderoperator.cpp \
           $$PWD/rfidcardsimulatorreader.cpp \
           $$PWD/mifareclassicvalueeditor.cpp \
           $$PWD/qhexlineedit.cpp \
           $$PWD/rfidtableview.cpp \
           $$PWD/mifareultralighteeprom.cpp \
           $$PWD/lockbitseditor.cpp \
           $$PWD/mifareultralightceeprom.cpp \
           $$PWD/mifarekeydatabase.cpp \
           $$PWD/qtexttoolbutton.cpp \
           $$PWD/rfidcardcdcatreader.cpp \
           $$PWD/rfidcardhidreader.cpp \
           $$PWD/mifare_global.cpp

FORMS    += $$PWD/mifareclassictrailereditor.ui \
            $$PWD/mifareclassicvalueeditor.ui \
            $$PWD/mifarentagconfigeditor.ui \
            $$PWD/mifareultralightev1configeditor.ui \
            $$PWD/mifareultralightpasswordeditor.ui

RESOURCES += $$PWD/mifare.qrc
