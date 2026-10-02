#include "mifareclassicvalueeditor.h"
#include <ui_mifareclassicvalueeditor.h>


#include <QRegExpValidator>

MifareClassicValueEditor::MifareClassicValueEditor(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MifareClassicValueEditor)
{
    ui->setupUi(this);
    ui->valueEdit->setRange(2147483648LL*-1, 2147483647LL);
    ui->valueEdit->setMode(QNumberEdit::Dec);
    ui->valueEdit->setValue(0);
}

MifareClassicValueEditor::~MifareClassicValueEditor()
{
    delete ui;
}

void MifareClassicValueEditor::setBlockAddress(int addr)
{
    ui->blockNumberCombo->clear();
    if(/*sector<MifareClassicFirstJumboSector*/addr < 4*MifareClassicFirstJumboSector)
    {
        //normal
        for(int i=0;i<3;++i)
        {
            ui->blockNumberCombo->addItem(QString::number(i));
        }
    }
    else
    {
        //jumbo
        for(int i=0;i<15;++i)
        {
            ui->blockNumberCombo->addItem(QString::number(i));
        }
    }
}

int MifareClassicValueEditor::block() const
{
    return ui->blockNumberCombo->currentIndex();
}

qint32 MifareClassicValueEditor::value() const
{
    return qint32(ui->valueEdit->value());
}

quint8 MifareClassicValueEditor::address() const
{
    return ui->addrSpin->value();
}
