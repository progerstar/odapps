#include "mifarentagconfigeditor.h"
#include <ui_mifarentagconfigeditor.h>

#include <mifaresector.h>
#include "mifareultralightconfig_common.h"

/*
 * Mirror Config:
 *  0 - No ASCII Mirror
 *  1 - UID ASCII Mirror
 *  2 - NFC counter ASCII Mirror
 *  3 - UID and NFC counter ASCII Mirror
 */
#define CFG0_MOD_BITS_MIRROR_CONFG    (0xC0)
#define CFG0_MOD_MIRROR_CONFIG_OFF      (0x00)
#define CFG0_MOD_MIRROR_CONFIG_UID      (0x01)
#define CFG0_MOD_MIRROR_CONFIG_CNT      (0x02)
#define CFG0_MOD_MIRROR_CONFIG_UID_CNT  (0x03)

#define CFG0_MOD_BITS_MIRROR_BYTE     (0x30)

#define CFG0_MIRROR_PAGE_BYTE     (2)

#define CFG1_ACCESS_BITS_NFC_CNT_EN    (0x10)
#define CFG1_ACCESS_BITS_NFC_CNT_PROT  (0x08)


MifareNTAGConfigEditor::MifareNTAGConfigEditor(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MifareNTAGConfigEditor)
{
    ui->setupUi(this);
}

MifareNTAGConfigEditor::~MifareNTAGConfigEditor()
{
    delete ui;
}


void MifareNTAGConfigEditor::setup(const quint8* cfg0, const quint8* cfg1)
{
    ui->asciiModeCombo->setCurrentIndex((cfg0[CFG0_MOD_BYTE] & CFG0_MOD_BITS_MIRROR_CONFG) >> 6);
    ui->asciiPageSpin->setValue(cfg0[CFG0_MIRROR_PAGE_BYTE]);
    ui->asciiByteSpin->setValue((cfg0[CFG0_MOD_BYTE] & CFG0_MOD_BITS_MIRROR_BYTE) >> 4);
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
    ui->nfcCntEnableCheck->setChecked(cfg1[CFG1_ACCESS_BYTE] & CFG1_ACCESS_BITS_NFC_CNT_EN);
    ui->nfcCntPasswordCheck->setChecked(cfg1[CFG1_ACCESS_BYTE] & CFG1_ACCESS_BITS_NFC_CNT_PROT);
}

void MifareNTAGConfigEditor::apply(MifareSectorInterface* card, int cfg0_blk, int cfg1_blk)
{
    quint8 newcfg0[4] = {0,0,0,0};
    newcfg0[CFG0_MOD_BYTE] = (ui->asciiModeCombo->currentIndex() << 6) |
                             (ui->asciiByteSpin->value() << 4) |
                             (ui->strgmodenCheck->isChecked() ? CFG0_MOD_BITS_STRG_MOD_EN : 0);
    newcfg0[CFG0_MIRROR_PAGE_BYTE] = ui->asciiPageSpin->value();
    newcfg0[CFG0_AUTH0_BYTE] = ui->auth0Spin->value();
    card->write(cfg0_blk, newcfg0);

    quint8 newcfg1[4] = {0,0,0,0};
    newcfg1[CFG1_ACCESS_BYTE] = (ui->protRWCheck->isChecked() ? CFG1_ACCESS_BITS_PROT : 0) |
                                (ui->cfglckCheck->isChecked() ? CFG1_ACCESS_BITS_CFGLCK : 0) |
                                (ui->nfcCntEnableCheck->isChecked() ? CFG1_ACCESS_BITS_NFC_CNT_EN : 0) |
                                (ui->nfcCntPasswordCheck->isChecked() ? CFG1_ACCESS_BITS_NFC_CNT_PROT : 0) |
                                ui->authlimSpin->value();

    card->write(cfg1_blk, newcfg1);
}
