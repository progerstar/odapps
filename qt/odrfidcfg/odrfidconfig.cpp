#include "odrfidconfig.h"
#include <ui_odrfidconfig.h>
#include <rfidcardreaderinterface.h>

#include <qmaterialfont.h>
#include <themedetector.h>
#include <endianrw.h>
#include <qi18n.h>

#include <QSerialPortInfo>

#include <QDebug>
#include <QMessageBox>
#include <QStringBuilder>
#include <QTabBar>
#include <QVector>

#define OD_VID       (0x0483)
#define OD_RFID_HID  (0xA26A)

#define HID_REPORT_ID_KBD            0x01
#define HID_REPORT_ID_CTRL           0x02
#define HID_REPORT_CTRL_SIZE         0x3F

namespace {
constexpr int MaxCdcReplyBufferSize = 64 * 1024;

QString fromWideString(const wchar_t* value)
{
    return value ? QString::fromWCharArray(value) : QString();
}
}

typedef enum {
    CMD_INV          = 0x00,
    CMD_INFO         = 0x01,
    CMD_LED          = 0x02,
    CMD_BUZZ         = 0x03,
    CMD_BEEP         = 0x04,
    CMD_LED2         = 0x05,
    CMD_OUTP         = 0x06,
    CMD_INP          = 0x07,
    CMD_UUID         = 0x0F,
    CMD_SCAN_ALL     = 0x10,
    CMD_SCAN_TAG     = 0x11,
    CMD_NEXT_TAG     = 0x12,
    CMD_CHECK_TAG    = 0x13,
    CMD_SEL_TAG      = 0x20,
    CMD_TAG_INFO     = 0x21,
    CMD_READ_TAG     = 0x22,
    CMD_WRITE_TAG    = 0x23,
    CMD_INCREMENT    = 0x24,
    CMD_DECREMENT    = 0x25,
    CMD_WRITEEXT     = 0x26,
    CMD_COMMITPERSO  = 0x27,
    CMD_READEXT      = 0x28,
    CMD_PLUSAUTH     = 0x29,
    CMD_READ_BATCH   = 0x2A,
    CMD_TAG_CHUID    = 0x2F,
    CMD_SET_LED      = 0x30,
    CMD_SET_BUZZ     = 0x31,
    CMD_SET_GAIN     = 0x32,
    CMD_SET_INTERVAL = 0x33,
    CMD_SET_KEY      = 0x34,
    CMD_SET_CAPS     = 0x35,
    CMD_SET_CRNL     = 0x36,
    CMD_SET_WSAK     = 0x37,
    CMD_SET_SCND     = 0x38,
    CMD_SET_BLOCK    = 0x39,
    CMD_SET_FMT      = 0x3A,
    CMD_SET_WRITE    = 0x3F,
    CMD_ANT_STATE    = 0x40,
    CMD_SCAN_MODE    = 0x41,
    CMD_SET_HF       = 0x42,
    CMD_SET_EM       = 0x43,
    CMD_SET_IFACE    = 0x44,
    CMD_EM_PARAMS    = 0x60,
    CMD_EM_CLONE     = 0x61,
    CMD_EM_WRITE     = 0x62,
    CMD_EM_XWRITE    = 0x63,
    CMD_EM_UNROP     = 0x64,
    CMD_REBOOT_UAPP  = 0x80,
    CMD_REBOOT_DFU   = 0x81,
} HID_CMD_ID;

typedef enum {
    HID_PARAM_QUERY = 0,
    HID_PARAM_ON,
    HID_PARAM_OFF,
    HID_PARAM_SWITCH,
    HID_PARAM_DEFAULT,
} HID_CMD_PARAM;

#define HID_SET_PARAM(cmd, cond)  sendCmdParam((cmd), ((cond)?HID_PARAM_ON:HID_PARAM_OFF))
#define HID_SEND_QUERY(cmd)       sendCmdParam((cmd), HID_PARAM_QUERY)

/*
 * Init SEQ:
 *  CMD_INFO -> CMD_SET_(LED/BUZZ/GAIN/INTERVAL/MODE/KEY/CAPS/[CRNL/SAK/UID/BLOCK]_or_FMT/[_HF/_EM])
 */

ODRFIDConfig::ODRFIDConfig(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::ODRFIDConfig),
    handle(0), uhnd(this), devtype(I_INV),
    hTim(-1), reply_timeout(0),
    uart_watcher(this),
    cop(OP_READ), cdc_iter(CDC_OP_IDLE), legacy_block(false), cmd_iteration(0),
    usbmode_cfg(-1), usbmode_active(-1), usbmode_changed(false)
{
    ui->setupUi(this);
    ui->tabWidget->setElideMode(Qt::ElideNone);
    ui->tabWidget->tabBar()->setExpanding(true);
    ui->keyEdit->setSize(6);
    ui->blockSpin->setEnabled(false);
    ui->ulevGroup->setEnabled(false);
    ui->ulevEdit->setSize(4);
    ui->plusBox->setEnabled(false);
    ui->ifaceCombo->setEnabled(false);
    ui->keyplusEdit->setSize(16);
    MaterialUI("logout", ui->actionExit);
    MaterialUI("save", ui->actionWrite);
    MaterialUI("sync", ui->actionRead);
    MaterialUI("system_update", ui->actionUpgrade);
#if 0
    ui->actionSwitch_Interface->setEnabled(false);
    MaterialUI("swap_horiz", ui->actionSwitch_Interface);
#endif
    ui->mainToolBar->setEnabled(false);

    setWindowTitle(tr("ODRFID Configurator v%1").arg(ODRFIDCFG_VERSION));

    uart_watcher.setSingleShot(true);
    uart_watcher.setInterval(1000);
    QObject::connect(&uart_watcher, &QTimer::timeout, this, &ODRFIDConfig::uart_nack);

    QObject::connect(&uhnd, &QSerialPort::readyRead, this, &ODRFIDConfig::uart_readyRead);
    QObject::connect(&uhnd, &QSerialPort::errorOccurred, this, &ODRFIDConfig::uart_error);

    QObject::connect(ui->actionExit, &QAction::triggered, qApp, &QGuiApplication::quit);
    QObject::connect(ui->actionAbout_Qt, &QAction::triggered, this, [this](){
        QMessageBox::aboutQt(this, tr("About Qt"));
    });
    QObject::connect(ui->actionAbout, &QAction::triggered, this, &ODRFIDConfig::about);
}

ODRFIDConfig::~ODRFIDConfig()
{
    if(hTim > 0) {
        killTimer(hTim);
    }

    if(handle) {
        hid_close(handle);
        handle = 0;
    }

    delete ui;
}

QStringList ODRFIDConfig::availableDevices() const
{
    QStringList ret;
    QSet<QString> snRegistry;
    QSet<QString> pathRegistry;

    /* HID devices */
    struct hid_device_info *devs, *cur_dev;
    devs = hid_enumerate(OD_VID, OD_RFID_HID);
    cur_dev = devs;
    while (cur_dev)
    {
        QString vendor  = fromWideString(cur_dev->manufacturer_string);
        QString product = fromWideString(cur_dev->product_string);
        qWarning()<<"HID RFID "<<vendor<<" / "<<product<<" @ "<<fromWideString(cur_dev->serial_number);
        if((vendor=="Open Development")&&(product=="USB HID RFID"))
        {
            // Composite USB profiles also expose the keyboard interface (Generic Desktop page);
            // only the vendor-defined one carries the control protocol.
            if(cur_dev->usage_page == 0x0001) {
                cur_dev = cur_dev->next;
                continue;
            }
            // Several readers may share (or lack) a serial number: the path is the identity.
            const QString path = QString::fromLocal8Bit(cur_dev->path);
            if(!pathRegistry.contains(path))
            {
                QString sn = fromWideString(cur_dev->serial_number);
                ret.append((!sn.isEmpty() ? sn : QString("S/N") ) + QString(" (HID @ %1)")
           #ifdef Q_OS_DARWIN
                           //too long on OSX
                           .arg("osx core")
           #else
                           .arg(path)
           #endif
                           );
                pathRegistry.insert(path);
            }
        }
        else
        {
            qWarning()<<"Not a true OD HID RFID";
        }
        cur_dev = cur_dev->next;
    }
    hid_free_enumeration(devs);

    /* CDC devices */
    snRegistry.clear();
    QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();

    for(const QSerialPortInfo& port : qAsConst(ports)) {
#ifdef Q_OS_WIN
        if(port.portName()=="COM1") {continue;}
#endif
        if((port.vendorIdentifier() == OD_VID) && (port.productIdentifier() == OD_RFID_HID)) {
            // cu - tty -> filter by SN
            if(port.serialNumber().isEmpty()) {
                ret.append(QString("S/N (CDC-AT @ %1)").arg(port.portName()));
            } else if(!snRegistry.contains(port.serialNumber())) {
                ret.append(port.serialNumber() + QString(" (CDC-AT @ %1)").arg(port.portName()));
                snRegistry.insert(port.serialNumber());
            }
        }
    }

    return ret;
}

bool ODRFIDConfig::connect(const QString& name)
{
    if(hTim >= 0) {
        killTimer(hTim);
        hTim = -1;
    }

    if(handle)
    {
        hid_close(handle);
        handle = 0;
    }
    uhnd.close();
    uart_watcher.stop();
    uart_read_buffer.clear();
    devtype = I_INV;

    QRegExp devNameRX("(.*)\\s+\\((HID|CDC-AT) \\@ (.*)\\)");

    if(devNameRX.exactMatch(name)) {
        QString devSN = devNameRX.cap(1);
        QString devType = devNameRX.cap(2);

        if(devType == "HID") {
            if(devSN.isEmpty())
            {
                qWarning() << "Invalid serial number in device description " << name;
                return false;
            }

            const QString hidPath = devNameRX.cap(3);
            if(!hidPath.isEmpty() && (hidPath != QLatin1String("osx core"))) {
                handle = hid_open_path(hidPath.toLocal8Bit().constData());
            } else if(devSN == QLatin1String("S/N")) {
                // Some valid HID devices have no USB serial descriptor.
                handle = hid_open(OD_VID, OD_RFID_HID, nullptr);
            } else {
                QVector<wchar_t> wsn(devSN.size()+1, L'\0');
                wsn[devSN.toWCharArray(wsn.data())]=L'\0';
                handle = hid_open(OD_VID, OD_RFID_HID, wsn.constData());
            }
            if(!handle)
            {
                qWarning()<<"Cannot open HID device "<<devSN;
                return false;
            }

            hid_set_nonblocking(handle,1);
            devtype = I_HID;
            hTim = startTimer(50);
            on_actionRead_triggered();
            return true;
        }

        if(devType == "CDC-AT") {
            QString port = devNameRX.cap(3);
            if(port.isEmpty()) {
                qWarning() << "Invalid com port in device description" << name;
                return false;
            }

            uhnd.blockSignals(true);
            uhnd.close();
            uhnd.setBaudRate(115200);
            uhnd.setPortName(port);
            if(!uhnd.open(QSerialPort::ReadWrite)) {
                qWarning()<<"Cannot open COM device "<<port;
                uhnd.blockSignals(false);
                return false;
            }

            // ATH has its own OK reply. Drain it before starting the ATI
            // transaction so that the read state machine cannot mistake the
            // acknowledgement for the end of the device information reply.
            uhnd.clear();
            uhnd.write("ATH\r");
            uhnd.flush();
            while(uhnd.waitForReadyRead(500)) {
                (void)uhnd.readAll();
            }
            uhnd.clear(QSerialPort::Input);

            devtype = I_CDC;
            uhnd.blockSignals(false);
            on_actionRead_triggered();
            return true;
        }
    } else {
        qWarning()<<"Invalid device description "<<name<<": must be \"S/N (HID / path)\"";
    }
    return false;
}

void ODRFIDConfig::timerEvent(QTimerEvent *e)
{
    (void)e;

    if(!handle) {
        return;
    }

    memset(hid_report_in, 0, sizeof(hid_report_in));
    int ret = hid_read(handle, hid_report_in, HID_REPORT_CTRL_SIZE+1);
    if(ret<0)
    {
        qWarning()<<"HID read error "<<fromWideString(hid_error(handle));
        hid_close(handle);
        handle=0;

        QMessageBox::critical(this,tr("ODRFID Config"),tr("Device communication error"));
        qApp->quit();
        return;
    }

    if(ret && (ret != (HID_REPORT_CTRL_SIZE + 1))) {
        qWarning()<<"Invalid HID report size "<<ret;
        return;
    }

    if(ret && (hid_report_in[0] == HID_REPORT_ID_CTRL))
    {
        reply_timeout = 0;
        qWarning()<<"read HID report of size "<<ret<<" - starts with "<<QByteArray((const char*)hid_report_in,7).toHex().toUpper();
        processHidPacket(hid_report_in[1], read_uint32_le(&hid_report_in[2]), &hid_report_in[1 + 1 + 4]);
    }
    else if(reply_timeout && (--reply_timeout==0))
    {
        uart_nack();
    }
}

void ODRFIDConfig::uart_readyRead()
{
    uart_read_buffer.append(uhnd.readAll());
    if(uart_read_buffer.size() > MaxCdcReplyBufferSize) {
        qWarning()<<"CDC reply buffer limit exceeded";
        uart_read_buffer.clear();
        uhnd.close();
        uart_watcher.stop();
        QMessageBox::critical(this, tr("ODRFID Config"), tr("Device communication error"));
        qApp->quit();
        return;
    }
    int start = 0, end = 0;
    while((end = uart_read_buffer.indexOf("\r\n", start)) >= 0) {
        if(end != start) {
            processCdcPacket(uart_read_buffer.mid(start, end - start));
        }
        start = end + 2;
    }

    if(start == uart_read_buffer.size()) {
        uart_read_buffer.clear();
    } else if(start) {
        uart_read_buffer = uart_read_buffer.mid(start);
    }
}

void ODRFIDConfig::uart_error(QSerialPort::SerialPortError error)
{
    switch(error) {
        case QSerialPort::ReadError:
        case QSerialPort::WriteError:
        case QSerialPort::ResourceError: {
            uhnd.close();
            uart_watcher.stop();
            QMessageBox::critical(this, tr("ODRFID Config"), tr("The device has been disconnected"));
            qApp->quit();
            break;
        }
        default:break;
    }
}

void ODRFIDConfig::uart_nack()
{
    QMessageBox::critical(this, tr("ODRFID Config"), tr("The device is not responding"));
    qApp->quit();
}

void ODRFIDConfig::on_actionRead_triggered()
{
    if((devtype == I_INV) || ((devtype == I_HID) && !handle) || ((devtype == I_CDC) && !uhnd.isOpen())) {
        return;
    }

    dev_fw.clear();
    dev_fw_num = QVersionNumber();
    ui->ulevGroup->setEnabled(false);
    ui->ulevEdit->clear();
    ui->usbModeCombo->setEnabled(false);
    usbmode_cfg = usbmode_active = -1;
    usbmode_changed = false;
    ui->mainToolBar->setEnabled(false);
    cop = OP_READ;
    if(devtype == I_HID) {
        sendPacket(CMD_INFO);
    } else {
        cdc_iter = CDC_OPREAD_INFO;
        cdc_command_queue.clear();
        sendAtCommand("I");
    }
}

void ODRFIDConfig::on_actionWrite_triggered()
{
    if((devtype == I_INV) || ((devtype == I_HID) && !handle) || ((devtype == I_CDC) && !uhnd.isOpen())) {
        return;
    }

    if(!legacy_block) {
        unsigned int maxlen = RFIDCardReaderInterface::cfglineSize(dev_fw_num);
        if(ui->fmtEdit->text().toLatin1().size() > int(maxlen)) {
            QMessageBox::warning(this, tr("Output Format"),
                                 tr("The format line is too long. Maximum allowed length is %1 characters.").arg(maxlen));
            return;
        }
    }

    const bool usbModeEdited = ui->usbModeCombo->isEnabled() && (usbmode_cfg >= 0) &&
                               (ui->usbModeCombo->currentIndex() != usbmode_cfg);
    if(usbModeEdited && (ui->usbModeCombo->currentIndex() == 2) &&
       (QMessageBox::warning(this, tr("USB Profile"),
                             tr("In the \"CCID only\" profile the reader has neither COM port nor HID interfaces, "
                                "so this program will not see it. Use tools/odrfid_usbmode.py (PC/SC or USB) to switch back.\n\n"
                                "Continue?"),
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)) {
        return;
    }
    usbmode_changed = usbModeEdited;

    ui->mainToolBar->setEnabled(false);
    cop = OP_WRITE;
    if(devtype == I_HID) {
        sendPacket(CMD_INFO);
    } else {
        cdc_iter = CDC_OPWRITE_PARAMS;
        cdc_command_queue.clear();
        if(RFIDCardReaderInterface::hasCapabilities(dev_fw_num, RFIDCardReaderInterface::HF_Reader|RFIDCardReaderInterface::EM_Reader)) {
            /* only the combo device can enable/disable the frequencies */
            cdc_command_queue.enqueue(QString("+m%1").arg(ui->emCheck->isChecked() ? 1 : 0));
            cdc_command_queue.enqueue(QString("+h%1").arg(ui->hfCheck->isChecked() ? 1 : 0));
        }
        if(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::IND_Led)) {
            cdc_command_queue.enqueue(QString("+L%1").arg(ui->ledCheck->isChecked() ? 1 : 0));
        }
        if(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::IND_Buzz)) {
            cdc_command_queue.enqueue(QString("+Z%1").arg(ui->buzzCheck->isChecked() ? 1 : 0));
        }
        if(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::HF_Reader)) {
            cdc_command_queue.enqueue(QString("+G=%1").arg(ui->gainSpin->value()));
            cdc_command_queue.enqueue(QString("+K%1%2").arg(ui->keyBRadio->isChecked() ? "B" : "A").arg(ui->keyEdit->text()));

            if(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::MF_Ulev)) {
                cdc_command_queue.enqueue(QString("+KU%1").arg(ui->ulevEdit->text()));
            }

            if(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::MF_Plus)) {
                cdc_command_queue.enqueue(QString("+KX%1").arg(ui->keyplusEdit->text()));
            }
        }

        cdc_command_queue.enqueue(QString("+T%1").arg(ui->intervalSpin->value()));
        cdc_command_queue.enqueue(QString("+SCAN%1").arg(ui->scanCombo->currentIndex() == SCAN_MODE_AUTO ? "2" : "0"));
        cdc_command_queue.enqueue(QString("+F=") % ui->fmtEdit->text());

        if(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::IF_Switch)) {
            cdc_command_queue.enqueue(QString("+IF=%1").arg(ui->ifaceCombo->currentIndex() ? "C" : "H"));
        } else if(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::IF_Combo)) {
            cdc_command_queue.enqueue(QString("+H%1").arg(ui->ifaceCombo->currentIndex() ? "0" : "1"));
        }
        if(usbModeEdited) {
            cdc_command_queue.enqueue(QString("+USBMODE=%1").arg(ui->usbModeCombo->currentIndex()));
        }
        cdc_command_queue.enqueue(QString("+P"));

        sendAtCommand(cdc_command_queue.dequeue());
    }
}

void ODRFIDConfig::on_actionUpgrade_triggered()
{
    if((devtype == I_HID) && handle) {
        sendPacket(CMD_REBOOT_DFU);
        QMetaObject::invokeMethod(qApp, &QCoreApplication::quit, Qt::QueuedConnection);
    } else if((devtype == I_CDC) && uhnd.isOpen()) {
        sendAtCommand("+X");
        QMetaObject::invokeMethod(qApp, &QCoreApplication::quit, Qt::QueuedConnection);
    } else {
        statusBar()->showMessage(tr("No active connection"));
    }
}

#if 0
#define RFID_INTERFACE_USB_HID (1)
#define RFID_INTERFACE_USB_CDC (2)
#define RFID_INTERFACE_UART    (3)
#define RFID_INTERFACE_CAN     (4)

void ODRFIDConfig::on_actionSwitch_Interface_triggered()
{
    if(!handle) {
        return;
    }

    cop = OP_SWITCH;
    sendPacket(CMD_SET_IFACE, QByteArray(1, char(RFID_INTERFACE_USB_CDC)));
}
#endif

void ODRFIDConfig::on_actionSystemLanguage_triggered()
{
    QSettings().setValue(SETTINGS_LANG, QString());
    QMessageBox::information(this, tr("ODRFID Config"), tr("Please restart the application to apply the selected language"));
}

void ODRFIDConfig::on_actionEnglish_triggered()
{
    QSettings().setValue(SETTINGS_LANG, "en");
    QMessageBox::information(this, tr("ODRFID Config"), tr("Please restart the application to apply the selected language"));
}

void ODRFIDConfig::on_actionRussian_triggered()
{
    QSettings().setValue(SETTINGS_LANG, "ru");
    QMessageBox::information(this, tr("ODRFID Config"), tr("Please restart the application to apply the selected language"));
}

void ODRFIDConfig::about()
{
    QMessageBox::about(this, tr("ODRFID Config"),
                       tr("OpenDev RFID Configuration Utility v%1").arg(ODRFIDCFG_VERSION) %
                       QStringLiteral("<br>") %
                       tr("Developed by: Open Development LLC") %
                       QStringLiteral(" ") % QString(QChar(0x00A9)) % QStringLiteral("2022") %
                       QStringLiteral("<br><a href=\"https://help.unitx.pro\">help.unitx.pro</a><br><br>") %
                       tr("Uses:") %
                       QStringLiteral("<ul>"
                                      "<li><a href=\"https://github.com/signal11/hidapi\">signal11/hidapi</a> (BSD-style license)</li>"
                                      "<li><a href=\"https://github.com/GNOME/adwaita-icon-theme\">adwaita-icon-theme</a> (CC-BY-SA 3.0)</li>"
                                      "</ul>"));
}

bool ODRFIDConfig::sendCmdParam(quint8 cmd, quint8 param)
{
    return sendPacket(cmd,QByteArray((const char*)&param,1));
}

bool ODRFIDConfig::sendPacket(quint8 cmd, const QByteArray& param)
{
    qWarning("HID out report: 0x%02x (%s)", cmd, param.toHex().toUpper().constData());

    if(!handle || (param.size() > (HID_REPORT_CTRL_SIZE - 1))) {
        qWarning()<<"Cannot send HID command "<<cmd<<": invalid handle or payload size "<<param.size();
        return false;
    }

    memset(hid_report_out, 0, sizeof(hid_report_out));
    hid_report_out[0] = HID_REPORT_ID_CTRL;
    hid_report_out[1] = cmd;
    if(param.size()) {
        memcpy(hid_report_out + 2, param.constData(), param.size());
    }

    //qWarning()<<"Full HID report ("<<(HID_REPORT_CTRL_SIZE+1)<<QByteArray((const char*)hid_report_out,HID_REPORT_CTRL_SIZE+1).toHex().toUpper();
    const bool written = hid_write(handle, hid_report_out, HID_REPORT_CTRL_SIZE+1) == (HID_REPORT_CTRL_SIZE + 1);
    reply_timeout = written ? 9 : 0;
    if(!written) {
        statusBar()->showMessage(tr("Device communication error"));
        ui->mainToolBar->setEnabled(true);
    }
    return written;
}

bool ODRFIDConfig::sendAtCommand(const QString& cmd, const QByteArray& param)
{
    QByteArray message = QByteArray("AT") + cmd.toLatin1() + param + QByteArray("\r");
    qWarning("CDC>> %s", message.constData());
    cdc_last_cmd = cmd;
    uart_watcher.start();
    const bool written = (uhnd.write(message) == message.size()) && uhnd.flush();
    if(!written) {
        uart_watcher.stop();
        cdc_command_queue.clear();
        cdc_iter = CDC_OP_IDLE;
        statusBar()->showMessage(tr("Device communication error"));
        ui->mainToolBar->setEnabled(true);
    }
    return written;
}

void ODRFIDConfig::processCdcPacket(const QByteArray& data)
{
    /* AT.../ERROR/OK/+CME line w/o CRNL! */
    qWarning("CDC<< %s", data.constData());
    if(cdc_iter != CDC_OP_IDLE) {
        if((data == "OK") || (data == "ERROR")) {
            uart_watcher.stop();
        } else {
            // Timeout measures inactivity, not the total duration of a
            // multi-line command response.
            uart_watcher.start();
        }
    }

    if(cdc_iter == CDC_OP_IDLE) {
        QString reply = QString::fromUtf8(data);
        if(!reply.startsWith("+")) {
            /* Scanned? */
            statusBar()->showMessage(tr("Scanned: %1").arg(reply), 2000);
        } else {
            qWarning("Drop");
        }
        return;
    }

    if(data == "OK") {
        if(cop == OP_READ) {
            if(cdc_iter == CDC_OPREAD_INFO) {
                if(dev_fw_num.isNull()) {
                    qWarning()<<"Firmware version was not found in the device information reply";
                    cdc_command_queue.clear();
                    cdc_iter = CDC_OP_IDLE;
                    ui->mainToolBar->setEnabled(true);
                    statusBar()->showMessage(tr("Unsupported firmware version"));
                    return;
                }

                prepareCdcReadCommands();
                cdc_iter = CDC_OPREAD_PARAMS;
            }

            /* Ok to proceed */
            if(cdc_iter == CDC_OPREAD_PARAMS) {
                if(cdc_command_queue.size()) {
                    sendAtCommand(cdc_command_queue.dequeue());
                } else {
                    /* Done */
                    paramsReadFinalize();
                }
            }
        } else {
            /* Ok to proceed */
            if(cdc_command_queue.size()) {
                sendAtCommand(cdc_command_queue.dequeue());
            } else {
                /* Done */
                if(cdc_iter == CDC_OPWRITE_PARAMS) {
                    if((dev_fw_num.majorVersion() == 2) &&
                       RFIDCardReaderInterface::hasCapabilities(dev_fw_num, RFIDCardReaderInterface::HF_Reader|RFIDCardReaderInterface::EM_Reader)) {
                        /* Bug in the software: +h might need to be repeated */
                        cop = OP_READ;
                        cdc_iter = CDC_OPWRITE_FIN;
                        sendAtCommand("+h?");
                        return;
                    }
                }
                cdc_iter = CDC_OP_IDLE;
                statusBar()->showMessage(tr("Configuration has been written"));
                ui->mainToolBar->setEnabled(true);
                if(usbmode_changed) {
                    usbmode_changed = false;
                    usbmode_cfg = ui->usbModeCombo->currentIndex();
                    offerRestartForUsbMode();
                }
            }
        }
        return;
    }

    if((data == "ERROR") && (cop == OP_READ) && cdc_last_cmd.startsWith(QLatin1String("+USBMODE"))) {
        // Older firmware does not know the USB profiles; carry on without them.
        qWarning("USB profile is not supported by this firmware");
        ui->usbModeCombo->setEnabled(false);
        processCdcPacket("OK");
        return;
    }

    if(data == "ERROR") {
        QMessageBox::critical(this,tr("ODRFID Config"),tr("Device communication error"));
        qApp->quit();
        return;
    }

    if(cop == OP_READ) {
        QString reply = QString::fromUtf8(data);
        switch(cdc_iter) {
            case CDC_OPREAD_INFO: {
                /*
                 * 1st: ....text.... X.X[code] DATE
                 * Optional: S/N <serial>
                 */
                QString parsedVersion;
                QVersionNumber parsedVersionNumber;
                int versionPosition = -1;
                const QString infoLine = QString::fromLatin1(data);
                if(RFIDCardReaderInterface::parseFirmwareVersion(infoLine, parsedVersion,
                                                                 parsedVersionNumber, &versionPosition)) {
                    dev_fw_num = parsedVersionNumber;
                    //full:
                    dev_fw = infoLine.mid(versionPosition);
                    processVersionInfo();
                }
                break;
            }
            case CDC_OPWRITE_FIN: {
                if(reply.startsWith("+h")) {
                    qWarning("Multi-protocol: high freq enabled: %s", data.mid(2).constData());
                    if(cdc_iter == CDC_OPWRITE_FIN) {
                        bool hw_en = reply.mid(2) == "1";
                        if(hw_en != ui->hfCheck->isChecked()) {
                            qWarning("HW2 bug compensation: send 'h' command the second time");
                            cop = OP_WRITE;
                            sendAtCommand(QString("+h%1").arg(ui->hfCheck->isChecked() ? 1 : 0));
                        }
                    } else {
                        ui->hfCheck->setChecked(reply.mid(2) == "1");

                        queryIndicatorsCdc();
                    }
                }
                break;
            }
            case CDC_OPREAD_PARAMS: {
                if(reply.startsWith("+USBMODE=")) {
                    const QStringList modes = reply.mid(9).split(',');
                    bool okCfg = false, okAct = false;
                    const int cfg = modes.value(0).toInt(&okCfg);
                    const int act = modes.value(1).toInt(&okAct);
                    if(okCfg && (cfg >= 0) && (cfg < ui->usbModeCombo->count())) {
                        usbmode_cfg = cfg;
                        usbmode_active = okAct ? act : cfg;
                        ui->usbModeCombo->setCurrentIndex(cfg);
                        ui->usbModeCombo->setEnabled(true);
                        ui->usbModeLabel->setText((usbmode_active != cfg)
                            ? tr("USB Profile (active: %1):").arg(usbmode_active)
                            : tr("USB Profile:"));
                    }
                } else if(reply.startsWith("+L")) {
                    ui->ledCheck->setChecked(reply.mid(2) == "1");
                } else if(reply.startsWith("+Z")) {
                    ui->buzzCheck->setChecked(reply.mid(2) == "1");
                } else if(reply.startsWith("+m")) {
                    qWarning("Multi-protocol: low freq enabled: %s", data.mid(2).constData());
                    ui->emCheck->setChecked(reply.mid(2) == "1");
                } else if(reply.startsWith("+h")) {
                    qWarning("Multi-protocol: high freq enabled: %s", data.mid(2).constData());
                    ui->hfCheck->setChecked(reply.mid(2) == "1");
                } else if(reply.startsWith("+G=")) {
                    ui->gainSpin->setValue(reply.mid(3).toInt());
                } else if((reply.startsWith("+A") || reply.startsWith("+B")) &&
                          (reply.size() == 14) && (reply.at(2) != '=')) {
                    if(reply.at(1) == 'B') {
                        ui->keyBRadio->setChecked(true);
                    } else {
                        ui->keyARadio->setChecked(true);
                    }
                    ui->keyEdit->setText(reply.mid(2));
                } else if(reply.startsWith("+U") && (reply.size() == 10)) {
                    ui->ulevGroup->setEnabled(true);
                    ui->ulevEdit->setText(reply.mid(2));
                }  else if(reply.startsWith("+X") && (reply.size() == 34)) {
                    ui->plusBox->setEnabled(true);
                    ui->keyplusEdit->setText(reply.mid(2));
                } else if(reply.startsWith("+T")) {
                    ui->intervalSpin->setValue(reply.mid(2).toInt());
                } else if(reply.startsWith("+SCAN=")) {
                    if(reply.mid(6) == "0") {
                        ui->scanCombo->setCurrentIndex(SCAN_MODE_MANUAL);
                    } else {
                        ui->scanCombo->setCurrentIndex(SCAN_MODE_AUTO);
                    }
                } else if(reply.startsWith("+H")) {
                    ui->ifaceCombo->setCurrentIndex(reply.mid(2) == "1" ? 0 : 1);
                } else if(reply.startsWith("+IF=")) {
                    ui->ifaceCombo->setCurrentIndex((reply.mid(3) == "C") || (reply.mid(3) == "c") ? 1 : 0);
                } else if(reply.startsWith("+F=")) {
                    ui->fmtEdit->setText(reply.mid(3));
                } else if(!reply.startsWith("+")) {
                    /* Scanned? */
                    statusBar()->showMessage(tr("Scanned: %1").arg(reply), 2000);
                } else {
                    qWarning()<<"Unexpected reply from rfid: "<<data;
                }
                break;
            }
            default:break;
        }
    }
}

void ODRFIDConfig::processHidPacket(uint8_t cmd, uint32_t ret_code, const uint8_t* data)
{
    if((ret_code != 0) && (cmd != CMD_SET_IFACE) && (cmd != CMD_SET_WRITE)) {
        statusBar()->showMessage(tr("Device command %1 failed with error %2").arg(cmd).arg(ret_code));
        ui->mainToolBar->setEnabled(true);
        return;
    }

    switch(cmd)
    {
        case CMD_INFO:
        {
            /*CMD | 4byte code | LEN | DATA*/
            const int firmwareLength = int(data[0]);
            if(firmwareLength > (HID_REPORT_CTRL_SIZE - 6)) {
                qWarning()<<"Invalid firmware description length "<<firmwareLength;
                ui->mainToolBar->setEnabled(true);
                statusBar()->showMessage(tr("Unsupported firmware version"));
                return;
            }
            dev_fw = QString::fromLatin1((const char*)(data+1), firmwareLength);
            qWarning()<<"HID Reader fw: "<<dev_fw;
            QString parsedVersion;
            if(!RFIDCardReaderInterface::parseFirmwareVersion(dev_fw, parsedVersion, dev_fw_num)) {
                qWarning()<<"Firmware version was not found in "<<dev_fw;
                ui->mainToolBar->setEnabled(true);
                statusBar()->showMessage(tr("Unsupported firmware version"));
                return;
            }

            processVersionInfo();

            if(cop == OP_READ) {
                if(RFIDCardReaderInterface::hasCapabilities(dev_fw_num, RFIDCardReaderInterface::HF_Reader|RFIDCardReaderInterface::EM_Reader)) {
                    ui->emCheck->setEnabled(true);
                    ui->hfCheck->setEnabled(true);
                    HID_SEND_QUERY(CMD_SET_EM);
                    break;
                } else if(RFIDCardReaderInterface::hasCapabilities(dev_fw_num, RFIDCardReaderInterface::EM_Reader)) {
                    qWarning("EM only - check low freq");
                    ui->emCheck->setChecked(true);
                    ui->keyGroup->setEnabled(false);
                    ui->ulevGroup->setEnabled(false);
                    ui->gainSpin->setEnabled(false);
                } else {
                    qWarning("HF only - check high freq");
                    ui->hfCheck->setChecked(true);
                }

                queryIndicatorsHid();
            } else /* WRITE */ {
                if(RFIDCardReaderInterface::hasCapabilities(dev_fw_num, RFIDCardReaderInterface::HF_Reader|RFIDCardReaderInterface::EM_Reader)) {
                    /* only the combo device can enable/disable the frequencies */
                    HID_SET_PARAM(CMD_SET_EM, ui->emCheck->isChecked());
                } else if(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::IND_Led)) {
                    HID_SET_PARAM(CMD_SET_LED, ui->ledCheck->isChecked());
                } else {
                    /* no LED -> proceed to buzzer */
                    HID_SET_PARAM(CMD_SET_BUZZ, ui->buzzCheck->isChecked());
                }
            }
            break;
        }
        case CMD_SET_EM:
        {
            /*just in case*/
            if(RFIDCardReaderInterface::hasCapabilities(dev_fw_num, RFIDCardReaderInterface::HF_Reader|RFIDCardReaderInterface::EM_Reader)) {
                qWarning("Multi-protocol: low freq enabled: %d", int(*data));
                ui->emCheck->setChecked(*data != 0);
                if(cop == OP_READ) {
                    HID_SEND_QUERY(CMD_SET_HF);
                } else {
                    qWarning("Multi-protocol: set high freq enabled to %d", int(ui->hfCheck->isChecked()));
                    cmd_iteration = 0;
                    HID_SET_PARAM(CMD_SET_HF, ui->hfCheck->isChecked());
                }
            }
            break;
        }
        case CMD_SET_HF:
        {
            /*just in case*/
            if(RFIDCardReaderInterface::hasCapabilities(dev_fw_num, RFIDCardReaderInterface::HF_Reader|RFIDCardReaderInterface::EM_Reader)) {
                qWarning("Multi-protocol: high freq enabled %d", int(*data));
                if(cop==OP_READ) {
                    ui->hfCheck->setChecked(*data != 0);
                    queryIndicatorsHid();
                } else {
                    /* There is a bug in 2.x firmwares - check execution result */
                    if(!cmd_iteration && (dev_fw_num.majorVersion() == 2) && ((*data != 0) != ui->hfCheck->isChecked())) {
                        /* Again ! */
                        qWarning("BUG compensation - reissue HF command");
                        ++cmd_iteration;
                        HID_SET_PARAM(CMD_SET_HF, ui->hfCheck->isChecked());
                        break;
                    }

                    if(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::IND_Led)) {
                        HID_SET_PARAM(CMD_SET_LED, ui->ledCheck->isChecked());
                    } else {
                        HID_SET_PARAM(CMD_SET_BUZZ, ui->buzzCheck->isChecked());
                    }
                }
            }
            break;
        }
        case CMD_SET_LED:
        {
            //led state - data[0]
            ui->ledCheck->setChecked(*data != 0);
            if(cop==OP_READ) {
                if(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::IND_Buzz)) {
                    HID_SEND_QUERY(CMD_SET_BUZZ);
                } else {
                    startParamsQueryHid();
                }
            } else {
                HID_SET_PARAM(CMD_SET_BUZZ, ui->buzzCheck->isChecked());
            }
            break;
        }
        case CMD_SET_BUZZ:
        {
            ui->buzzCheck->setChecked(*data != 0);
            if(cop == OP_READ) {
                startParamsQueryHid();
            } else {
                if(RFIDCardReaderInterface::hasCapabilities(dev_fw_num, RFIDCardReaderInterface::HF_Reader)) {
                    sendCmdParam(CMD_SET_GAIN, quint8(ui->gainSpin->value()));
                } else {
                    QByteArray intervalData(2, '\0');
                    write_uint16_le(ui->intervalSpin->value(),intervalData.data());
                    sendPacket(CMD_SET_INTERVAL,intervalData);
                }
            }
            break;
        }
        case CMD_SET_GAIN:
        {
            ui->gainSpin->setValue(data[0]);
            if(cop==OP_READ) {
                sendPacket(CMD_SET_INTERVAL, QByteArray(2,'\0'));
            } else {
                QByteArray intervalData(2, '\0');
                write_uint16_le(ui->intervalSpin->value(),intervalData.data());
                sendPacket(CMD_SET_INTERVAL,intervalData);
            }
            break;
        }
        case CMD_SET_INTERVAL:
        {
            ui->intervalSpin->setValue(read_uint16_le(data));
            if(RFIDCardReaderInterface::hasCapabilities(dev_fw_num, RFIDCardReaderInterface::HF_Reader)) {
                if(cop==OP_READ) {
                    sendCmdParam(CMD_SET_KEY, '?');
                } else {
                    QByteArray keyData = QByteArray::fromHex(ui->keyEdit->text().toLatin1());
                    keyData.prepend(ui->keyARadio->isChecked() ? 'A' : 'B');
                    sendPacket(CMD_SET_KEY, keyData);
                }
            } else {
                if(cop == OP_READ) {
                    HID_SEND_QUERY(CMD_SCAN_MODE);
                } else {
                    HID_SET_PARAM(CMD_SCAN_MODE, (ui->scanCombo->currentIndex() == SCAN_MODE_AUTO));
                }
            }
            break;
        }
        case CMD_SET_KEY:
        {
            if(cop == OP_READ) {
                if(data[0] == 'B') {
                    ui->keyBRadio->setChecked(true);
                } else if(data[0] == 'A') {
                    ui->keyARadio->setChecked(true);
                } else {
                    QMessageBox::warning(this,tr("Mifare Classic Key"),tr("The data reported by the device is invalid"));
                    ui->mainToolBar->setEnabled(true);
                    break;
                }

                ui->keyEdit->setText(QString::fromLatin1(QByteArray((const char*)(data+1), 6).toHex().toUpper()));
                if(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::MF_Ulev)) {
                    ui->ulevGroup->setEnabled(true);
                    ui->ulevEdit->setText(QString::fromLatin1(QByteArray((const char*)(data+7), 4).toHex().toUpper()));
                } else {
                    ui->ulevGroup->setEnabled(false);
                }

                if(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::MF_Plus)) {
                    ui->plusBox->setEnabled(true);
                    ui->keyplusEdit->setText(QString::fromLatin1(QByteArray((const char*)(data+7+4), 16).toHex().toUpper()));
                }

                HID_SEND_QUERY(CMD_SCAN_MODE);
            } else {
                switch(data[0])
                {
                    case 'B':
                    {
                        ui->keyBRadio->setChecked(true);
                        ui->keyEdit->setText(QString::fromLatin1(QByteArray((const char*)(data+1), 6).toHex().toUpper()));

                        if(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::MF_Ulev)) {
                            sendPacket(CMD_SET_KEY, QByteArray(1, char('U')) + QByteArray::fromHex(ui->ulevEdit->text().toLatin1()));
                        } else {
                            HID_SET_PARAM(CMD_SCAN_MODE, (ui->scanCombo->currentIndex() == SCAN_MODE_AUTO));
                        }
                        break;
                    }
                    case 'A':
                    {
                        ui->keyARadio->setChecked(true);
                        ui->keyEdit->setText(QString::fromLatin1(QByteArray((const char*)(data+1), 6).toHex().toUpper()));

                        if(dev_fw_num >= QVersionNumber(1, 5, 0))
                        {
                            sendPacket(CMD_SET_KEY, QByteArray(1, char('U')) + QByteArray::fromHex(ui->ulevEdit->text().toLatin1()));
                        }
                        else
                        {
                            HID_SET_PARAM(CMD_SCAN_MODE, (ui->scanCombo->currentIndex() == SCAN_MODE_AUTO));
                        }
                        break;
                    }
                    case 'U':
                    {
                        ui->ulevEdit->setText(QString::fromLatin1(QByteArray((const char*)(data+1), 4).toHex().toUpper()));

                        if(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::MF_Plus)) {
                            sendPacket(CMD_SET_KEY, QByteArray(1, char('X')) + QByteArray::fromHex(ui->keyplusEdit->text().toLatin1()));
                        } else {
                            HID_SET_PARAM(CMD_SCAN_MODE, (ui->scanCombo->currentIndex() == SCAN_MODE_AUTO));
                        }
                        break;
                    }
                    case 'X':
                    {
                        ui->plusBox->setEnabled(true);
                        ui->keyplusEdit->setText(QString::fromLatin1(QByteArray((const char*)(data+1), 16).toHex().toUpper()));
                        HID_SET_PARAM(CMD_SCAN_MODE, (ui->scanCombo->currentIndex() == SCAN_MODE_AUTO));
                        break;
                    }
                    case 'P':
                    {
                        break;
                    }
                    default:
                    {
                        QMessageBox::warning(this, tr("Mifare Classic Key"), tr("The data reported by the device is invalid"));
                        ui->mainToolBar->setEnabled(true);
                        break;
                    }
                }
            }
            break;
        }
        case CMD_SCAN_MODE:
        {
            ui->scanCombo->setCurrentIndex((data[0] == 0) ? SCAN_MODE_MANUAL : SCAN_MODE_AUTO);

            if(cop == OP_READ) {
                HID_SEND_QUERY(CMD_SET_CAPS);
            } else {
                HID_SET_PARAM(CMD_SET_CAPS, ui->capsCheck->isChecked());
            }
            break;
        }
        case CMD_SET_CAPS:
        {
            ui->capsCheck->setChecked(data[0]!=0);
            if(cop==OP_READ) {
                HID_SEND_QUERY(legacy_block ? CMD_SET_CRNL : CMD_SET_FMT);
            } else {
                if(legacy_block) {
                    HID_SET_PARAM(CMD_SET_CRNL, ui->crnlCheck->isChecked());
                } else {
                    QByteArray fmtData = QByteArray(1,char(1)) + ui->fmtEdit->text().toLatin1();
                    if(fmtData.size() < int(1 + RFIDCardReaderInterface::cfglineSize(dev_fw_num))) {
                        fmtData += QByteArray(1, char(0));
                    }
                    sendPacket(CMD_SET_FMT, fmtData);
                }
            }
            break;
        }
        case CMD_SET_CRNL:
        {
            ui->crnlCheck->setChecked(data[0]!=0);
            if(cop==OP_READ) {
                HID_SEND_QUERY(CMD_SET_WSAK);
            } else {
                HID_SET_PARAM(CMD_SET_WSAK, ui->uidsakCheck->isChecked());
            }
            break;
        }
        case CMD_SET_WSAK:
        {
            ui->uidsakCheck->setChecked(data[0]!=0);
            if(cop==OP_READ) {
                HID_SEND_QUERY(CMD_SET_SCND);
            } else {
                HID_SET_PARAM(CMD_SET_SCND, ui->uidRadio->isChecked());
            }
            break;
        }
        case CMD_SET_SCND:
        {
            if(data[0]) ui->uidRadio->setChecked(true);
            else ui->blockRadio->setChecked(true);
            if(cop==OP_READ) {
                sendCmdParam(CMD_SET_BLOCK ,0);
            } else {
                QByteArray blockData(2,'1');
                blockData[1] = static_cast<char>(ui->blockSpin->value());
                sendPacket(CMD_SET_BLOCK, blockData);
            }
            break;
        }
        case CMD_SET_BLOCK:
        case CMD_SET_FMT:
        {
            if(cmd == CMD_SET_BLOCK) {
                ui->blockSpin->setValue(data[0]);
            } else {
                size_t data_len = 0;
                while((data_len < 58) && data[data_len]) {
                    qWarning()<<"Format data at "<<data_len<<": "<<QString("%1 [0x%2]").arg(char(data[data_len])).arg(int(data[data_len]),2,16);
                    ++data_len;
                }
                ui->fmtEdit->setText(QString::fromLatin1((const char*)data, data_len));
            }

            if(cop==OP_READ) {
                //read finished
                paramsReadFinalize();
            } else {
                if(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::IF_Switch)) {
                    sendPacket(CMD_SET_IFACE, QByteArray(1, char(ui->ifaceCombo->currentIndex() ? RFIDCardHardwareReader::iCDC : RFIDCardHardwareReader::iHID)));
                } else {
                    sendPacket(CMD_SET_WRITE);
                }
            }
            break;
        }
        case CMD_SET_IFACE:
        {
            if(ret_code == 0) {
                ui->ifaceCombo->setCurrentIndex((*data == RFIDCardHardwareReader::iCDC) ? 1 : 0);
            }
            if(cop == OP_WRITE) {
                sendPacket(CMD_SET_WRITE);
            }
            break;
        }
        case CMD_SET_WRITE:
        {
            if(ret_code == 0) {
                //write finished
                statusBar()->showMessage(tr("Configuration has been written"));
                ui->mainToolBar->setEnabled(true);
            } else {
                statusBar()->showMessage(tr("Config write error %1").arg(ret_code));
            }
            break;
        }
        default:
        {
            qWarning("Unhandled HID command %d", int(cmd));
            break;
        }
    }
}

void ODRFIDConfig::processVersionInfo()
{
    ui->deviceInfoLabel->setText(tr("Connected to: ODRFID version ") % dev_fw);
    statusBar()->showMessage(tr("Connected to ODRFID v%1").arg(dev_fw));
    legacy_block = (dev_fw_num.majorVersion()==1) && (dev_fw_num.minorVersion()<2);
    ui->outputFormatSelector->setCurrentIndex(legacy_block ? 1 : 2);
    ui->ulevGroup->setEnabled(dev_fw_num >= QVersionNumber(1,5,0));

    qWarning()<<"Parsed HW version "<<dev_fw_num;
    /* Some devices do not have LEDs */
    ui->ledCheck->setEnabled(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::IND_Led));
    /* Older do not support iface switching */
    if(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::IF_Switch)) {
        ui->ifaceCombo->clear();
        ui->ifaceCombo->addItems({tr("HID"), tr("CDC")});
        ui->ifaceCombo->setEnabled(true);
    } else if(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::IF_Combo)) {
        ui->ifaceCombo->clear();
        ui->ifaceCombo->addItems({tr("Keyboard Enabled"), tr("Keyboard Blocked")});
        ui->ifaceCombo->setEnabled(true);
    } else {
        ui->ifaceCombo->setEnabled(false);
    }

    if(!RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::HF_Reader)) {
        ui->authTab->setEnabled(false);
    }
}

void ODRFIDConfig::queryIndicatorsHid()
{
    if(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::IND_Led)) {
        HID_SEND_QUERY(CMD_SET_LED);
    } else if(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::IND_Buzz)) {
        qWarning("No LED support - send buzz request");
        HID_SEND_QUERY(CMD_SET_BUZZ);
    } else{
        qWarning("No LED/BUZZ support - skip indicators");
        startParamsQueryHid();
    }
}

void ODRFIDConfig::prepareCdcReadCommands()
{
    cdc_command_queue.clear();

    /* Disable CAPS - even if supported, no way to modify */
    ui->capsCheck->setChecked(false);
    ui->capsCheck->setEnabled(false);

    if(RFIDCardReaderInterface::hasCapabilities(dev_fw_num, RFIDCardReaderInterface::HF_Reader|RFIDCardReaderInterface::EM_Reader)) {
        ui->emCheck->setEnabled(true);
        ui->hfCheck->setEnabled(true);
        cdc_command_queue.enqueue("+m?");
        cdc_command_queue.enqueue("+h?");
    } else if(RFIDCardReaderInterface::hasCapabilities(dev_fw_num, RFIDCardReaderInterface::EM_Reader)) {
        qWarning("EM only - check low freq");
        ui->emCheck->setChecked(true);
        ui->keyGroup->setEnabled(false);
        ui->ulevGroup->setEnabled(false);
        ui->gainSpin->setEnabled(false);
    } else {
        qWarning("HF only - check high freq");
        ui->hfCheck->setChecked(true);
    }

    if(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::IND_Led)) {
        cdc_command_queue.enqueue("+L?");
    }
    if(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::IND_Buzz)) {
        cdc_command_queue.enqueue("+Z?");
    }
    if(RFIDCardReaderInterface::hasCapabilities(dev_fw_num, RFIDCardReaderInterface::HF_Reader)) {
        cdc_command_queue.enqueue("+G?");
    }

    cdc_command_queue.enqueue("+T?");
    if(RFIDCardReaderInterface::hasCapabilities(dev_fw_num, RFIDCardReaderInterface::HF_Reader)) {
        cdc_command_queue.enqueue("+K?");
    }

    cdc_command_queue.enqueue("+SCAN?");

    if(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::IF_Combo)) {
        cdc_command_queue.enqueue("+H?");
    } else if(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::IF_Switch)) {
        cdc_command_queue.enqueue("+IF?");
    }

    cdc_command_queue.enqueue("+F?");
    if(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::USB_Profile)) {
        cdc_command_queue.enqueue("+USBMODE?");
    }
    qWarning("Will send %d queries", cdc_command_queue.size());
}

void ODRFIDConfig::queryIndicatorsCdc()
{
    if(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::IND_Led)) {
        sendAtCommand("+L?");
    } else if(RFIDCardReaderInterface::hasCapability(dev_fw_num, RFIDCardReaderInterface::IND_Buzz)) {
        qWarning("No LED support - send buzz request");
        sendAtCommand("+Z?");
    } else {
        qWarning("No LED/BUZZ support - skip indicators");
        startParamsQueryCdc();
    }
}

void ODRFIDConfig::startParamsQueryHid()
{
    if(RFIDCardReaderInterface::hasCapabilities(dev_fw_num, RFIDCardReaderInterface::HF_Reader)) {
        // Query gain
        sendCmdParam(CMD_SET_GAIN, 0);
    } else {
        // Query interval
        sendPacket(CMD_SET_INTERVAL, QByteArray(2,'\0'));
    }
}

void ODRFIDConfig::startParamsQueryCdc()
{
    if(RFIDCardReaderInterface::hasCapabilities(dev_fw_num, RFIDCardReaderInterface::HF_Reader)) {
        // Query gain
        sendAtCommand("+G?");
    } else {
        // Query interval
        sendAtCommand("+T?");
    }
}

void ODRFIDConfig::paramsReadFinalize()
{
    statusBar()->showMessage(tr("Config read"));
    ui->mainToolBar->setEnabled(true);
    cdc_iter = CDC_OP_IDLE;
}

void ODRFIDConfig::offerRestartForUsbMode()
{
    if(QMessageBox::question(this, tr("USB Profile"),
                             tr("The USB profile is stored and will be applied after the reader restarts. Restart it now?"),
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes) {
        ui->usbModeLabel->setText(tr("USB Profile (restart required):"));
        return;
    }

    uart_watcher.stop();
    uhnd.write("AT+Q\r");
    uhnd.flush();
    uhnd.waitForBytesWritten(500);
    uhnd.close();
    QMessageBox::information(this, tr("USB Profile"),
                             tr("The reader is restarting. Start the program again once it has reconnected."));
    qApp->quit();
}
