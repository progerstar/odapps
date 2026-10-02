#ifndef MIFARECLASSICVALUEEDITOR_H
#define MIFARECLASSICVALUEEDITOR_H

#include <QDialog>

#include "mifare_global.h"

namespace Ui {
class MifareClassicValueEditor;
}

class MifareClassicValueEditor : public QDialog
{
        Q_OBJECT

    public:
        explicit MifareClassicValueEditor(QWidget *parent = 0);
        ~MifareClassicValueEditor();

        void setBlockAddress(int addr);

        int block() const;
        qint32 value() const;
        quint8 address() const;
    private:
        Ui::MifareClassicValueEditor *ui;
};

#endif // MIFARECLASSICVALUEEDITOR_H
