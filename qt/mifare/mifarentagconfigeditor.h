#ifndef MIFARENTAGCONFIGEDITOR_H
#define MIFARENTAGCONFIGEDITOR_H

#include <QDialog>
#include <mifareblock.h>


namespace Ui {
class MifareNTAGConfigEditor;
}

class MifareSectorInterface;

class MifareNTAGConfigEditor : public QDialog
{
        Q_OBJECT

    public:
        explicit MifareNTAGConfigEditor(QWidget *parent = nullptr);
        ~MifareNTAGConfigEditor();

        void setup(const quint8* cfg0, const quint8* cfg1);
        void apply(MifareSectorInterface* card, int cfg0_blk, int cfg1_blk);
    private:
        Ui::MifareNTAGConfigEditor *ui;
};

#endif // MIFARENTAGCONFIGEDITOR_H
