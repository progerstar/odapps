#include "mifareultralightev1configeditor.h"
#include "mifareultralightconfig_common.h"
#include <ui_mifareultralightev1configeditor.h>

#include <mifaresector.h>

#define CFG1_VCTID_BYTE           (1)          /*Virtual Card Type Identifier response*/

MifareUltralightEV1ConfigEditor::MifareUltralightEV1ConfigEditor(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MifareUltralightEV1ConfigEditor)
{
    ui->setupUi(this);
}

MifareUltralightEV1ConfigEditor::~MifareUltralightEV1ConfigEditor()
{
    delete ui;
}

void MifareUltralightEV1ConfigEditor::setup(const quint8* cfg0, const quint8* cfg1)
{
    ui->strgmodenCheck->setChecked(cfg0[CFG0_MOD_BYTE] & CFG0_MOD_BITS_STRG_MOD_EN);
    ui->auth0Spin->setValue(cfg0[CFG0_AUTH0_BYTE]);
    if(cfg1[CFG1_ACCESS_BYTE] & CFG1_ACCESS_BITS_PROT)
    {
        ui->protRWCheck->setChecked(true);
    }
    else
    {
        ui->protWOCheck->setChecked(true);
    }
    ui->cfglckCheck->setChecked(cfg1[CFG1_ACCESS_BYTE] & CFG1_ACCESS_BITS_CFGLCK);
    ui->authlimSpin->setValue(cfg1[CFG1_ACCESS_BYTE] & CFG1_ACCESS_BITS_AUTHLIM);
    ui->vctidSpin->setValue(cfg1[CFG1_VCTID_BYTE]);
}

void MifareUltralightEV1ConfigEditor::apply(MifareSectorInterface* card, int cfg0_blk, int cfg1_blk)
{
    quint8 newcfg0[4] = {0,0,0,0};
    newcfg0[CFG0_MOD_BYTE] = ui->strgmodenCheck->isChecked() ? CFG0_MOD_BITS_STRG_MOD_EN : 0;
    newcfg0[CFG0_AUTH0_BYTE] = ui->auth0Spin->value();
    card->write(cfg0_blk, newcfg0);

    quint8 newcfg1[4] = {0,0,0,0};
    newcfg1[CFG1_ACCESS_BYTE] = (ui->protRWCheck->isChecked() ? CFG1_ACCESS_BITS_PROT : 0) |
                                (ui->cfglckCheck->isChecked() ? CFG1_ACCESS_BITS_CFGLCK : 0) |
                                ui->authlimSpin->value();
    newcfg1[CFG1_VCTID_BYTE] = ui->vctidSpin->value();
    card->write(cfg1_blk, newcfg1);
}
