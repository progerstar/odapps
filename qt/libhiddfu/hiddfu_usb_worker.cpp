#include "hiddfu_usb_worker.h"

#ifdef Q_OS_ANDROID
#include <hidproxy_android.h>
#else
#include <hidproxy_desktop.h>
#endif

#include <endianrw.h>

#include <QDebug>
#include <QFileInfo>
#include <math.h>

hiddfu_usb_worker::hiddfu_usb_worker(QObject* parent) : hiddfu_worker(parent), _io(nullptr)
{

}

hiddfu_usb_worker::~hiddfu_usb_worker()
{
    if(_io) {
        _io->close();
        delete _io;
        _io = nullptr;
    }
}

void hiddfu_usb_worker::setup(const QVariantMap& params)
{
    if(_io) {
        _io->close();
        delete _io;
        _io = nullptr;
    }

    QStringList products = params.value("products").toStringList();
    if(products.isEmpty()) {
        emit error(hiddfu_worker::ERR_COMM, tr("Please specify a device to connect to"));
        emit finished(hiddfu_worker::Setup, false);
        return;
    }


#ifdef Q_OS_ANDROID
    _io = new HIDProxyAndroid(this);
#else
    _io = new HIDProxyDesktop(this);
#endif

    QStringList devs;

    for(const QString& product: qAsConst(products)) {
        devs.append(_io->availableHidDevices("Open Development", product));
    }

    for(const QString& dev : qAsConst(devs)) {
        qWarning()<<"HID DFU "<<dev;
        if(_io->setDevice(dev) && postConnectionRoutine(dev)) {
            emit finished(Setup, true);
            return;
        }
    }

    _io->close();
    delete _io;
    _io = nullptr;
    emit finished(Setup, false);
}

void hiddfu_usb_worker::getstatus()
{
    if(!_io) {
        emit error(hiddfu_worker::ERR_COMM, tr("Cannot get status - no handle"));
        emit finished(hiddfu_worker::Status, false);
        return;
    }

    DFUStatus status;
    if(!getStatus(&status)) {
        emit error(hiddfu_worker::ERR_COMM, tr("Status request error"));
        emit finished(hiddfu_worker::Status, false);
        return;
    }

    emit info(IStatus, QVariant::fromValue(status));
    emit finished(hiddfu_worker::Status, true);
}

void hiddfu_usb_worker::getinfo()
{
    if(!_io) {
        emit error(hiddfu_worker::ERR_COMM, tr("Cannot get device info - no handle"));
        emit finished(hiddfu_worker::Info, false);
        return;
    }

    QByteArray hid_info_buf;
    if(!_io->getFeature(32, hid_info_buf, HID_ID_INFO)) {
        qWarning()<<"GET INFO failed";
        emit error(hiddfu_worker::ERR_COMM, tr("GET INFO error"));
        emit finished(hiddfu_worker::Info, false);
        return;
    }
    emit info(hiddfu_worker::IDescr, QString::fromLatin1(hid_info_buf.constData(), strnlen(hid_info_buf.constData(), 32)));
    emit finished(hiddfu_worker::Info, true);
}

static bool hid_check_has_bulk(HIDProxy* _io) {
    if((_io->interfaceCount() != 2) || (_io->interfaceClass(1).bClass != 0xFF)) {
        qWarning("Expected 2 interfaces (got %d) or wrong class", _io->interfaceCount());
        return false;
    }
    return true;
}

bool hiddfu_usb_worker::flash_bulk(QFile* iofile, uint32_t fw_start, uint32_t fw_size)
{
    /*
     * Flash_bulk always has a backup -> make all errors non-fatal
     *
     * check if BULK interace is present and accessible
     */
    if(!hid_check_has_bulk(_io)) {
        return false;
    }

    /************************************************************************************/
    //flash page size is at least 1kB (for F0, L4) - write in 512 chunks

    bool first_try = true;
    uint8_t cmd_buf[5];
    DFUStatus status;
    char fw_data[512];

    uint32_t bytes_written = 0;
    int read;

    // erase
    cmd_buf[0] = HID_ERASE;
    write_uint32_le(fw_size, cmd_buf+1);
    if(!_io->sendPacket(cmd_buf, 5, HID_ID_CMD)) {
        qWarning()<<"ERASE cmd returned an error";
        emit error(ERR_COMM, QVariant("erase"));
        goto fail_abort;
    }

    qDebug()<<"Start erasing";
    emit info(IStatus, tr("Erasing") + QLatin1String("..."));

    do {
        HIDProxy::qSleep(100);
        emit progress(-1);
        if(force_stop || !getStatus(&status)) {
            emit error(ERR_COMM, QLatin1String("GET_STATUS"));
            goto fail_abort;
        }
    }while((status.state & SB_Erasing) && (status.error == OD_DFU_ERROR_OK));

    if(status.error != OD_DFU_ERROR_OK) {
        qWarning()<<"Error erasing: "<<status.error;
        emit error(ERR_ERASE, QVariant());
        goto fail_abort;
    }

    qDebug()<<"Erased";
    //dumpStatus(&status);

    qDebug()<<"Start flashing";
    emit info(IStatus, tr("Flashing") + QLatin1String("..."));
    emit progress(0);

    if(fw_size>512) {
        //first write everything but the first chunk

        //set address to 512

        cmd_buf[0] = HID_SET_ADDR;
        write_uint32_le(512, cmd_buf+1);
        if(!_io->sendPacket(cmd_buf, 5, HID_ID_CMD)) {
            qWarning()<<"SET ADDR cmd returned an error";
            emit error(ERR_COMM, QLatin1String("SET_ADDRESS"));
            goto fail_abort;
        }

        qDebug()<<"address 512 set";
        if(!verifyAddressSet(status, 512, HIDDFU_DEFAULT_TIMEOUT)) {
            goto fail_abort;
        }
        //dumpStatus(&status);

        iofile->seek(fw_start + 512);

        while(!iofile->atEnd()) {
            if(force_stop) {
                emit error(ERR_COMM, tr("aborted"));
                goto fail_abort;
            }

            read = iofile->read(fw_data, 512);
            if(read < 0) {
                emit error(ERR_READ, iofile->errorString());
                goto fail_abort;
            } else if(read==0) {
                qWarning("EOF");
                break;
            }

            if(!_io->rawSend(1, QByteArray(fw_data, read), 5000)) {
                if(first_try) {
                    emit error(ERR_COMM_NONF, tr("The fast flash interface is not supported - fallback to the slower one"));
                    return false;
                }
                qWarning("DATA bulk transfer error");
                emit error(ERR_COMM_NONF, QLatin1String("data bulk transfer"));
                goto fail_cont;
            }
            first_try = false;

            do {
                HIDProxy::qSleep(50);
                if(force_stop || !getStatus(&status)) {
                    emit error(ERR_COMM, QLatin1String("GET_STATUS"));
                    goto fail_abort;
                }
            } while ((status.state & SB_Writing) && (status.error==OD_DFU_ERROR_OK));

            if(status.error != OD_DFU_ERROR_OK) {
                qWarning()<<"Error writing: "<<status.error;
                emit error(ERR_FLASH, tr("stopped flashing at %1 - internal error %2")
                           .arg(qRound(100. * bytes_written / fw_size))
                           .arg(dfu_error_string(status.error)));
                goto fail_cont;
            }

            qDebug("Write of  %u:%u finished", bytes_written, read);

            bytes_written += read;
            emit progress(qRound(100. * bytes_written / fw_size));
        }
    }

    //ZLP - the new proxy API takes care of this :))

    do {
        HIDProxy::qSleep(50);
        if(force_stop || !getStatus(&status)) {
            emit error(ERR_COMM, QLatin1String("GET_STATUS"));
            goto fail_abort;
        }
    } while ((status.state & SB_Writing) && (status.error == OD_DFU_ERROR_OK));

    if(status.error != OD_DFU_ERROR_OK) {
        qWarning()<<"Error writing: "<<status.error;
        emit error(ERR_FLASH, tr("stopped flashing at %1 - internal error %2")
                   .arg(qRound(100. * bytes_written / fw_size))
                   .arg(dfu_error_string(status.error)));
        goto fail_cont;
    }

    //set address to 0

    cmd_buf[0] = HID_SET_ADDR;
    write_uint32_le(0, cmd_buf+1);
    if(!_io->sendPacket(cmd_buf, 5, HID_ID_CMD)) {
        qWarning()<<"SET ADDR cmd returned an error";
        emit error(ERR_COMM, QLatin1String("SET_ADDRESS"));
        goto fail_abort;
    }
    if(!verifyAddressSet(status, 0, HIDDFU_DEFAULT_TIMEOUT)) {
        goto fail_abort;
    }

    //write the first chunk
    iofile->seek(fw_start);
    read = iofile->read((char*)fw_data, 512);
    if(read < 0) {
        emit error(ERR_READ, iofile->errorString());
        goto fail_abort;
    }

    if(!_io->rawSend(1, QByteArray(fw_data, read), 5000)) {
        if(first_try) {
            emit error(ERR_COMM_NONF, tr("The fast flash interface is not supported - fallback to the slower one"));
            return false;
        }
        qWarning()<<"DATA bulk transfer error";
        emit error(ERR_COMM_NONF, QLatin1String("data bulk transfer"));
        goto fail_cont;
    }

    do {
        HIDProxy::qSleep(50);
        if(force_stop || !getStatus(&status)) {
            emit error(ERR_COMM, QLatin1String("GET_STATUS"));
            goto fail_abort;
        }
    } while ((status.state & SB_Writing) && (status.error == OD_DFU_ERROR_OK));

    if(status.error != OD_DFU_ERROR_OK) {
        qWarning()<<"Error writing: "<<status.error;
        emit error(ERR_FLASH, tr("stopped flashing at %1 - internal error %2")
                   .arg(qRound(100. * bytes_written / fw_size))
                   .arg(dfu_error_string(status.error)));
        goto fail_abort;
    }
    emit progress(100);

    qDebug("Flashed OK (bulk)");
    emit finished(hiddfu_worker::Flash, true);
    return true;

fail_abort:
    emit finished(hiddfu_worker::Flash, false);
    return true;

fail_cont:
    return false;
}

#ifndef OD_NO_DEVELOPER
hiddfu_worker::FlashResult hiddfu_usb_worker::flash_storage_bulk(QFile* iofile, uint32_t size)
{
    /*
     * check if BULK interace is present and accessible
     */
    if(!hid_check_has_bulk(_io)) {
        return FFallback;
    }

    /* Address already set in the calling function */

    DFUStatus status;
    char fw_data[512];

    uint32_t bytes_written = 0;
    const uint32_t write_size = size;
    int read;

    //write cmd is already sent and succeeded at this point
    while(size) {
        if(force_stop) {
            return FError;
        }

        read = iofile->read(fw_data, qMin(uint32_t(512), size));
        if(read < 0) {
            emit error(ERR_READ, iofile->errorString());
            return FError;
        } else if(read == 0) {
            emit error(ERR_READ, tr("Unexpected end of file"));
            return FError;
        }

        if(!_io->rawSend(1, QByteArray(fw_data, read), 5000)) {
            qWarning("DATA bulk transfer error");
            emit error(ERR_COMM_NONF, QLatin1String("data bulk transfer"));
            return bytes_written ? FError : FFallback;
        }

        do {
            HIDProxy::qSleep(50);
            if(force_stop ||!getStatus(&status)) {
                emit error(ERR_COMM, QLatin1String("GET_STATUS"));
                return FError;
            }
        } while ((status.state & SB_Writing) && (status.error == OD_DFU_ERROR_OK));

        if(status.error != OD_DFU_ERROR_OK) {
            qWarning()<<"Error writing: "<<status.error;
            emit error(ERR_FLASH, tr("stopped flashing at %1 - internal error %2")
                       .arg(qRound(100. * bytes_written / write_size))
                       .arg(dfu_error_string(status.error)));
            return FError;
        }

        qDebug("Write of %u:%u finished", bytes_written, read);

        bytes_written += read;
        size -= read;
        emit progress(qRound(100. * bytes_written / write_size));
    }

    //ZLP - the new proxy API takes care of this
    emit progress(100);

    qDebug()<<"SPI Flashed OK (bulk)";

    emit info(INotify, tr("Device storage has been successfully programmed."));
    return FSuccess;
}

hiddfu_worker::FlashResult hiddfu_usb_worker::flash_storage_read(QFile* iofile, uint32_t size)
{
    /*
     * check if BULK interace is present and accessible
     */
    if(!hid_check_has_bulk(_io)) {
        return FFallback;
    }

    /*
     * Address is already set
     */
    int read = 0;
    bool success;

    uint8_t cmd[5];
    cmd[0] = HID_READ_STORAGE;
    write_uint32_le(size, cmd+1);
    if(!_io->sendPacket(cmd, 5, HID_ID_CMD)) {
        qWarning("READ cmd returned an error");
        emit error(ERR_COMM, QVariant("prepare download"));
        return FError;
    }

    while(uint32_t(read) < size) {
        if(force_stop) {
            return FError;
        }

        QByteArray data = _io->rawReceive(1, qMin<int>(512, size-read), 5000, &success);
        if(!success || data.isEmpty()) {
            qWarning("BULK read error");
            emit error(ERR_COMM, QVariant("cannot read from device"));
            return FError;
        }

        iofile->write(data);
        read += data.size();
        qDebug("Received  %d of %u bytes", read, size);
        emit progress(qRound(100. * read / size));
    }

    emit progress(100);
    emit info(INotify, tr("Device storage data has been read and saved"));
    return FSuccess;
}
#endif  // OD_NO_DEVELOPER

#define FAIL_ABORT(reason) do{\
    emit error(ERR_COMM, (reason));\
    emit finished(hiddfu_worker::Flash, false);\
    return;}while(0)

#define UPDATE_STATUS() do{\
    if(!getStatus(&status)) {\
        FAIL_ABORT(QLatin1String("GET_STATUS"));\
    }} while(0)

void hiddfu_usb_worker::flash(const QString& dfu_file)
{
    if(!_io) {
        FAIL_ABORT(tr("No active connection"));
    }

    QFile iofile(dfu_file);
    if(!iofile.open(QFile::ReadOnly)) {
        FAIL_ABORT(tr("Cannot read from %1: %2").arg(dfu_file, iofile.errorString()));
    }

    if(!preFlashRoutine(&iofile)) {
        FAIL_ABORT(tr("the firmware file is invalid"));
    }

    if(!preOperationRoutine(Flash)) {
        return;
    }

    uint8_t cmd_buf[6];

    uint32_t fw_start = iofile.pos();
    uint32_t fw_size = iofile.size() - fw_start;
    DFUStatus status;
    force_stop=false;

    if(flash_bulk(&iofile, fw_start, fw_size)) {
        return;
    }
    qWarning("Bulk flashing is not supported on this OS or failed");

    // erase
    cmd_buf[0] = HID_ERASE;
    write_uint32_le(fw_size, cmd_buf+1);
    if(!_io->sendPacket(cmd_buf, 5, HID_ID_CMD)) {
        qWarning("ERASE cmd returned an error");
        FAIL_ABORT(tr("erase"));
    }

    qDebug()<<"Start erasing";
    emit info(IStatus, tr("Erasing") + QLatin1String("..."));

    do {
        if(force_stop) {
            FAIL_ABORT(tr("aborted"));
        }
        HIDProxy::qSleep(100);
        emit progress(-1);
        UPDATE_STATUS();
    } while((status.state & SB_Erasing) && (status.error == OD_DFU_ERROR_OK));

    if(status.error != OD_DFU_ERROR_OK) {
        qWarning()<<"Error erasing: "<<status.error;
        FAIL_ABORT("cannot erase the old firmware");
    }

    qDebug("Erased");
    //dumpStatus(&status);

    qDebug("Start flashing");
    emit info(IStatus, tr("Flashing") + QLatin1String("..."));
    emit progress(0);

    uint32_t current_fw_address = 0;
    QByteArray fw_data(63, char(0));
    uint32_t bytes_written = 0;
    int read;

    if(fw_size > 56) {
        //first write everything but the first chunk

        //set address to 56

        current_fw_address = 56;
        cmd_buf[0] = HID_SET_ADDR;
        write_uint32_le(56, cmd_buf+1);
        if(!_io->sendPacket(cmd_buf, 5, HID_ID_CMD)) {
            qWarning("SET ADDR cmd returned an error");
            FAIL_ABORT(QLatin1String("SET_ADDRESS"));
        }

        if(!verifyAddressSet(status, current_fw_address, HIDDFU_DEFAULT_TIMEOUT)) {
            emit finished(hiddfu_worker::Flash, false);
            return;
        }
        qDebug("Address %u (0x%08x) set", current_fw_address, current_fw_address);

        iofile.seek(fw_start + 56);

        while(!iofile.atEnd()) {
            if(force_stop) {
                FAIL_ABORT(tr("aborted"));
            }

            fw_data.fill(char(0));
            read = iofile.read(fw_data.data() + 7, 56);
            //qWarning()<<"From FW_file: "<<fw_data.mid(7, read).toHex().toUpper();
            if(read < 0) {
                emit error(ERR_READ, iofile.errorString());
                FAIL_ABORT(tr("cannot read the firmware file"));
            }
            if(read > 0) {
                if(!_io->sendPacket(fw_data, HID_ID_DATA)) {
                    qWarning("DATA transfer error");
                    FAIL_ABORT(tr("data transfer"));
                }

                /* Auto-increment */
                current_fw_address += 56;
                if(!verifyAddressSet(status, current_fw_address, HIDDFU_DEFAULT_TIMEOUT)) {
                    if(status.error != OD_DFU_ERROR_OK) {
                        qWarning()<<"Error writing: "<<status.error;
                        emit error(ERR_FLASH, tr("stopped flashing at %1 - internal error %2")
                                   .arg(qRound(100. * bytes_written / fw_size))
                                   .arg(dfu_error_string(status.error)));
                    }

                    emit finished(hiddfu_worker::Flash, false);
                    return;
                }
                qDebug("Address %u (0x%08x) auto-set", current_fw_address, current_fw_address);
            }

            qDebug("Write of  %u:%u finished", bytes_written, read);

            bytes_written += read;
            emit progress(qRound(100. * bytes_written / fw_size));
        }
    }

    //set address to 0

    current_fw_address = 0;
    cmd_buf[0] = HID_SET_ADDR;
    write_uint32_le(0, cmd_buf+1);
    if(!_io->sendPacket(cmd_buf, 5, HID_ID_CMD)) {
        qWarning()<<"SET ADDR cmd returned an error";
        FAIL_ABORT(QLatin1String("SET_ADDRESS"));
    }

    if(!verifyAddressSet(status, current_fw_address, HIDDFU_DEFAULT_TIMEOUT)) {
        emit finished(hiddfu_worker::Flash, false);
        return;
    }
    qDebug("Address 0 set");

    //write the first chunk
    iofile.seek(fw_start);
    fw_data.fill(char(0));
    read = iofile.read(fw_data.data() + 7, 56);
    if(read < 0) {
        emit error(ERR_READ, iofile.errorString());
        FAIL_ABORT(tr("cannot read the firmware file"));
    }

    if(!_io->sendPacket(fw_data, HID_ID_DATA)) {
        qWarning()<<"DATA transfer error";
        FAIL_ABORT(tr("data transfer"));
    }

    /* Auto-increment */
    current_fw_address += 56;
    if(!verifyAddressSet(status, current_fw_address, HIDDFU_DEFAULT_TIMEOUT)) {
        if(status.error != OD_DFU_ERROR_OK) {
            qWarning()<<"Error writing: "<<status.error;
            emit error(ERR_FLASH, tr("stopped flashing at %1 - internal error %2")
                       .arg(qRound(100. * bytes_written / fw_size))
                       .arg(dfu_error_string(status.error)));
        }

        emit finished(hiddfu_worker::Flash, false);
        return;
    }

    emit progress(100);

    qDebug("Flashed OK");
    emit finished(hiddfu_worker::Flash, true);
}

void hiddfu_usb_worker::reboot(int kind)
{
    if(!_io) {
        emit error(ERR_COMM, tr("Cannot reboot - not connected"));
        emit finished(Reboot, false);
        return;
    }

    uint8_t cmd_buf[5];
    cmd_buf[0] = static_cast<uint8_t>(kind);
    write_uint16_le(0, cmd_buf+1);
    bool res = _io->sendPacket(cmd_buf, 5, HID_ID_CMD);

    if(!res) {
        emit error(ERR_COMM, tr("Reboot request failure"));
    } else {
        _io->close();
        _io->deleteLater();
        _io = nullptr;
    }
    emit finished(Reboot, res);
}

#undef FAIL_ABORT

#define FAIL_ABORT(reason) do{\
    emit error(ERR_COMM, (reason));\
    return false;}while(0)

bool hiddfu_usb_worker::erase_op(Commands op, uint8_t hid_cmd)
{
    Q_UNUSED(op)
    DFUStatus status;
    uint8_t cmd_buf[6];

    if(!_io) {
        emit error(ERR_COMM, tr("Cannot erase - not connected"));
        return false;
    }

    cmd_buf[0] = hid_cmd;
    write_uint32_le(0, cmd_buf+1);
    if(!_io->sendPacket(cmd_buf, 5, HID_ID_CMD)) {
        qWarning("ERASE cmd %u returned an error", hid_cmd);
        FAIL_ABORT(tr("prepare storage cleanup"));
        return false;
    }

    emit info(IStatus, tr("Erasing") + QLatin1String("..."));
    do {
        HIDProxy::qSleep(100);
        emit progress(-1);
        if(force_stop || !getStatus(&status)) {
            FAIL_ABORT(QLatin1String("GET_STATUS"));
        }
    }while((status.state & SB_Erasing) && (status.error == OD_DFU_ERROR_OK));

    if(status.error != OD_DFU_ERROR_OK) {
        qWarning()<<"Error erasing storage: "<<status.error;
        emit error(ERR_ERASE, QVariant());
        return false;
    }

    return true;
}

#undef FAIL_ABORT

#ifndef OD_NO_DEVELOPER

void hiddfu_usb_worker::storage_erase()
{
    if(!preOperationRoutine(EraseStor)) {
        return;
    }

    if(erase_op(EraseStor, HID_ERASE_STORAGE)) {
        emit info(INotify, tr("Device storage has been erased"));
        emit finished(EraseStor, true);
    } else {
        emit finished(EraseStor, false);
    }
}

bool hiddfu_usb_worker::storage_write_entry(QFile* iofile, const StorageTableEntry& entry, int flash_size)
{
    DFUStatus status;
    uint8_t cmd[6];

    const quint64 entryEnd = quint64(entry.address) + entry.size;
    if(!entry.size || entryEnd > quint64(flash_size)) {
        emit error(ERR_COMM, entry.size
                   ? tr("Cannot write %1 to the device storage: overflowed by %2 bytes")
                     .arg(iofile->fileName()).arg(entryEnd - quint64(flash_size))
                   : tr("Cannot write an empty data section from %1").arg(iofile->fileName()));
        return false;
    }

    cmd[0] = HID_WRITE_STORAGE;
    write_uint32_le(entry.address, &cmd[1]);

    if(!_io->sendPacket(cmd, 5, HID_ID_CMD)) {
        qWarning("WRITE STORAGE cmd returned an error");
        emit error(ERR_COMM, QLatin1String("SET_ADDRESS"));
        return false;
    }

    qDebug("Start flashing");
    emit info(INotify, tr("Writing %1 to the device storage from 0x%2")
              .arg(QFileInfo(*iofile).fileName()).arg(entry.address, 8, 16, QLatin1Char('0')));
    emit progress(0);
    if(!iofile->seek(entry.offset)) {
        emit error(ERR_READ, iofile->errorString());
        return false;
    }

    switch (flash_storage_bulk(iofile, entry.size)) {
        case FSuccess: return true;
        case FError: return false;
        case FFallback: break;
    }

    qWarning("Bulk flashing is not supported on this OS or failed");

    QByteArray fw_data(63, char(0));
    uint32_t bytes_written = 0;
    int read;
    while(bytes_written < entry.size) {
        if(force_stop) {
            return false;
        }

        fw_data.fill(char(0));
        read = iofile->read(fw_data.data() + 7, qMin<uint32_t>(56, entry.size - bytes_written));
        if(read < 0) {
            emit error(ERR_READ, iofile->errorString());
            return false;
        } else if(read == 0) {
            emit error(ERR_READ, tr("Unexpected end of file"));
            return false;
        }

        if(!_io->sendPacket(fw_data, HID_ID_DATA)) {
            qWarning()<<"DATA transfer error";
            emit error(ERR_COMM, tr("data transfer"));
            return false;
        }

        do {
            if(force_stop || !getStatus(&status)) {
                emit error(ERR_COMM, QLatin1String("GET_STATUS"));
                return false;
            }
            HIDProxy::qSleep(50);
        } while ((status.state & SB_Writing) && (status.error == OD_DFU_ERROR_OK));
        //dumpStatus(&status);

        if(status.error != OD_DFU_ERROR_OK) {
            qWarning()<<"Error writing: "<<status.error;
            emit error(ERR_FLASH, tr("stopped flashing at %1 - internal error %2")
                       .arg(qRound(100. * bytes_written / entry.size))
                       .arg(dfu_error_string(status.error)));
            return false;
        }

        qDebug("Write of %u:%d finished", bytes_written, read);

        bytes_written += read;
        emit progress(qRound(100. * bytes_written / entry.size));
    }
    emit progress(100);

    emit info(INotify, tr("Device storage has been successfully programmed."));
    return true;
}

#define FAIL_ABORT(reason) do{\
    emit error(ERR_COMM, (reason));\
    emit finished(hiddfu_worker::FlashStor, false);\
    return;}while(0)

void hiddfu_usb_worker::storage_write(const QString& source, int index)
{
    //common checks
    int flash_size = check_spi_flash();
    if(flash_size < 0) {
        FAIL_ABORT(tr("This device does not support storage programming"));
    } else if(flash_size==0) {
        FAIL_ABORT(tr("the device storage ID is not supported"));
    }
    emit info(INotify, tr("Device storage memory capacity: %1Mb").arg(flash_size/(1024.*1024)));

    QFile iofile(source);
    if(!iofile.open(QFile::ReadOnly)) {
        qWarning()<<"Cannot read from "<<source<<": "<<iofile.errorString();
        FAIL_ABORT(tr("%1 is not readable").arg(source));
    }

    QString fInfo, errorString;
    QList<StorageTableEntry> vtable;
    if(!parseStorageHeader(&iofile, fInfo, vtable, &errorString)) {
        FAIL_ABORT(errorString);
    }

    emit info(IDfuDescr, fInfo);

    if(!vtable.size() || (index >= vtable.size())) {
        FAIL_ABORT(tr("Data section %1 does not exist in the package").arg(index));
    }

    if(!preOperationRoutine(FlashStor)) {
        return;
    }

    if(index == -1) {
        for(const StorageTableEntry& entry : qAsConst(vtable)) {
            if(!storage_write_entry(&iofile, entry, flash_size)) {
                emit finished(FlashStor, false);
                return;
            }
        }
    } else {
        if(!storage_write_entry(&iofile, vtable.at(index), flash_size)) {
           emit finished(FlashStor, false);
           return;
        }
    }

    emit info(INotify, tr("Device storage has been successfully programmed."));
    emit finished(FlashStor, true);
}
#undef FAIL_ABORT

void hiddfu_usb_worker::storage_read(const QString& source, const QList<LocationTypeDef>& locations, const QString& info)
{
#if 0
    uint8_t cmd[6];
    int res;

    qWarning("Requested storage_read from 0x%08x (%lld bytes)", address, size);

    //common checks
    int flash_size = check_spi_flash();
    if(flash_size < 0) {
        qWarning("SPI is not supported by this bootloader");
        emit error(ERR_COMM, QString("the device storage ID is not supported"));
        emit finished(ReadStor, false);
        return;
    } else if(flash_size==0) {
        qWarning()<<"SPI chip is not known";
        emit error(ERR_COMM, QString("the device storage ID is not supported"));
        emit finished(ReadStor, false);
        return;
    }
    emit info(INotify, tr("Device storage memory capacity: %1Mb").arg(flash_size/(1024.*1024)));

    if(qint64(address) > flash_size) {
        emit error(ERR_FLASH, tr("Cannot read from 0x%1 - address is beyond the storage boundary").arg(address, 8, 16, QLatin1Char('0')));
        emit finished(ReadStor, false);
        return;
    }

    if(!preOperationRoutine(ReadStor)) {
        return;
    }

    if(size<0) {
        size = flash_size;
    }

    size = qMin<int>((flash_size - address), (int)size);

    //set flash address
    cmd[0] = HID_STORAGE_ADDRESS;
    write_uint32_le(address, cmd+1);

    if(!_io->sendPacket(cmd, 5, HID_ID_CMD)) {
        qWarning()<<"READ cmd returned an error";
        emit error(ERR_COMM, QLatin1String("SET_ADDRESS"));
        emit finished(ReadStor, false);
        return;
    }

    if(flash_storage_read(iofile, address, size)) {
        return;
    }
    qWarning("Bulk flashing is not supported on this OS or failed");

    cmd[0] = HID_READ_STORAGE;
    write_uint32_le(size|0x80000000UL, cmd + 2);
    if(!_io->sendPacket(cmd, 5, HID_ID_CMD)) {
        qWarning()<<"READ cmd returned an error";
        emit error(ERR_COMM, tr("prepare download"));
        emit finished(ReadStor, false);
        return;
    }

    int read = 0;
    _io->setReportID(HID_ID_DATA);
    while(read < size) {
        if(force_stop) {
            _io->setReportID(-1);
            emit finished(ReadStor, false);
            return;
        }

        qWarning("Wait HID read %d of %lld", read, size);
        res = _io->waitForReadyRead(1000);
        if(res <= 0) {
            qWarning("HID read error or no response");
            emit error(ERR_COMM, tr("cannot read from device"));
            _io->setReportID(-1);
            emit finished(ReadStor, false);
            return;
        }

        /* incl ID */
        QByteArray reply = _io->getData(res);
        iofile->write(reply.constData()+1, qMin<int>(res-1, int(size)-read));
        read += (res-1);
        emit progress(qRound(100. * read / size));
    }

    _io->setReportID(-1);
    emit progress(100);
    emit info(INotify, tr("Device storage data has been read and saved"));
    emit finished(ReadStor, true);
#else
    qWarning("Device storage content download is not implemented");
    Q_UNUSED(source);
    Q_UNUSED(locations);
    Q_UNUSED(info);
    emit error(ERR_COMM, tr("unimplemented"));
    emit finished(ReadStor, false);
#endif
}

typedef enum {
    JEDEC_M_SPANSION = 0x01,
    JEDEC_M_ST       = 0x20,
    JEDEC_M_ATMEL    = 0x1F,
    JEDEC_M_WINBOND  = 0xEF,
} JEDEC_Manufacturer;

typedef struct {
        uint8_t mf;
        uint16_t id;
        const char* name;
        uint16_t page_size;
        uint16_t sector_size;
        uint16_t sector_count;
} spiflash_chip_t;

static const spiflash_chip_t SPIFLASH_CHIPS[] = {
    {JEDEC_M_WINBOND, 0x3010, "W25X05CL", 256, 4*1024, 16},
    {JEDEC_M_WINBOND, 0x3011, "W25X10CL", 256, 4*1024, 32},
    {JEDEC_M_WINBOND, 0x3012, "W25X20CL", 256, 4*1024, 64},
    {JEDEC_M_WINBOND, 0x3013, "W25X40CL", 256, 4*1024, 128},
    {JEDEC_M_WINBOND, 0x4013, "W25Q40CL", 256, 4*1024, 128},
    {JEDEC_M_WINBOND, 0x4014, "W25Q80DV", 256, 4*1024, 256},
    {JEDEC_M_WINBOND, 0x4015, "W25Q16JV", 256, 4*1024, 512},
    {JEDEC_M_WINBOND, 0x7015, "W25Q16JV-Q", 256, 4*1024, 512},
    {JEDEC_M_WINBOND, 0x4016, "W25Q32FV", 256, 4*1024, 1024},
    {JEDEC_M_WINBOND, 0x6016, "W25Q32FV-Q", 256, 4*1024, 1024},
    {JEDEC_M_WINBOND, 0x7016, "W25Q32JV-M", 256, 4*1024, 1024},
    {JEDEC_M_WINBOND, 0x8016, "W25Q32JW-M", 256, 4*1024, 1024},
    {JEDEC_M_WINBOND, 0x4017, "W25Q64FV", 256, 4*1024, 2048},
    {JEDEC_M_WINBOND, 0x6017, "W25Q64FV-Q", 256, 4*1024, 2048},
    {JEDEC_M_WINBOND, 0x4018, "W25Q128FV", 256, 4*1024, 4096},
    {JEDEC_M_WINBOND, 0x6018, "W25Q128FV-Q", 256, 4*1024, 4096},
    {0,0,0,0,0,0},
};

int hiddfu_usb_worker::check_spi_flash()
{
    QByteArray spi_struct;
    if(!_io->getFeature(16, spi_struct, HID_ID_SPI) || spi_struct.size() < 3) {
        qWarning()<<"SPI interface is not supported";
        return -1;
    }
    qWarning()<<"SPI feature: "<<spi_struct.toHex().toUpper();

    const uint8_t* spi_struct_data = (const uint8_t*)spi_struct.constData();

    uint8_t spi_mf = spi_struct_data[0];
    uint16_t spi_id = read_uint16_le(spi_struct_data+1);
    qWarning()<<"SPI manufacturer/id "<<QString("0x%1/0x%2").arg(spi_mf,2,16,QLatin1Char('0')).arg(spi_id,4,16,QLatin1Char('0'));
    const spiflash_chip_t* known_chips = SPIFLASH_CHIPS;
    while(known_chips->name) {
        if((known_chips->mf == spi_mf) && (known_chips->id == spi_id)) {
            qWarning()<<"Detected SPI chip "<<known_chips->name;
            return int(known_chips->sector_size) * known_chips->sector_count;
        }
        ++known_chips;
    }
    qWarning()<<"SPI chip "<<QString("0x%1:0x%2").arg(spi_mf,2,16,QLatin1Char('0')).arg(spi_id,4,16,QLatin1Char('0'))<<" is not known";
    qWarning("Guessed size: %ukB", qRound((64UL*1024) * pow(2,(spi_id & 0xFF)-0x10)));
    return 0;
}
#endif  // !OD_NO_DEVELOPER

void hiddfu_usb_worker::eeprom_erase()
{
    if(!preOperationRoutine(EraseEEPROM)) {
        return;
    }

    if(erase_op(EraseEEPROM, HID_ERASE_EEPROM)) {
        emit info(INotify, tr("Device config storage has been erased"));
        emit finished(EraseEEPROM, true);
    } else {
        emit finished(EraseEEPROM, false);
    }
}

bool hiddfu_usb_worker::getStatus(DFUStatus* status)
{
    if(!_io || !_io->isOpen()) {
        return false;
    }

    QByteArray status_data;
    if(!_io->getFeature(14, status_data, HID_ID_STAT) || (status_data.size()<14))
    {
#ifdef Q_OS_WIN
        if(!(_io->getQuirks() & HIDProxy::QUIRK_INPUT_VS_FEATURE)) {
            /* old (pre 2020.04.16) bootloaders declare INPUT instead of FEATURE */
            qWarning()<<"GET STAT failed - retry with a quirk";
            _io->setQuirks(_io->getQuirks() | HIDProxy::QUIRK_INPUT_VS_FEATURE);
            return getStatus(status);
        }
#endif
        qWarning()<<"GET STAT failed";
        return false;
    }

    const uint8_t* feature_buf = (const uint8_t*)status_data.constData();
    status->state   = feature_buf[0];
    status->error   = feature_buf[1];
    status->address = read_uint32_le(feature_buf+2);
    status->maxsize = read_uint32_le(feature_buf+6);

    /* Bug fix: 0x08000000 not subtracted */
    if(status->maxsize > 0x08000000U) {
        status->maxsize -= 0x08000000U;
    }

    status->id = read_uint32_le(feature_buf+10);
#ifndef OD_NO_DEVELOPER
    dumpStatus(status);
#endif
    return true;
}
