#include "mifareclassictrailereditor.h"
#include <ui_mifareclassictrailereditor.h>

#include "mifaresector.h"

MifareClassicTrailerEditor::MifareClassicTrailerEditor(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MifareClassicTrailerEditor),
    m_acc_key(MifareClassicKeyA)
{
    ui->setupUi(this);
    setUIMode(BlockEditor);
    for(int i=0;i<8;++i)
    {
        ui->trailerModeCombo->addItem(QString::number(i));
    }
}

MifareClassicTrailerEditor::~MifareClassicTrailerEditor()
{
    delete ui;
}

void MifareClassicTrailerEditor::setUIMode(Mode mode)
{
    m_mode = mode;
    ui->keysGroup->setVisible(mode==TrailerEditor);
}

void MifareClassicTrailerEditor::setMode(quint8 mode, bool on)
{
    if(mode<8)
    {
        ui->trailerModeCombo->setCurrentIndex(mode);
    }
    ui->trailerModeCombo->setEnabled(on);
}

quint8 MifareClassicTrailerEditor::getMode() const
{
    return ui->trailerModeCombo->currentIndex();
}

void MifareClassicTrailerEditor::setKey(MifareClassicKeyType type, const MifareClassicKey& key)
{
    MifareClassicKeyEdit* ed = (type==MifareClassicKeyA)?ui->keyAEdit:ui->keyBEdit;
    ed->setText(key.toString());
}

void MifareClassicTrailerEditor::setKeyAvailable(MifareClassicKeyType type, bool on)
{
    QCheckBox* box = (type==MifareClassicKeyA)?ui->keyACheck:ui->keyBCheck;
    if(on)
        box->setEnabled(true);
    else
    {
        box->setEnabled(false);
        box->setChecked(false);
    }
}

bool MifareClassicTrailerEditor::keyModified(MifareClassicKeyType type) const
{
    return (type==MifareClassicKeyA) ? ui->keyACheck->isChecked() : ui->keyBCheck->isChecked();
}

MifareClassicKey MifareClassicTrailerEditor::getKey(MifareClassicKeyType type) const
{
    MifareClassicKeyEdit* ed = (type==MifareClassicKeyA)?ui->keyAEdit:ui->keyBEdit;
    MifareClassicKey ret;
    ret.fromString(ed->text());
    return ret;
}

void MifareClassicTrailerEditor::on_trailerModeCombo_currentIndexChanged(int index)
{
    if(m_mode==TrailerEditor)
    {
        MifareClassicTrailerAccess tacc = MifareClassicFunctions::trailerAccessHash.value(index);
        //ui->keyAEdit->setEnabled(tacc.keyA_write & ((m_acc_key==MifareClassicKeyA)?MC_Permissions_KeyA:MC_Permissions_KeyB) );
        //ui->keyBEdit->setEnabled(tacc.keyB_write & ((m_acc_key==MifareClassicKeyA)?MC_Permissions_KeyA:MC_Permissions_KeyB) );

        ui->modeDetailsLabel->setText(QString("<table border=\"1\" align=\"center\" width=\"100%\"><thead><tr><th>")+
                                      tr("Field")+
                                      QString("</th><th>")+
                                      tr("Read")+
                                      QString("</th><th>")+
                                      tr("Write")+QString("</th></tr></thead><tr><td>")+
                                      tr("Key A")+
                                      QString("</td><td>%1</td><td>%2</td></tr><tr><td>")
                                      .arg(mifareClassicPermissionString(tacc.keyA_read))
                                      .arg(mifareClassicPermissionString(tacc.keyA_write))+
                                      tr("Access Bits")+
                                      QString("</td><td>%1</td><td>%2</td></tr><tr><td>")
                                      .arg(mifareClassicPermissionString(tacc.bits_read))
                                      .arg(mifareClassicPermissionString(tacc.bits_write))+
                                      tr("Key B")+QString("</td><td>%1</td><td>%2</td></tr></table>")
                                      .arg(mifareClassicPermissionString(tacc.keyB_read))
                                      .arg(mifareClassicPermissionString(tacc.keyB_write)));
    }
    else
    {
        MifareClassicBlockAccess bacc = MifareClassicFunctions::blockAccessHash.value(index);

        ui->modeDetailsLabel->setText(QString("<table border=\"1\" align=\"center\" width=\"100%\"><thead><tr><th>")+
                                      tr("Operation")+
                                      QString("</th><th>")+
                                      tr("Permissions")+
                                      QString("</th></tr></thead><tr><td>")+
                                      tr("Read")+
                                      QString("</td><td>%1</td></tr><tr><td>")
                                      .arg(mifareClassicPermissionString(bacc.block_read))+
                                      tr("Write")+
                                      QString("</td><td>%1</td></tr><tr><td>")
                                      .arg(mifareClassicPermissionString(bacc.block_write))+
                                      tr("Increment")+
                                      QString("</td><td>%1</td></tr><tr><td>")
                                      .arg(mifareClassicPermissionString(bacc.block_incr))+
                                      tr("Decrement")+
                                      QString("</td><td>%1</td></tr></table>")
                                      .arg(mifareClassicPermissionString(bacc.block_decr)));
    }
}
