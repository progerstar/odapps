#ifndef MIFARECLASSICKEYEDIT_H
#define MIFARECLASSICKEYEDIT_H

#include <qhexlineedit.h>

class MifareClassicKeyEdit : public QHexLineEdit
{
        Q_OBJECT
    public:
        enum Class {
            KC_Classic,
            KC_Plus,
        };
        Q_ENUM(Class)

        MifareClassicKeyEdit(QWidget* parent = 0) : QHexLineEdit(parent)
        {
            setClass(KC_Classic);
        }
    public slots:
        inline void setClass(Class ck)
        {
            switch(ck)
            {
                case KC_Classic:
                    setSize(6);
                    break;
                case KC_Plus:
                    setSize(16);
                    break;
            }
            setDefaultValue(0xFF);
        }
};

class MifareUltralightCKeyEdit : public QHexLineEdit
{
        Q_OBJECT
    public:
        MifareUltralightCKeyEdit(QWidget* parent = 0) : QHexLineEdit(parent)
        {
            setSize(8);
            setDefaultValue(0x00);
        }
};

#endif // MIFARECLASSICKEYEDIT_H
