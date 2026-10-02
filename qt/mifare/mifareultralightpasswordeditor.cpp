#include "mifareultralightpasswordeditor.h"
#include <ui_mifareultralightpasswordeditor.h>

#include <mifaresector.h>
#include <endianrw.h>

MifareUltralightPasswordEditor::MifareUltralightPasswordEditor(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MifareUltralightPasswordEditor)
{
    ui->setupUi(this);
    ui->pwdEdit->setRange(0x00, 0xFFFFFFFFL);
    ui->pwdEdit->setMode(QNumberEdit::Hex);
    ui->packEdit->setRange(0x00, 0xFFFF);
    ui->packEdit->setMode(QNumberEdit::Hex);

    connect(ui->pwdByteZeroSpin, SIGNAL(valueChanged(int)), this, SLOT(pwdBytes_valueChanged(int)));
    connect(ui->pwdByteOneSpin, SIGNAL(valueChanged(int)), this, SLOT(pwdBytes_valueChanged(int)));
    connect(ui->pwdByteTwoSpin, SIGNAL(valueChanged(int)), this, SLOT(pwdBytes_valueChanged(int)));
    connect(ui->pwdByteThreeSpin, SIGNAL(valueChanged(int)), this, SLOT(pwdBytes_valueChanged(int)));

    connect(ui->packByteZeroSpin, SIGNAL(valueChanged(int)), this, SLOT(packBytes_valueChanged(int)));
    connect(ui->packByteOneSpin, SIGNAL(valueChanged(int)), this, SLOT(packBytes_valueChanged(int)));
}

MifareUltralightPasswordEditor::~MifareUltralightPasswordEditor()
{
    delete ui;
}

void MifareUltralightPasswordEditor::setup(quint32 pwd, quint16 pack)
{
    ui->pwdEdit->setValue(pwd);
    on_pwdEdit_valueChanged(ui->pwdEdit->value());
    ui->packEdit->setValue(pack);
    on_packEdit_valueChanged(ui->packEdit->value());
}

void MifareUltralightPasswordEditor::apply(MifareSectorInterface* card)
{
    MifareUltralightAbstractEEPROM_EV1Interface* iface = dynamic_cast<MifareUltralightAbstractEEPROM_EV1Interface*>(card);
    if(iface)
    {
        iface->setCredentials(quint32(ui->pwdEdit->value()), quint16(ui->packEdit->value()));
    }
    else
    {
        qWarning()<<"MifareUltralightPasswordEditor::apply failed - not an Ultralight EV1 Interface";
    }
}

quint32 MifareUltralightPasswordEditor::password() const
{
    return quint32(ui->pwdEdit->value());
}

quint16 MifareUltralightPasswordEditor::pack() const
{
    return quint16(ui->packEdit->value());
}

void MifareUltralightPasswordEditor::pwdBytes_valueChanged(int v)
{
    Q_UNUSED(v);
    ui->pwdEdit->blockSignals(true);
    ui->pwdEdit->setValue((qint64(ui->pwdByteThreeSpin->value())<<24)|
                          (qint64(ui->pwdByteTwoSpin->value())<<16)|
                          (qint64(ui->pwdByteOneSpin->value())<<8)|
                           qint64(ui->pwdByteZeroSpin->value()));
    ui->pwdEdit->blockSignals(false);
}

void MifareUltralightPasswordEditor::on_pwdEdit_valueChanged(qint64 v)
{
    ui->pwdByteZeroSpin->blockSignals(true);
    ui->pwdByteZeroSpin->setValue(v&0xFF);
    ui->pwdByteZeroSpin->blockSignals(false);

    ui->pwdByteOneSpin->blockSignals(true);
    ui->pwdByteOneSpin->setValue((v>>8)&0xFF);
    ui->pwdByteOneSpin->blockSignals(false);

    ui->pwdByteTwoSpin->blockSignals(true);
    ui->pwdByteTwoSpin->setValue((v>>16)&0xFF);
    ui->pwdByteTwoSpin->blockSignals(false);

    ui->pwdByteThreeSpin->blockSignals(true);
    ui->pwdByteThreeSpin->setValue((v>>24)&0xFF);
    ui->pwdByteThreeSpin->blockSignals(false);
}

void MifareUltralightPasswordEditor::packBytes_valueChanged(int v)
{
    Q_UNUSED(v);
    ui->packEdit->blockSignals(true);
    ui->packEdit->setValue((qint64(ui->packByteOneSpin->value())<<8)|
                           qint64(ui->packByteZeroSpin->value()));
    ui->packEdit->blockSignals(false);
}

void MifareUltralightPasswordEditor::on_packEdit_valueChanged(qint64 v)
{
    ui->packByteZeroSpin->blockSignals(true);
    ui->packByteZeroSpin->setValue(v&0xFF);
    ui->packByteZeroSpin->blockSignals(false);

    ui->packByteOneSpin->blockSignals(true);
    ui->packByteOneSpin->setValue((v>>8)&0xFF);
    ui->packByteOneSpin->blockSignals(false);
}
