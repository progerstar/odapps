#ifndef MIFAREULTRALIGHTEV1CONFIGEDITOR_H
#define MIFAREULTRALIGHTEV1CONFIGEDITOR_H

#include <QDialog>
#include <mifareblock.h>

namespace Ui {
class MifareUltralightEV1ConfigEditor;
}

class MifareSectorInterface;

class MifareUltralightEV1ConfigEditor : public QDialog
{
        Q_OBJECT

    public:
        explicit MifareUltralightEV1ConfigEditor(QWidget *parent = nullptr);
        ~MifareUltralightEV1ConfigEditor();

        void setup(const quint8* cfg0, const quint8* cfg1);
        void apply(MifareSectorInterface* card, int cfg0_blk, int cfg1_blk);
    private:
        Ui::MifareUltralightEV1ConfigEditor *ui;
};

#endif // MIFAREULTRALIGHTEV1CONFIGEDITOR_H
