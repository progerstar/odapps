#include "odrfidconfig.h"
#include <ui_odrfidconfig.h>
#include <qmaterialfont.h>

#include <endianrw.h>
#include <rs485setupdialog.h>
#include "logwindow.h"

#include <QTimer>
#include <QEventLoop>
#include <QDebug>
#include <QTime>
#include <QInputDialog>
#include <QProgressDialog>
#include <qautoclosemessagebox.h>
#include <endianrw.h>

#include <QSerialPort>
#include <QSerialPortInfo>

#include <qcomport.h>
#define RS485_SETTINGS_PREFIX ("RS485")

typedef enum {
    HID_PARAM_QUERY = 0,
    HID_PARAM_ON,
    HID_PARAM_OFF,
    HID_PARAM_SWITCH,
    HID_PARAM_DEFAULT,
} HID_CMD_PARAM;

#define CFG_USART_PARITY_NONE (0)
#define CFG_USART_PARITY_ODD  (1)
#define CFG_USART_PARITY_EVEN (2)

#define CFG_USART_STOP_1      (10)
#define CFG_USART_STOP_1_5    (15)
#define CFG_USART_STOP_2      (20)

#define USART_STOP_BIT_1_IDX   (0)
#define USART_STOP_BIT_1_5_IDX (1)
#define USART_STOP_BIT_2_IDX   (2)

static const uint8_t kStopBitsMap[] = {
    CFG_USART_STOP_1, CFG_USART_STOP_1_5, CFG_USART_STOP_2
};

#define USART_BITS_7_IDX (0)
#define USART_BITS_8_IDX (1)

#define RTU_HOLDREG_START_BUF   (0x00)
#define RTU_HOLDREG_END_BUF     (0x7D)
#define RTU_HOLDREG_DROP_BUF    (0x7E)
#define RTU_HOLDREG_REBOOT_UAPP (0x7F)
#define RTU_HOLDREG_REBOOT_UDFU (0x80)
#define RTU_HOLDREG_REBOOT_SDFU (0x81)

#ifndef OD_NO_DEVELOPER
#define GET_HOLDING_ADDRESS(x)  ((x) + hold_offset)
#else
#define GET_HOLDING_ADDRESS(x)  (x)
#endif

/*
 * Init SEQ:
 *  AT+L?
 *  AT+Z?
 *  AT+G?
 *  AT+T?
 *  AT+SCAN?
 *  AT+K?
 *  AT+F?
 */

ODRFIDConfig::ODRFIDConfig(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::ODRFIDConfig),
    console(new CommandConsoleDialog(this)),
    log(new LogWindow(0)),
    comm(new QModbusRtuSerialMaster(this)),
    cop(OP_READ), conn_params_changed(false),
    selected_address(0)
#ifndef OD_NO_DEVELOPER
    , hold_offset(0)
#endif
{
    console->hide();
    ui->setupUi(this);
    ui->devInfoLabel->setWordWrap(true);
    ui->mainToolBar->hide();
    ui->keyEdit->setSize(6);
    ui->ulevGroup->setEnabled(false);
    ui->ulevEdit->setSize(4);
    ui->plusBox->setEnabled(false);
    ui->keyplusEdit->setSize(16);
    MaterialUI("developer_mode", ui->consoleTool);
    MaterialUI("system_update", ui->fwTool);
    MaterialUI("table_chart", ui->logTool);

    setWindowTitle(tr("ODRFIDCfgM v%1").arg(ODRFIDCFG_VERSION));
#ifndef OD_NO_DEVELOPER
    setStyleSheet("QStatusBar{background-color: \"#80ff80\";}");
#endif

    QObject::connect(comm, &QModbusRtuSerialMaster::errorOccurred, this, &ODRFIDConfig::hnd_errorOccurred);
    QObject::connect(console, &CommandConsoleDialog::commit, this, &ODRFIDConfig::console_commit);
    QObject::connect(ui->logTool, &QToolButton::clicked, this, [=]{
        this->log->show();
        this->log->raise();
    });

    QObject::connect(ui->actionExit,SIGNAL(triggered()),qApp,SLOT(quit()));
    QObject::connect(ui->actionAbout,SIGNAL(triggered()),this,SLOT(about()));

    QObject::connect(ui->addressSpin, SIGNAL(valueChanged(int)), this, SLOT(connParamChanged()));
    QObject::connect(ui->baudSpin, SIGNAL(valueChanged(int)), this, SLOT(connParamChanged()));
    QObject::connect(ui->stopCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(connParamChanged()));
    QObject::connect(ui->parityCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(connParamChanged()));
    QObject::connect(ui->timeoutSpin, SIGNAL(valueChanged(int)), this, SLOT(connParamChanged()));
}

ODRFIDConfig::~ODRFIDConfig()
{
    comm->disconnectDevice();
    delete log;
    delete ui;
}

QStringList ODRFIDConfig::availableDevices() const
{
    QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    QStringList ports_list;
    ports_list.reserve(ports.size());
    foreach(const QSerialPortInfo& portInfo, ports)
    {
        ports_list.append(portInfo.portName());
    }
    return ports_list;
}

static inline QString parseDevInfo(const QByteArray& ati, bool multiline=false)
{
    QString info = QString::fromLatin1(ati).replace("\r\nOK\r\n", "").replace("\r\n\r\n", " ").replace("\r\n", " ").trimmed();
    qWarning()<<"Transformed "<<ati<<" to "<<info;
    if(multiline)
    {
        info.replace("S/N", "\nS/N");
        qWarning()<<" -> to multiline "<<info;
    }
    return info;
}

bool ODRFIDConfig::connect(const QString& name, bool from_settings)
{
    ui->controlWidget->setEnabled(false);
    QSettings set;

    if(!from_settings)
    {
        if(!set.contains(QCOMPORT_SETTING_ADDR(RS485_SETTINGS_PREFIX)))
        {
            set.setValue(QCOMPORT_SETTING_ADDR(RS485_SETTINGS_PREFIX), 0x5f);
        }

        RS485SetupDialog selector(this);
        selector.setAddressingMode(RS485SetupDialog::Addressing_MODBUS);
        selector.setScanning(true);
        selector.readSettings();

        if(selector.exec() != QDialog::Accepted)
            return false;

        selector.writeSettings();
    }

    comm->setConnectionParameter(QModbusDevice::SerialPortNameParameter, name);
    comm->setConnectionParameter(QModbusDevice::SerialParityParameter,
                                 set.value(QCOMPORT_SETTING_PARITY(RS485_SETTINGS_PREFIX), QSerialPort::NoParity).toInt());
    comm->setConnectionParameter(QModbusDevice::SerialBaudRateParameter,
                                 set.value(QCOMPORT_SETTING_BAUD(RS485_SETTINGS_PREFIX), 115200).toInt());
    comm->setConnectionParameter(QModbusDevice::SerialDataBitsParameter, 8);
    comm->setConnectionParameter(QModbusDevice::SerialStopBitsParameter,
                                 set.value(QCOMPORT_SETTING_STOP(RS485_SETTINGS_PREFIX), QSerialPort::OneStop).toInt());
    comm->setTimeout(2000);
    //comm->setInterFrameDelay(900);

    if(comm->connectDevice())
    {
        statusBar()->showMessage(tr("Connecting"), -1);


        int addr = QSettings().value(QCOMPORT_SETTING_ADDR(RS485_SETTINGS_PREFIX), 0x5F).toInt();
        if((addr<0)||(addr>247)) {
            addr=0x5F;
        }

        qWarning()<<"Requested connect to "<<addr;
        QProgressDialog progress(tr("Scanning"), tr("Abort"), 0, (addr==0) ? 248 : 1, this);
        progress.setWindowModality(Qt::NonModal);

        QList<int> devPool;
        if(addr == 0)
        {
            /*Drop buffers of all devices*/
            dropBuffers(0);
            addr = 1;
            int pendSize;
            comm->setTimeout(600);
            while(!progress.wasCanceled() && (addr<=247))
            {
                if(getPendingBufferSize(addr, &pendSize))
                {
                    qWarning()<<"InputReg read request succeded for "<<addr;
                    devPool.append(addr);
                }
                ++addr;
                progress.setLabelText(tr("Scanning (found %1)").arg(devPool.size()));
                progress.setValue(addr);
            }
            progress.setValue(248);
            comm->setTimeout(2000);
        }
        else
        {
            if(!dropBuffers(addr))
            {
                progress.setValue(1);
                comm->disconnectDevice();
                return false;
            }
            devPool.append(addr);
            progress.setValue(1);
        }
        qWarning()<<"Initial scan provided "<<devPool.size()<<" devices";

        QByteArray ati_output;
        QList<QPair<int, QByteArray> > devs;
        foreach(int devAddr, devPool)
        {
            ati_output.clear();
            if(sendCommand(devAddr, QByteArray("ATI\0"), &ati_output))
            {
                QByteArray cpy(ati_output.constData(), ati_output.size());
                devs.append(QPair<int,QByteArray>(devAddr, cpy));
            }
            else
            {
                qWarning()<<"Device at "<<devAddr<<" did not respond to ATI request";
            }
        }


        if(!devs.size())
        {
            comm->disconnectDevice();
            return false;
        }

        int devidx = 0;
        if(devs.size()>1)
        {
            QStringList selectList;
            for(int i=0;i<devs.size();++i)
            {
                selectList.append(QString("0x%1: %2").arg(devs.at(i).first,2,16,QLatin1Char('0'))
                                  .arg(parseDevInfo(devs.at(i).second)));
            }

            QString sel = QInputDialog::getItem(this, tr("Device Selection"), tr("Device:"), selectList, 0, false);
            if(sel.isEmpty())
            {
                comm->disconnectDevice();
                return false;
            }

            devidx = selectList.indexOf(sel);
        }

        qWarning()<<"Will talk to "<<int(devs.at(devidx).first);
        ui->devInfoLabel->setText(parseDevInfo(devs.at(devidx).second));
        selected_address = devs.at(devidx).first;
        dropBuffers(selected_address);

        on_readButton_clicked();
        return true;
    }
    return false;
}

bool ODRFIDConfig::processMessage(const QString& data)
{
    qWarning()<<"From uart: "<<data;
    QStringList parts = data.split("\r\n", QString::SkipEmptyParts);
    if(parts.contains("ERROR"))
    {
        qWarning()<<"Protocol error: "<<data;
        QMessageBox::critical(this,tr("ODRFIDCfgM"),tr("Device communication error"));
        return false;
    }

    foreach(const QString& reply, parts)
    {
        if(reply.startsWith("+[="))
        {
            QStringList connParams = reply.mid(3).split(",", QString::SkipEmptyParts);
            if(connParams.size()!=6)
            {
                QMessageBox::warning(this,tr("Connectivity Parameters"),
                                     tr("The device reported an invalid/unrecognized config. Please update the firmware and/or this software"));
                /*defaults*/
                ui->addressSpin->setValue(selected_address);
                ui->baudSpin->setValue(comm->connectionParameter(QModbusClient::SerialBaudRateParameter).toLongLong());
                ui->stopCombo->setCurrentIndex(USART_STOP_BIT_1_IDX);
                ui->parityCombo->setCurrentIndex(CFG_USART_PARITY_NONE);
                ui->timeoutSpin->setValue(10);
            }
            else
            {
                bool convok;
                uint val = connParams.at(0).toUInt(&convok);
                if(convok)
                {
                    ui->addressSpin->setValue(val);
                }
                else
                {
                    qWarning()<<"Config: address "<<connParams.at(0)<<" is invalid";
                    ui->addressSpin->setValue(selected_address);
                }

                val = connParams.at(1).toUInt(&convok);
                if(!convok)
                {
                    val = 0xFF;
                }

                switch(val)
                {
                    case CFG_USART_STOP_1:
                        ui->stopCombo->setCurrentIndex(USART_STOP_BIT_1_IDX);
                        break;
                    case CFG_USART_STOP_1_5:
                        ui->stopCombo->setCurrentIndex(USART_STOP_BIT_1_5_IDX);
                        break;
                    case CFG_USART_STOP_2:
                        ui->stopCombo->setCurrentIndex(USART_STOP_BIT_2_IDX);
                        break;
                    default:
                        qWarning()<<"Config: stop bits "<<connParams.at(1)<<" is invalid";
                        ui->stopCombo->setCurrentIndex(USART_STOP_BIT_1_IDX);
                        break;
                }

                val = connParams.at(2).toUInt(&convok);
                if(!convok)
                {
                    val = 0xFF;
                }

                switch (val)
                {
                    case CFG_USART_PARITY_NONE:
                    case CFG_USART_PARITY_ODD:
                    case CFG_USART_PARITY_EVEN:
                        ui->parityCombo->setCurrentIndex(val);
                        break;
                    default:
                        qWarning()<<"Config: parity "<<connParams.at(2)<<" is invalid";
                        break;
                }

#if 0
                val = connParams.at(3).toUInt(&convok);
                if(!convok)
                {
                    val = 0xFF;
                }

                switch (val)
                {
                    case 7:
                        ui->bitsCombo->setCurrentIndex(USART_BITS_7_IDX);
                        break;
                    case 8:
                        ui->bitsCombo->setCurrentIndex(USART_BITS_8_IDX);
                        break;
                    default:
                        qWarning()<<"Config: bits "<<connParams.at(3)<<" is invalid";
                        break;
                }
#endif

                val = connParams.at(4).toUInt(&convok);
                if(convok)
                {
                    ui->baudSpin->setValue(val);
                }
                else
                {
                    qWarning()<<"Config: baud "<<connParams.at(4)<<" is invalid";
                    ui->baudSpin->setValue(comm->connectionParameter(QModbusClient::SerialBaudRateParameter).toLongLong());
                }

                val = connParams.at(5).toUInt(&convok);
                if(convok)
                {
                    ui->timeoutSpin->setValue(val);
                }
                else
                {
                    qWarning()<<"Config: timeout "<<connParams.at(5)<<" is invalid";
                    ui->timeoutSpin->setValue(10);
                }
            }
            conn_params_changed = false;
        }
        else if(reply.startsWith("+L"))
        {
            //LED
            ui->ledCheck->setChecked(reply.mid(2)=="1");
        }
        else if(reply.startsWith("+Z"))
        {
            ui->buzzCheck->setChecked(reply.mid(2)=="1");
        }
        else if(reply.startsWith("+G="))
        {
            ui->gainSpin->setValue(reply.mid(3).toInt());
        }
        else if(reply.startsWith("+T"))
        {
            ui->intervalSpin->setValue(reply.mid(2).toInt());
        }
        else if(reply.startsWith("+A") && (reply.size()==14) && (reply.at(2)!='='))
        {
            ui->keyARadio->setChecked(true);
            ui->keyEdit->setText(reply.mid(2));
        }
        else if(reply.startsWith("+B") && (reply.size()==14) && (reply.at(2)!='='))
        {
            ui->keyBRadio->setChecked(true);
            ui->keyEdit->setText(reply.mid(2));
        }
        else if(reply.startsWith("+U") && (reply.size()==10))
        {
            ui->ulevGroup->setEnabled(true);
            ui->ulevEdit->setText(reply.mid(2));
        }
        else if(reply.startsWith("+X") && (reply.size()==34))
        {
            ui->plusBox->setEnabled(true);
            ui->keyplusEdit->setText(reply.mid(2));
        }
        else if(reply.startsWith("+SCAN="))
        {
            ui->scanCheck->setChecked(reply.mid(6)!="0");
        }
        else if(reply.startsWith("+F="))
        {
            ui->fmtEdit->setText(reply.mid(3));
        }
        else if(reply!="OK")
        {
            qWarning()<<"Unexpected reply from RFID: "<<reply;
        }
    }

    if(cop==OP_READ)
    {
        statusBar()->showMessage(tr("Config read"), 5000);
    }
    else if(cop==OP_WRITE)
    {
        statusBar()->showMessage(tr("Config written"), 5000);
    }
    return  true;
}

void ODRFIDConfig::reply_failure()
{
    QMessageBox::critical(this,tr("ODRFIDCfgM"),
                          tr("The device is not responding"));
    qApp->quit();
}

void ODRFIDConfig::on_readButton_clicked()
{
    if(comm->state() != QModbusClient::ConnectedState)
    {
        qWarning()<<"Cannot read - not connected";
        return;
    }

    statusBar()->showMessage(tr("Reading config"), -1);
    ui->controlWidget->setEnabled(false);
    cop = OP_READ;
    QByteArray reply;
#if 0
    if(!sendCommand(selected_address, QByteArray("AT+[?;+L?;+Z?;+G?;+T?;+SCAN?;+K?;+F?"), &reply))
    {
        reply_failure();
        return;
    }
    else
    {
        dropBuffers(selected_address);
        if(!processMessage(QString::fromLatin1(reply)))
        {
            qApp->quit();
            return;
        }
        ui->controlWidget->setEnabled(true);
    }
#else
    QList<QByteArray> reqCmds = QList<QByteArray>()
                                <<QByteArray("AT+[?")
                                <<QByteArray("AT+L?")
                                <<QByteArray("AT+Z?")
                                <<QByteArray("AT+G?")
                                <<QByteArray("AT+T?")
                                <<QByteArray("AT+SCAN?")
                                <<QByteArray("AT+K?")
                                <<QByteArray("AT+F?");
    foreach(const QByteArray& req, reqCmds)
    {
        if(!sendCommand(selected_address, req, &reply))
        {
            reply_failure();
            return;
        }
        else
        {
            dropBuffers(selected_address);
            if(!processMessage(QString::fromLatin1(reply)))
            {
                qApp->quit();
                return;
            }
        }
    }
    ui->controlWidget->setEnabled(true);
    statusBar()->showMessage(tr("Config read"), 5000);
#endif
}

void ODRFIDConfig::on_writeButton_clicked()
{
    if(comm->state() != QModbusClient::ConnectedState)
    {
        qWarning()<<"Cannot write - not connected";
        return;
    }

    if(ui->fmtEdit->text().toLatin1().size() > 32) {
        QMessageBox::warning(this, tr("Output Format"), tr("The format line is too long. Maximum allowed length is 32 characters."));
        return;
    }

    ui->controlWidget->setEnabled(false);
    cop = OP_WRITE;

    statusBar()->showMessage(tr("Writing parameters..."), 5000);
    QByteArray reply;
    if(conn_params_changed)
    {
        QAutoCloseMessageBox::warning(5, this, tr("Connectivity Parameters"), tr("New connectivity values (addres, baudrate, etc) will be applied only after a device reset"));

        QString connCMD = QString("AT+[=%1,%2,%3,8,%4,%5")
                          .arg(ui->addressSpin->value())
                          .arg(kStopBitsMap[ui->stopCombo->currentIndex()])
                .arg(ui->parityCombo->currentIndex())
                .arg(ui->baudSpin->value())
                .arg(ui->timeoutSpin->value());
        qWarning()<<"To device: "<<connCMD;
        if(!sendCommand(selected_address, connCMD.toLatin1(), &reply))
        {
            reply_failure();
            return;
        }
        dropBuffers(selected_address);
        if(!processMessage(QString::fromLatin1(reply)))
        {
            on_readButton_clicked();
            return;
        }
        conn_params_changed = false;
    }

    statusBar()->showMessage(tr("Writing parameters..."), 5000);
#if 1
    QStringList paramsToWrite = QStringList()
                                <<QString("AT+L%1").arg(ui->ledCheck->isChecked() ? "1" : "0")
                                <<QString("AT+Z%1").arg(ui->buzzCheck->isChecked() ? "1" : "0")
                                <<QString("AT+G=%1").arg(ui->gainSpin->value())
                                <<QString("AT+T%1").arg(ui->intervalSpin->value())
                                <<QString("AT+SCAN%1").arg(ui->scanCheck->isChecked()?"2":"0")
                                <<QString("AT+K%1%2").arg(ui->keyARadio->isChecked()?"A":"B").arg(ui->keyEdit->text());
    if(ui->ulevGroup->isEnabled())
    {
        paramsToWrite.append(QString("AT+KU") + ui->ulevEdit->text());
    }
    if(ui->plusBox->isEnabled())
    {
        paramsToWrite.append(QString("AT+KX") + ui->keyplusEdit->text());
    }
    paramsToWrite.append(QString("AT+F=%1").arg(ui->fmtEdit->text()));
    paramsToWrite.append(QString("AT+P"));
    foreach(const QString& wrtp, paramsToWrite)
    {
        if(!sendCommand(selected_address, wrtp.toLatin1(), &reply))
        {
            reply_failure();
            return;
        }
        dropBuffers(selected_address);
        if(!processMessage(QString::fromLatin1(reply)))
        {
            QMessageBox::critical(this,tr("ODRFIDCfgM"),
                                  tr("The device response is invalid"));
            on_readButton_clicked();
            return;
        }
    }
#else
    reply.clear();
    QString cmd = QString("AT+L%1;+Z%2;+G=%3;+T%4;+SCAN%5;+K%6;+F=%7;+P")
                  .arg(ui->ledCheck->isChecked() ? "1" : "0")
                  .arg(ui->buzzCheck->isChecked() ? "1" : "0")
                  .arg(ui->gainSpin->value())
                  .arg(ui->intervalSpin->value())
                  .arg(ui->scanCheck->isChecked()?"2":"0")
                  .arg(QString(ui->keyARadio->isChecked()?"A":"B")+ui->keyEdit->text())
                  .arg(ui->fmtEdit->text());

    if(!sendCommand(selected_address, cmd.toLatin1(), &reply))
    {
        reply_failure();
        return;
    }
    dropBuffers(selected_address);
    if(!processMessage(QString::fromLatin1(reply)))
    {
        on_readButton_clicked();
        return;
    }
#endif
    ui->controlWidget->setEnabled(true);
}

void ODRFIDConfig::on_consoleTool_clicked()
{
    if(console->isHidden())
    {
        console->show();
    }
}

void ODRFIDConfig::console_commit(const QString& data)
{
    if(comm->state() != QModbusClient::ConnectedState)
    {
        qWarning()<<"Cannot write - not connected";
        console->display(QString("<font color=\"red\">%1</font>").arg(tr("Not connected")));
        return;
    }

    QByteArray reply;
    if(sendCommand(selected_address, data.toLatin1(), &reply))
    {
        if(data.size() || reply.size()) {
            dropBuffers(selected_address);
        }
        if(reply.size())
        {
            qWarning()<<"Raw reply: "<<reply;
            console->display(QString::fromLatin1(reply).split("\r\n", QString::SkipEmptyParts).join("\n").trimmed());
        }
        else if(data.size())
        {
            console->display(QString("<font color=\"green\">%1</font>").arg(tr("Command completed")));
        }
    }
    else
    {
        console->display(QString("<font color=\"red\">%1</font>").arg(tr("Command error")));
    }
}

void ODRFIDConfig::connParamChanged()
{
    conn_params_changed=true;
}

void ODRFIDConfig::hnd_errorOccurred(QModbusDevice::Error error)
{
    qWarning()<<"MODBUS error: "<<error;
}

void ODRFIDConfig::on_fwTool_clicked()
{
    if(comm->state() != QModbusClient::ConnectedState)
    {
        qWarning()<<"Cannot write - not connected";
        return;
    }

#ifdef OD_NO_DEVELOPER
    if(QMessageBox::question(this, tr("Firmware Upgrade"), tr("Are you sure? The DFU protocol is not compatible with the Modbus RTU."),
                             QMessageBox::Yes|QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
    {
        return;
    }
#endif

    sendCommand(selected_address, QByteArray("AT+X\0"));
    qApp->quit();
}

void ODRFIDConfig::about()
{
    QMessageBox::about(this,tr("ODRFIDCfgM"),
                       tr("OpenDev RFID Configuration Utility v%1 (Modbus)").arg(ODRFIDCFG_VERSION)+
                       QStringLiteral("<br>")+
                       tr("Developed by: Open Development LLC")+
                       QStringLiteral(" ")+QString(QChar(0x00A9))+QStringLiteral("2020")+
                       QStringLiteral("<br>")+
                       QStringLiteral("<a href=\"https://help.unitx.pro\">help.unitx.pro</a><br><br>")+
                       tr("Uses:")+
                       QStringLiteral("<ul>"
                                      "<li><a href=\"https://material.io/resources/icons/\">Material Icons</a> (Apache 2.0 license)</li>"
                                      "</ul>"));
}

/******************MODBUS stuff**********************/

bool ODRFIDConfig::modbusReplyWaitFinished(QModbusReply* r)
{
    QTimer t(this);
    QEventLoop e;
    t.setSingleShot(true);
    t.setInterval(2000);
    QObject::connect(r, &QModbusReply::finished, &e, &QEventLoop::quit);
    QObject::connect(&t, &QTimer::timeout, &e, &QEventLoop::quit);
    t.start();
    e.exec();
    return t.isActive();
}

bool ODRFIDConfig::dropBuffers(int address)
{
    QModbusReply* r = modbusWrite(QModbusDataUnit(QModbusDataUnit::HoldingRegisters, RTU_HOLDREG_DROP_BUF, 1), address);
    if(!r)
    {
        qWarning()<<"Drop buffers command to "<<address<<" failed: "<<comm->errorString();
        return false;
    }

    qWarning()<<"Drop buffers command success";
    delete r;
    return true;
}

bool ODRFIDConfig::getPendingBufferSize(int address, int* output)
{
    if(address==0)
    {
        return false;
    }

    QModbusReply* r = modbusRead(QModbusDataUnit(QModbusDataUnit::InputRegisters, 0, 1), address);
    if(!r)
    {
        qWarning()<<"Get buffer size command to "<<address<<" failed: "<<comm->errorString();
        return false;
    }
    else if(r->result().valueCount()==1)
    {
        *output = r->result().value(0);
        delete r;
        return true;
    }
    else
    {
        qWarning()<<"Get buffer size reply is empty";
        delete r;
        return false;
    }
}

bool ODRFIDConfig::sendCommand(int address, const QByteArray& data, QByteArray* reply)
{
    if(data.size())
    {
        /*
         * Format
         *  16BE: register address (addr in table: 40001+value)
         *  16BE: number of registers
         *  8:    byte count
         *  N:    data
         */
        QByteArray prefix = QByteArray(5, char(0));
        uint8_t* uprefix = (uint8_t*)prefix.data();
        //reg address
        write_uint16_be(GET_HOLDING_ADDRESS(0), uprefix);
        //no. of regs
        write_uint16_be((data.size()+1)>>1, uprefix+2);
        //bute  count
        uprefix[4] = ((data.size()+1)>>1)<<1;
        QModbusReply* r = modbusRaw(QModbusRequest(QModbusPdu::WriteMultipleRegisters,
                                                              (data.size() & 0x01) ? (prefix + data+QByteArray(1,char(0))) : (prefix + data) ), address);
        if(!r)
        {
            if(reply) reply->clear(); // no reply
            return (comm->error() == QModbusRtuSerialMaster::NoError);
        }
        else if(r->error() != QModbusDevice::NoError)
        {
            qWarning()<<"SendCommand "<<data<<" error: "<<r->errorString();
            delete r;
            return false;
        }

        delete r;
    }

    if(reply)
    {
        int replySize = 0;
        if(!getPendingBufferSize(address, &replySize))
        {
            return false;
        }

        if(replySize<=0)
        {
            qWarning()<<"The command did not produce any reply";
            reply->clear();
            return true;
        }
        int replySizeAligned = (((replySize+1)>>1)<<1);

        QByteArray reply_data;
        if(replySizeAligned > 250)
        {
            /* cannot read 126 registers at once */
            QModbusReply* r = modbusRead(QModbusDataUnit(QModbusDataUnit::HoldingRegisters, GET_HOLDING_ADDRESS(0), 125), address);
            if(!r)
            {
                qWarning()<<"Device buffer not read - reply did not finish";
                delete r;
                return false;
            }
            else if(r->error() != QModbusDevice::NoError)
            {
                qWarning()<<"Reply error: "<<r->errorString();
                delete r;
                return false;
            }

            reply_data = r->rawResult().data();
            delete r;

            r = modbusRead(QModbusDataUnit(QModbusDataUnit::HoldingRegisters, GET_HOLDING_ADDRESS(125), (replySizeAligned-250)>>1), address);
            if(!r)
            {
                qWarning()<<"Device buffer not read - reply did not finish";
                delete r;
                return false;
            }
            else if(r->error() != QModbusDevice::NoError)
            {
                qWarning()<<"Reply error: "<<r->errorString();
                delete r;
                return false;
            }

            QByteArray append = r->rawResult().data();
            reply_data += append.mid(1); /* 1st byte is 'COUNT' */
            reply_data[0] = int(reply_data.at(0)) + int(append.at(0));
            delete r;
        }
        else
        {
            QModbusReply* r = modbusRead(QModbusDataUnit(QModbusDataUnit::HoldingRegisters, GET_HOLDING_ADDRESS(0), replySizeAligned>>1), address);
            if(!r)
            {
                qWarning()<<"Device buffer not read - reply did not finish";
                delete r;
                return false;
            }
            else if(r->error() != QModbusDevice::NoError)
            {
                qWarning()<<"Reply error: "<<r->errorString();
                delete r;
                return false;
            }

            reply_data = r->rawResult().data();
            delete r;
        }

        qWarning()<<"Reply for "<<replySize<<" read request: "<<reply_data.toHex().toUpper();
        /*1st byte is the size*/

        if((reply_data.size() < (replySizeAligned+1))||(*((const uint8_t*)reply_data.constData()) != replySizeAligned))
        {
            qWarning()<<"Device buffer read, but the device returned "<<(reply_data.size()-1)<<" of expected "<<replySizeAligned<<" bytes"
                      <<" or "<<int(*((const uint8_t*)reply_data.constData()))<<" != "<<replySizeAligned;
            return false;
        }

        *reply = reply_data.mid(1,replySize);
    }

    return true;
}

/********************* MODBUS helpers from Qt source ************************************/

/*!
    \internal
    \fn quint16 QModbusSerialAdu::calculateCRC(const char *data, qint32 len) const
    Returns the CRC checksum of the first \a len bytes of \a data.
    \note The code used by the function was generated with pycrc. There is no copyright assigned
    to the generated code, however, the author of the script requests to show the line stating
    that the code was generated by pycrc (see implementation).
*/

inline static quint16 crc_reflect(quint16 data, qint32 len)
{
    // Generated by pycrc v0.8.3, https://pycrc.org
    // Width = 16, Poly = 0x8005, XorIn = 0xffff, ReflectIn = True,
    // XorOut = 0x0000, ReflectOut = True, Algorithm = bit-by-bit-fast
    quint16 ret = data & 0x01;
    for (qint32 i = 1; i < len; i++) {
        data >>= 1;
        ret = (ret << 1) | (data & 0x01);
    }
    return ret;
}

inline static quint16 calculateCRC(const char *data, qint32 len)
{
    // Generated by pycrc v0.8.3, https://pycrc.org
    // Width = 16, Poly = 0x8005, XorIn = 0xffff, ReflectIn = True,
    // XorOut = 0x0000, ReflectOut = True, Algorithm = bit-by-bit-fast
    quint16 crc = 0xFFFF;
    while (len--) {
        const quint8 c = *data++;
        for (qint32 i = 0x01; i & 0xFF; i <<= 1) {
            bool bit = crc & 0x8000;
            if (c & i)
                bit = !bit;
            crc <<= 1;
            if (bit)
                crc ^= 0x8005;
        }
        crc &= 0xFFFF;
    }
    crc = crc_reflect(crc & 0xFFFF, 16) ^ 0x0000;
    return (crc >> 8) | (crc << 8); // swap bytes
}

inline static QByteArray makeRTUFrame(const QModbusPdu *const req, int address) {
    QByteArray frame;
    QDataStream ds(&frame, QIODevice::WriteOnly);
    /* Seems calcCRC is already swapped ??? */
    ds.setByteOrder(QDataStream::BigEndian);
    ds << quint8(address) << quint8(req->functionCode());
    ds.writeRawData(req->data().constData(), req->data().size());
    quint16 crc = calculateCRC((const char*)frame.constData(), frame.size());
    ds << crc;
    return frame;
}

enum Coil {
    On = 0xff00,
    Off = 0x0000
};

static QModbusRequest modbusReadRequest(const QModbusDataUnit &data)
{
    if (!data.isValid())
        return QModbusRequest();
    switch (data.registerType()) {
    case QModbusDataUnit::Coils:
        return QModbusRequest(QModbusRequest::ReadCoils, quint16(data.startAddress()), quint16(data.valueCount()));
    case QModbusDataUnit::DiscreteInputs:
        return QModbusRequest(QModbusRequest::ReadDiscreteInputs, quint16(data.startAddress()), quint16(data.valueCount()));
    case QModbusDataUnit::InputRegisters:
        return QModbusRequest(QModbusRequest::ReadInputRegisters, quint16(data.startAddress()), quint16(data.valueCount()));
    case QModbusDataUnit::HoldingRegisters:
        return QModbusRequest(QModbusRequest::ReadHoldingRegisters, quint16(data.startAddress()), quint16(data.valueCount()));
    default:
        break;
    }
    return QModbusRequest();
}

static QModbusRequest modbusWriteRequest(const QModbusDataUnit &data)
{
    switch (data.registerType()) {
    case QModbusDataUnit::Coils: {
        if (data.valueCount() == 1) {
            return QModbusRequest(QModbusRequest::WriteSingleCoil, quint16(data.startAddress()),
                                  quint16((data.value(0) == 0u) ? Coil::Off : Coil::On));
        }
        quint8 byteCount = data.valueCount() / 8;
        if ((data.valueCount() % 8) != 0)
            byteCount += 1;
        quint8 address = 0;
        QVector<quint8> bytes;
        for (quint8 i = 0; i < byteCount; ++i) {
            quint8 byte = 0;
            for (int currentBit = 0; currentBit < 8; ++currentBit)
                if (data.value(address++))
                    byte |= (1U << currentBit);
            bytes.append(byte);
        }
        return QModbusRequest(QModbusRequest::WriteMultipleCoils, quint16(data.startAddress()),
                              quint16(data.valueCount()), byteCount, bytes);
    }   break;
    case QModbusDataUnit::HoldingRegisters: {
        if (data.valueCount() == 1) {
            return QModbusRequest(QModbusRequest::WriteSingleRegister, quint16(data.startAddress()),
                                  data.value(0));
        }
        const quint8 byteCount = data.valueCount() * 2;
        return QModbusRequest(QModbusRequest::WriteMultipleRegisters, quint16(data.startAddress()),
                              quint16(data.valueCount()), byteCount, data.values());
    }   break;
    case QModbusDataUnit::DiscreteInputs:
    case QModbusDataUnit::InputRegisters:
    default:    // fall through on purpose
        break;
    }
    return QModbusRequest();
}

static QString dumpWriteRequest(const QModbusDataUnit& write, int address)
{
    QModbusRequest req = modbusWriteRequest(write);
    return QString::fromLatin1(makeRTUFrame(&req, address).toHex().toUpper());
}

static QString dumpReadRequest(const QModbusDataUnit& read, int address)
{
    QModbusRequest req = modbusReadRequest(read);
    return QString::fromLatin1(makeRTUFrame(&req, address).toHex().toUpper());
}

static inline QString dumpReply(const QModbusReply* r, int address)
{
    QModbusResponse res = r->rawResult();
    return QString::fromLatin1(makeRTUFrame(&res, address).toHex().toUpper());
}

/********************* MODBUS helpers from Qt source ************************************/

QModbusReply* ODRFIDConfig::modbusWrite(const QModbusDataUnit& write, int address)
{
    log->append(QLatin1String(">>"), dumpWriteRequest(write, address));
    QModbusReply* r = comm->sendWriteRequest(write, address);
    if(address == 0)
    {
        delete r; // broadcast replies return immediately
        return nullptr;
    }
    if(!modbusReplyWaitFinished(r))
    {
        delete r; // broadcast replies return immediately
        return nullptr;
    }
    if(r) {
        log->append(QLatin1String("<<"), dumpReply(r, address));
    }
    return r;
}

QModbusReply* ODRFIDConfig::modbusRead(const QModbusDataUnit& read, int address)
{
    log->append(QLatin1String(">>"), dumpReadRequest(read, address));
    QModbusReply* r = comm->sendReadRequest(read, address);
    if(address == 0)
    {
        delete r; // broadcast replies return immediately
        return nullptr;
    }
    if(!modbusReplyWaitFinished(r))
    {
        delete r; // broadcast replies return immediately
        return nullptr;
    }
    if(r) {
        log->append(QLatin1String("<<"), dumpReply(r, address));
    }
    return r;
}

QModbusReply* ODRFIDConfig::modbusRaw(const QModbusRequest& request, int address)
{
    log->append(QLatin1String(">>"), QString::fromLatin1(makeRTUFrame(&request, address).toHex().toUpper()));
    QModbusReply* r = comm->sendRawRequest(request, address);
    if(address == 0)
    {
        delete r; // broadcast replies return immediately
        return nullptr;
    }
    if(!modbusReplyWaitFinished(r))
    {
        delete r; // broadcast replies return immediately
        return nullptr;
    }
    if(r) {
        log->append(QLatin1String("<<"), dumpReply(r, address));
    }
    return r;
}
