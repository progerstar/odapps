#include "hiddfu.h"
#include "odhid_global.h"
#include <ui_hiddfu.h>

#ifdef Q_OS_ANDROID
#include <hidproxy_android.h>
#else
#include <hidproxy_desktop.h>
#endif
#include <endianrw.h>
#include <qautoclosemessagebox.h>
#if HIDDFU_USE_MATERIAL_ICONS
#include <qmaterialfont.h>
#endif

#include <QStringBuilder>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>

#include <QInputDialog>
#include <QDebug>
#include <QTimer>
#include <QSettings>
#include <QTime>
#include <limits>

HIDDfu::HIDDfu(QWidget *parent, Qt::WindowFlags f) : QWidget(parent, f), ui(new Ui::HIDDfu),
    dev_mode(false), caps(NoCapabilities),
    aact(Action_None),
    mode(Boot_Normal), m_baudrate(115200), m_address(-1), reboot_mode(HID_BOOT_APP),
    runner(nullptr), dfu_file(""), auto_accept(false),
    connectTimer(this), connectProgress(nullptr),
    connectRetries(0)
{
    /* Flags are not standalone (ex. Dialog includes Window) - cannot just '&') */
    if((f & Qt::Dialog) == Qt::Dialog) {
        run_mode = Mode_DFU;
        setAttribute(Qt::WA_DeleteOnClose, true);
        setWindowModality(Qt::WindowModal);
    } else if((f & Qt::Window) == Qt::Window) {
        run_mode = Mode_IAPP;
    } else {
        run_mode = Mode_EMB;
    }

    ui->setupUi(this);

#if HIDDFU_USE_MATERIAL_ICONS
    MaterialUI("autorenew", ui->restartTool);
    MaterialUI("system_update_alt", ui->sdfuTool);
    MaterialUI("sd_storage", ui->eStorTool);
    MaterialUI("memory", ui->eepromTool);
    MaterialUI("launch", ui->launchTool);
#else
    ui->restartTool->setIcon(QIcon(":/dfu/images/emblem-synchronizing.ico"));
    ui->sdfuTool->setIcon(QIcon(":/dfu/images/emblem-system.ico"));
    ui->eStorTool->setIcon(QIcon(":/dfu/images/media-flash.ico"));
    ui->eepromTool->setIcon(QIcon(":/dfu/images/media-floppy.ico"));
    ui->launchTool->setIcon(QIcon(":/dfu/images/media-playback-start.ico"));
#endif

    ui->eStorTool->setHidden(true);
    ui->eepromTool->setHidden(true);
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setHidden(true);
    ui->stackedWidget->setCurrentIndex(0);

#ifdef Q_OS_ANDROID
    setStyleSheet("QToolButton{icon-size: 64}");
    ui->infoLabel->setVisible(false);
#endif

    ui->controlWidget->setEnabled(false);
    ui->stackedWidget->setEnabled(false);
    connectTimer.setInterval(500);
    connectTimer.setSingleShot(true);
    connect(&connectTimer, SIGNAL(timeout()), this, SLOT(connectDfu()));
#ifndef OD_NO_DEVELOPER
    ui->statusBar->setProperty("debugBanner", true);
#endif
    setDevMode(false);
}

HIDDfu::~HIDDfu()
{
    if(runner)
    {
        runner->disconnect(this);
        runner->abort();
        delete runner;
        runner = nullptr;
    }
    delete ui;
}

void HIDDfu::setDevMode(bool on)
{
#ifdef OD_NO_DEVELOPER
    (void)on;
    dev_mode = false;
    ui->sdfuTool->setHidden(true);
#else
    dev_mode = on;
    ui->sdfuTool->setHidden(!on);
#endif
}

bool HIDDfu::init()
{
    connectRetries = 0;
    caps = NoCapabilities;
    ui->eStorTool->setHidden(true);
    ui->eepromTool->setHidden(true);
    hiddfu_runner::Mode dfumode = hiddfu_runner::USB;
    if(runner) {
        runner->disconnect(this);
        delete runner;
        runner = nullptr;
    }

    if(m_transport.toLower() == "usb") {
        dfumode = hiddfu_runner::USB;
    } else if(m_transport.toLower() == "rs485") {
        dfumode = hiddfu_runner::UART;
    } else {
        qWarning()<<"Unsupported transport "<<m_transport;
        return false;
    }

    switch (dfumode) {
        case hiddfu_runner::USB: {
            /*hid init is done automatically now*/
            break;
        }
        case hiddfu_runner::UART: {
            if(m_medium.isEmpty()) {
                qWarning()<<"Serial port device is not set!";
                return false;
            }
            if(!m_baudrate) {
                m_baudrate = 115200;
            }
            break;
        }
    }

    runner = new hiddfu_runner(dfumode, this);
    connect(this, &HIDDfu::thread_exec, runner, &hiddfu_runner::exec);
    connect(runner, &hiddfu_runner::result, this, &HIDDfu::thread_done);
    connect(runner, &hiddfu_runner::info, this, &HIDDfu::thread_info);
    connect(runner, &hiddfu_runner::error, this, &HIDDfu::thread_error);
    connect(runner, &hiddfu_runner::progress, this, &HIDDfu::thread_progress);

    connectTimer.stop();
    if(connectProgress)
    {
        connectProgress->close();
        delete connectProgress;
        connectProgress = 0;
    }

    connectProgress = new QProgressDialog(tr("Search DFU Devices"), tr("Cancel"), 0, 10, this);
    connectProgress->setWindowModality(Qt::WindowModal);
    QMetaObject::invokeMethod(&connectTimer, "start", Qt::QueuedConnection);
    //QTimer::singleShot(1000, &connectTimer, SLOT(start()));
    return true;
}

void HIDDfu::connectDfu()
{
    qWarning()<<"Start DFU search";
    if(!runner) {
        //spurious
        connectTimer.stop();
        return;
    }

    if(runner->isConnected()) {
        connectAccept();
        return;
    } else if(connectProgress && connectProgress->wasCanceled()) {
        connectBail();
        return;
    }

    switch(runner->mode()) {
        case hiddfu_runner::USB: {
            emit this->thread_exec(hiddfu_worker::Setup, {{"products", m_products}}, QPrivateSignal());
            break;
        }
        case hiddfu_runner::UART: {
            emit this->thread_exec(hiddfu_worker::Setup, {{"port", m_medium}, {"baudrate", m_baudrate}, {"address", m_address}}, QPrivateSignal());
            break;
        }
    }
}

void HIDDfu::connectAccept()
{
    if(connectProgress) {
        connectProgress->cancel();
        connectProgress->deleteLater();
        connectProgress = nullptr;
    }
    connectTimer.stop();
}

void HIDDfu::connectBail()
{
    qWarning()<<"Give up DFU search";
    connectTimer.stop();
    QMessageBox::warning(msgParent(), tr("Firmware Update"), tr("No DFU capable device available"));
    if(connectProgress) {
        connectProgress->deleteLater();
        connectProgress = nullptr;
    }

    mode = Boot_Normal;
    emit finished();
    switch (run_mode) {
        case Mode_IAPP: QMetaObject::invokeMethod(qApp, &QCoreApplication::quit, Qt::QueuedConnection);break;
        case Mode_DFU: QMetaObject::invokeMethod(this, &HIDDfu::close, Qt::QueuedConnection); break;
        default:break;
    }
}

void HIDDfu::postConnected()
{
    runner->exec(hiddfu_worker::Status, QVariantMap());
}

void HIDDfu::on_dfuDropArea_changed(const QString& file)
{
    qWarning()<<"Selected "<<file;
    ui->stackedWidget->setCurrentIndex(1);
    ui->controlWidget->setEnabled(false);
    flash(file);
}

void HIDDfu::on_launchTool_clicked()
{
    reboot_mode = HID_BOOT_APP;
    runner->exec(hiddfu_worker::Reboot, {{"mode", HID_BOOT_APP},});
}

void HIDDfu::on_restartTool_clicked()
{
    reboot_mode = HID_BOOT_UDFU;
    runner->exec(hiddfu_worker::Reboot, {{"mode", HID_BOOT_UDFU},});
}

void HIDDfu::on_sdfuTool_clicked()
{
#ifndef OD_NO_DEVELOPER
    reboot_mode = HID_BOOT_SDFU;
    runner->exec(hiddfu_worker::Reboot, {{"mode", HID_BOOT_SDFU},});
#endif
}

#ifndef OD_NO_DEVELOPER
void HIDDfu::on_eStorTool_clicked()
{
    bool ok;
    QStringList eops = QStringList()<<tr("Erase")<<tr("Write")<<tr("Read");
    QString op = QInputDialog::getItem(this,tr("Storage Tool"),tr("Operation:"),eops,0,false,&ok);
    if(!ok) return;
    switch(eops.indexOf(op))
    {
        case 0:
        {
            erase_storage();
            break;
        }
        case 1:
        {
            storage_write();
            break;
        }
        case 2:
        {
            storage_read();
            break;
        }
        default:break;
    }
}
#endif

void HIDDfu::on_eepromTool_clicked()
{
    if(QMessageBox::question(this, tr("EEPROM Tool"), tr("Erase EEPROM?")) != QMessageBox::Yes) {
        return;
    }

    eeprom_erase();
}

void HIDDfu::thread_done(int cmd, bool success)
{
    qWarning()<<"Thread finished "<<cmd<<", "<<success;
    switch(cmd) {
        case hiddfu_worker::Setup: {
            if(success) {
                connectAccept();
                postConnected();
            } else {
                qWarning()<<"No DFU devices";

                connectProgress->setValue(++connectRetries);
                if((connectRetries==10) || (connectProgress->wasCanceled())) {
                    connectBail();
                } else {
                    /* repeat search in N ms */
                    connectTimer.start();
                }
            }
            break;
        }
        case hiddfu_worker::Status: {
            if(success) {
                runner->exec(hiddfu_worker::Info, QVariantMap());

                connectTimer.stop();
                if(connectProgress) {
                    connectProgress->setValue(10);
                    connectProgress->deleteLater();
                    connectProgress = nullptr;
                }

                ui->controlWidget->setEnabled(true);
                ui->stackedWidget->setEnabled(true);
            } else {
                connectBail();
            }
            break;
        }
        case hiddfu_worker::Info: {
            if(!success) {
                break;
            }
            switch(aact) {
                case Action_STM: {
                    QMetaObject::invokeMethod(this,"on_sdfuTool_clicked",Qt::QueuedConnection);
                    break;
                }
                default: {
                    if(!dfu_file.isEmpty()) {
                        QMetaObject::invokeMethod(this,"on_dfuDropArea_changed",Qt::QueuedConnection,Q_ARG(QString,dfu_file));
                        dfu_file.clear();
                        dfu_file_info.clear();
                    }
                    break;
                }
            }
            break;
        }
        case hiddfu_worker::CheckFW: {
            if(!success) {
                file_restart();
            } else {
                if(QMessageBox::question(this,tr("Firmware Update"), tr("Update firmware to %1?")
                                         .arg(dfu_file_info.isEmpty() ? "<no info>" : dfu_file_info),
                                         QMessageBox::Yes|QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
                    file_restart();
                    break;
                }

                checkfw_complete();
            }
            break;
        }
        case hiddfu_worker::Flash: {
            if(success) {
                ui->dfuLog->append(tr("Firmware has been written") % QLatin1String("<br>") % tr("Press 'Launch' to reboot the device with the new firmware."));
            }
            ui->progressBar->setHidden(true);
            flash_finished(success);
            break;
        }
#ifndef OD_NO_DEVELOPER
        case hiddfu_worker::FlashStor: {
            if(success) {
                ui->dfuLog->append(tr("The device storage has been programmed") % QLatin1String("<br>") % tr("Press 'Launch' to reboot the device."));
            }
            ui->progressBar->setHidden(true);
            flash_finished(success);
            break;
        }
        case hiddfu_worker::ReadStor: {
            ui->progressBar->setHidden(true);
            ui->controlWidget->setEnabled(true);
            if(success) {
                ui->dfuLog->append(tr("The device storage has been read."));
            }
            break;
        }
        case hiddfu_worker::EraseStor: {
            ui->progressBar->setHidden(true);
            ui->controlWidget->setEnabled(true);
            if(success) {
                ui->dfuLog->append(tr("The device storage has been erased."));
            }
            break;
        }
#endif
        case hiddfu_worker::EraseEEPROM: {
            ui->progressBar->setHidden(true);
            ui->controlWidget->setEnabled(true);
            if(success) {
                ui->dfuLog->append(tr("The device configuration storage has been erased."));
            }
            break;
        }
        case hiddfu_worker::Reboot: {
            if(success) {
                switch(reboot_mode) {
                    case HID_BOOT_APP:
                    default:
                        mode = Boot_Restart;
                        break;
                    case HID_BOOT_SDFU:
                        mode = Boot_Normal;
                        break;
                    case HID_BOOT_UDFU:
                        mode = Boot_DFU;
                        break;
                }

                ui->controlWidget->setEnabled(false);
                switch (run_mode) {
                    case Mode_IAPP:
                        QTimer::singleShot(2000, qApp, SLOT(quit()));
                        break;
                    case Mode_DFU: {
                        QTimer::singleShot(2000, this, [this]{
                            emit this->finished();
                            this->close();
                        });
                        break;
                    }
                    case Mode_EMB: {
                        QTimer::singleShot(2000, this, [this]{
                            emit this->finished();
                        });
                        break;
                    }
                }
            }
            break;
        }
        default:
            break;
    }
}

void HIDDfu::thread_info(int typ, const QVariant& data)
{
    qWarning()<<"Thread info: "<<typ<<", "<<data;

    switch(typ) {
        case hiddfu_worker::ISerial: {
            ui->infoLabel->setText(data.toString());
            break;
        }
        case hiddfu_worker::INotify: {
            ui->dfuLog->append(data.toString());
            break;
        }
        case hiddfu_worker::IDescr: {
            QString descr = data.toString();

            if(descr.endsWith(" E")) {
#ifndef OD_NO_DEVELOPER
                if(dev_mode) {
                    caps = SpiStorage;
                    ui->eStorTool->setHidden(false);
                    qWarning()<<"SPI capabilities detected!";
                }
#endif
                descr.chop(2);
            } else if(descr.endsWith(" S")) {
                caps = EepromStorage;
                ui->eepromTool->setHidden(false);
                qWarning()<<"EEPROM capabilities detected!";

                descr.chop(2);
            }

            ui->statusBar->setText(descr);
            break;
        }
        case hiddfu_worker::IDfuDescr: {
            dfu_file_info = data.toString();
            ui->dfuLog->append(tr("Opened firmware file %1").arg(dfu_file_info.isEmpty() ? QFileInfo(dfu_file).completeBaseName() : dfu_file_info));
            break;
        }
        case hiddfu_worker::IStatus: {
            ui->dfuLog->append(data.toString());
            break;
        }
        default:break;
    }
}

void HIDDfu::thread_error(int code, const QVariant& param)
{
    qWarning()<<"Thread error "<<code<<", "<<param;
    switch(code) {
        case hiddfu_worker::ERR_ERASE: {
            QMessageBox::warning(msgParent(), tr("Firmware Update"), tr("Error erasing old firmware"));
            //flash_finished(false); <- do NOT duplicate
            break;
        }
        case hiddfu_worker::ERR_COMM_NONF: {
            ui->dfuLog->append(tr("Non-fatal flashing error") % QLatin1String(": ") % param.toString());
            break;
        }
        case hiddfu_worker::ERR_COMM: {
            ui->dfuLog->append(tr("Communication error: %1").arg(param.toString()));
            QMessageBox::warning(msgParent(), tr("Firmware Update"), tr("Communication error: %1").arg(param.toString()));
            //flash_finished(false); <- do NOT duplicate
            break;
        }
        case hiddfu_worker::ERR_READ: {
            QMessageBox::warning(msgParent(), tr("Firmware Update"),tr("Error reading from file (%1)").arg(param.toString()));
            //flash_finished(false); <- do NOT duplicate
            break;
        }
        case hiddfu_worker::ERR_FLASH: {
            QMessageBox::warning(msgParent(), tr("Firmware Update"), tr("Error flashing new firmware. Internal error:") % QLatin1String("\n") % param.toString());
            //flash_finished(false); <- to thread_done
            break;
        }
        case hiddfu_worker::ERR_DFU: {
            QMessageBox::warning(msgParent(), tr("Firmware Update"), tr("Firmware package processing error:") % QLatin1String("\n") % param.toString());
            //flash_finished(false); <- to thread_done
            break;
        }
        default: {
            //treat this as error
            QMessageBox::warning(msgParent(), tr("Firmware Update"), tr("Unknown error"));
            //flash_finished(false); <- to thread_done
            break;
        }
    }
}

void HIDDfu::thread_progress(int prog)
{
    qWarning("Thread progress %d", prog);
    if(prog==-1) {
        ui->progressBar->setValue(0);
        ui->dfuLog->insertPlainText(".");
    } else {
        ui->progressBar->setValue(prog);
    }
}

#define WARN_AND_LOG(text) do{QAutoCloseMessageBox::warning(5, this, HIDDfu::tr("Firmware Update"), (text)); ui->dfuLog->append(text);}while(0)

bool HIDDfu::flash(const QString& file)
{
    ui->dfuLog->clear();
    ui->progressBar->setValue(0);

    if(!runner->isConnected()) {
        WARN_AND_LOG(tr("The device is not connected"));
        return false;
    }

    if(runner->isBusy()) {
        WARN_AND_LOG(tr("Busy flashing. Please wait"));
        return false;
    }

    ui->progressBar->setHidden(false);
    dfu_file = file;
    if(auto_accept) {
        checkfw_complete();
    } else {
        runner->exec(hiddfu_worker::CheckFW, {{"file", dfu_file}});
    }

    return true;
}

void HIDDfu::checkfw_complete()
{
    ui->stackedWidget->setCurrentIndex(1);
    ui->controlWidget->setEnabled(false);
    ui->progressBar->setValue(0);
    ui->progressBar->setHidden(false);

    runner->exec(hiddfu_worker::Flash, {{"file", dfu_file}});
}

#ifndef OD_NO_DEVELOPER
bool HIDDfu::erase_storage()
{
    if(!runner->isConnected()) {
        WARN_AND_LOG(tr("The device is not connected"));
        return false;
    }

    if(runner->isBusy()) {
        WARN_AND_LOG(tr("Busy flashing. Please wait"));
        return false;
    }

    ui->stackedWidget->setCurrentIndex(1);
    ui->controlWidget->setEnabled(false);
    runner->exec(hiddfu_worker::EraseStor);
    return true;
}

bool HIDDfu::storage_write()
{
    if(!runner->isConnected()) {
        WARN_AND_LOG(tr("The device is not connected"));
        return false;
    }

    if(runner->isBusy()) {
        WARN_AND_LOG(tr("Busy flashing. Please wait"));
        return false;
    }

    QString storage_file_name = QFileDialog::getOpenFileName(this,tr("Select flash image"),
                                                             QSettings().value(SETTINGS_DFU_STORAGE, "").toString(),
                                                             tr("Flash Images(*.img)"));
    if(storage_file_name.isEmpty()) {
        return false;
    }

    QSettings().setValue(SETTINGS_DFU_STORAGE, QFileInfo(storage_file_name).absolutePath());

    ui->stackedWidget->setCurrentIndex(1);
    ui->controlWidget->setEnabled(false);
    ui->progressBar->setValue(0);
    ui->progressBar->setHidden(false);

    ///TODO: select & append 'index'
    runner->exec(hiddfu_worker::FlashStor, {{"file", storage_file_name}});
    return true;
}

bool HIDDfu::storage_read()
{
    if(!runner->isConnected()) {
        WARN_AND_LOG(tr("The device is not connected"));
        return false;
    }

    if(runner->isBusy()) {
        WARN_AND_LOG(tr("Busy flashing. Please wait"));
        return false;
    }

    QString outputFile = QFileDialog::getSaveFileName(this,tr("Save As"),
                                                      QSettings().value(SETTINGS_DFU_STORAGE, "").toString(),
                                                      tr("Flash Images(*.img)"));
    if(outputFile.isEmpty()) {
        return false;
    }
    if(!outputFile.endsWith(".img",Qt::CaseInsensitive)) {
        outputFile.append(".img");
    }
    QSettings().setValue(SETTINGS_DFU_STORAGE, QFileInfo(outputFile).absolutePath());

    bool ok;
    QString addrString = QInputDialog::getText(this,tr("Select Address"), tr("Flash address:"), QLineEdit::Normal, "0x00000000", &ok);
    if(!ok || addrString.isEmpty()) {
        return false;
    }

    uint32_t address = 1;
    if(addrString.endsWith("kb",Qt::CaseInsensitive)) {
        addrString.chop(2);
        address *= 1024;
    } else if(addrString.endsWith("mb",Qt::CaseInsensitive)) {
        addrString.chop(2);
        address *= 1024*1024;
    }
    address *= addrString.toUInt(&ok, 0);

    if(!ok) {
        WARN_AND_LOG(tr("Address is invalid"));
        return false;
    }

    QString sizeString = QInputDialog::getText(this,tr("Select Size"),
                                               tr("Size (-1 - all):"),
                                               QLineEdit::Normal,"-1",&ok);
    if(!ok) {
        return false;
    }

    qint64 size = 1;
    if(sizeString.endsWith("kb",Qt::CaseInsensitive)) {
        sizeString.chop(2);
        size *= 1024;
    } else if(sizeString.endsWith("mb",Qt::CaseInsensitive)) {
        sizeString.chop(2);
        size *= 1024*1024;
    }
    size *= sizeString.toInt(&ok, 0);
    if(!ok) {
        WARN_AND_LOG(tr("Size is invalid"));
        return false;
    }

    if(size < 0) {
        size = -1;
    }

    ui->stackedWidget->setCurrentIndex(1);
    ui->controlWidget->setEnabled(false);
    ui->progressBar->setValue(0);
    ui->progressBar->setHidden(false);

    qWarning("Request read flash from 0x%08x / %lld", address, size);

    const uint32_t readSize = size < 0 ? std::numeric_limits<uint32_t>::max()
                                       : uint32_t(qMin<qint64>(size, std::numeric_limits<uint32_t>::max()));
    QList<QVariant> locs;
    locs.append(QVariant::fromValue<LocationTypeDef>({address, readSize}));
    QString info = QInputDialog::getText(this,tr("Image Info"), tr("Info (optional):"));
    runner->exec(hiddfu_worker::ReadStor, {{"file", outputFile}, {"locations", locs}, {"info", info}});
    return true;
}
#endif  // OD_NO_DEVELOPER

bool HIDDfu::eeprom_erase()
{
    if(!runner->isConnected()) {
        WARN_AND_LOG(tr("The device is not connected"));
        return false;
    }

    if(runner->isBusy()) {
        WARN_AND_LOG(tr("Busy flashing. Please wait"));
        return false;
    }

    ui->stackedWidget->setCurrentIndex(1);
    ui->controlWidget->setEnabled(false);
    runner->exec(hiddfu_worker::EraseEEPROM);
    return true;
}

void HIDDfu::file_restart()
{
    dfu_file.clear();
    dfu_file_info.clear();
    ui->controlWidget->setEnabled(true);
    ui->progressBar->setHidden(true);
    ui->dfuDropArea->clear();
    ui->stackedWidget->setCurrentIndex(0);
}

void HIDDfu::flash_finished(bool ok)
{
    qWarning("Flash op finished - result %s", ok ? "success" : "failure");

    ui->controlWidget->setEnabled(true);
    ui->dfuDropArea->clear();
    if(!ok) {
        switch (run_mode) {
            case Mode_IAPP: {
                mode = Boot_DFU;
                QMetaObject::invokeMethod(qApp, &QCoreApplication::quit, Qt::QueuedConnection);
                break;
            }
            case Mode_DFU: {
                QMetaObject::invokeMethod(this, &HIDDfu::close, Qt::QueuedConnection);
                break;
            }
            default:break;
        }
        emit finished();
    } else if(auto_accept) {
        QMetaObject::invokeMethod(this, "on_launchTool_clicked");
    }
}

QWidget* HIDDfu::msgParent()
{
    /* When running in embedded mode - could be destroyed BEFORE messagebox -> SEGV */
    return (run_mode == Mode_EMB) ? parentWidget() : this;
}
