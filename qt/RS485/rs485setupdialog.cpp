#include "rs485setupdialog.h"
#include <ui_rs485setupdialog.h>

#include <qcomport.h>
#define RPREFIX ("RS485")

quint32 RS485SetupDialog::standardBaudrate(quint16 rate)
{
    switch(rate)
    {
        case BAUD_300: return 300;
        case BAUD_600: return 600;
        case BAUD_1200: return 1200;
        case BAUD_2400: return 2400;
        case BAUD_4800: return 4800;
        case BAUD_9600: return 9600;
        case BAUD_14400: return 14400;
        case BAUD_19200: return 19200;
        case BAUD_38400: return 38400;
        case BAUD_57600: return 57600;
        case BAUD_74880: return 74880;
        case BAUD_115200: return 115200;
        case BAUD_128000: return 128000;
        case BAUD_230400: return 230400;
        case BAUD_256000: return 256000;
        case BAUD_460800: return 460800;
        case BAUD_921600: return 921600;
        default:break;
    }
    return 115200;
}

RS485SetupDialog::RS485SetupDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::RS485SetupDialog), amode(Addressing_RS458)
{
    ui->setupUi(this);

    for(quint16 i=0; i<=RS485SetupDialog::BAUD_LAST;++i)
    {
        ui->baudCombo->addItem(QString::number(RS485SetupDialog::standardBaudrate(i)));
    }
    ui->baudCombo->setEditable(true);


}

RS485SetupDialog::~RS485SetupDialog()
{
    delete ui;
}

void RS485SetupDialog::readSettings(const QString& prefix)
{
    QSettings set;

    QHash<QString, QVariant> cfg;
    cfg.insert(QCOMPORT_SETTING_ADDR(RPREFIX), set.value(QCOMPORT_SETTING_ADDR(prefix), QCOMPORT_SETTING_ADDR_DEFAULT));
    cfg.insert(QCOMPORT_SETTING_BAUD(RPREFIX), set.value(QCOMPORT_SETTING_BAUD(prefix), QCOMPORT_SETTING_BAUD_DEFAULT));
    cfg.insert(QCOMPORT_SETTING_PARITY(RPREFIX), set.value(QCOMPORT_SETTING_PARITY(prefix), QCOMPORT_SETTING_PARITY_DEFAULT));
    cfg.insert(QCOMPORT_SETTING_FLOW(RPREFIX), set.value(QCOMPORT_SETTING_FLOW(prefix), QCOMPORT_SETTING_FLOW_DEFAULT));
    cfg.insert(QCOMPORT_SETTING_STOP(RPREFIX), set.value(QCOMPORT_SETTING_STOP(prefix), QCOMPORT_SETTING_STOP_DEFAULT));

    this->set(cfg);
}

void RS485SetupDialog::writeSettings(const QString& prefix)
{
    QSettings set;

    QHash<QString,QVariant> cfg = get();
    set.setValue(QCOMPORT_SETTING_ADDR(prefix), cfg.value(QCOMPORT_SETTING_ADDR(RPREFIX)));
    set.setValue(QCOMPORT_SETTING_BAUD(prefix), cfg.value(QCOMPORT_SETTING_BAUD(RPREFIX)));
    set.setValue(QCOMPORT_SETTING_PARITY(prefix), cfg.value(QCOMPORT_SETTING_PARITY(RPREFIX)));
    set.setValue(QCOMPORT_SETTING_FLOW(prefix), cfg.value(QCOMPORT_SETTING_FLOW(RPREFIX)));
    set.setValue(QCOMPORT_SETTING_STOP(prefix), cfg.value(QCOMPORT_SETTING_STOP(RPREFIX)));
}

bool RS485SetupDialog::scanning() const
{
    return !ui->addressScanCheck->isHidden();
}

void RS485SetupDialog::setScanning(bool on)
{
    ui->addressScanCheck->setHidden(!on);
}

void RS485SetupDialog::setAddressingMode(RS485SetupDialog::AddressingMode mode)
{
    amode = mode;
    if(mode == Addressing_MODBUS)
    {
        ui->addressSpin->setRange(1, 247);
    }
    else
    {
        ui->addressSpin->setRange(0, 254);
    }
}

QHash<QString,QVariant> RS485SetupDialog::get() const
{
    QHash<QString,QVariant> cfg;
    if(!ui->addressScanCheck->isHidden() && ui->addressScanCheck->isChecked())
    {
        cfg.insert(QCOMPORT_SETTING_ADDR(RPREFIX), ((amode == Addressing_MODBUS) ? 0 : 0xFF));
    }
    else
    {
        cfg.insert(QCOMPORT_SETTING_ADDR(RPREFIX), ui->addressSpin->value());
    }

    bool ok;
    quint32 baud = ui->baudCombo->currentText().toUInt(&ok);
    cfg.insert(QCOMPORT_SETTING_BAUD(RPREFIX), (ok ? baud : 115200));

    switch(ui->parityCombo->currentIndex())
    {
        default:
        case 0: cfg.insert(QCOMPORT_SETTING_PARITY(RPREFIX), QSerialPort::NoParity);break;
        case 1: cfg.insert(QCOMPORT_SETTING_PARITY(RPREFIX), QSerialPort::OddParity);break;
        case 2: cfg.insert(QCOMPORT_SETTING_PARITY(RPREFIX), QSerialPort::EvenParity);break;
    }

    switch(ui->stopBitsCombo->currentIndex())
    {
        default:
        case 0: cfg.insert(QCOMPORT_SETTING_STOP(RPREFIX), QSerialPort::OneStop);break;
        case 1: cfg.insert(QCOMPORT_SETTING_STOP(RPREFIX), QSerialPort::OneAndHalfStop);break;
        case 2: cfg.insert(QCOMPORT_SETTING_STOP(RPREFIX), QSerialPort::TwoStop);break;
    }

    cfg.insert(QCOMPORT_SETTING_FLOW(RPREFIX), QSerialPort::NoFlowControl);
    return cfg;
}

void RS485SetupDialog::set(const QHash<QString, QVariant>& cfg)
{
    if(((amode == Addressing_RS458) && (cfg.value(QCOMPORT_SETTING_ADDR(RPREFIX), 0).toUInt() == 0xFF)) ||
       ((amode == Addressing_MODBUS) && (cfg.value(QCOMPORT_SETTING_ADDR(RPREFIX), 0).toUInt() == 0)))
    {
        if(!ui->addressScanCheck->isHidden())
        {
            ui->addressScanCheck->setChecked(true);
        }
    }
    else
    {
        ui->addressSpin->setValue(cfg.value(QCOMPORT_SETTING_ADDR(RPREFIX)).toUInt());
    }

    ui->baudCombo->setCurrentText(QString::number(cfg.value(QCOMPORT_SETTING_BAUD(RPREFIX), 115200).toUInt()));

    switch(cfg.value(QCOMPORT_SETTING_PARITY(RPREFIX)).toInt())
    {
        case QSerialPort::OddParity: ui->parityCombo->setCurrentIndex(1);break;
        case QSerialPort::EvenParity: ui->parityCombo->setCurrentIndex(2);break;
        default: ui->parityCombo->setCurrentIndex(0);break;
    }

    switch(cfg.value(QCOMPORT_SETTING_STOP(RPREFIX)).toInt())
    {
        case QSerialPort::OneAndHalfStop: ui->stopBitsCombo->setCurrentIndex(1);break;
        case QSerialPort::TwoStop: ui->stopBitsCombo->setCurrentIndex(2);break;
        default: ui->stopBitsCombo->setCurrentIndex(0);break;
    }
}
