#include "hiddfu_uart_worker.h"

#include <uartcommunicator.h>
#include <endianrw.h>

#include <QDebug>
#include <QFileInfo>
#include <QElapsedTimer>
#include <limits>

hiddfu_uart_worker::hiddfu_uart_worker(QObject* parent): hiddfu_worker (parent), _io(nullptr), busy(false)
{

}

hiddfu_uart_worker::~hiddfu_uart_worker()
{
    if(_io) {
        _io->close();
        delete _io;
        _io = nullptr;
    }
}

void hiddfu_uart_worker::setup(const QVariantMap& params)
{
    if(_io) {
        _io->close();
        delete _io;
        _io = nullptr;
    }

    QString m_medium = params.value("port").toString();
    if(m_medium.isEmpty()) {
        emit error(ERR_COMM, tr("Serial port is not set - cannot connect"));
        emit finished(Setup, false);
        return;
    }

    _io = new UARTCommunicator(this);
    quint32 m_baudrate = params.value("baudrate", 115200).toUInt();
    if(!m_baudrate) {
        m_baudrate = 115200;
    }
    if(m_baudrate > quint32(std::numeric_limits<int>::max())) {
        emit error(hiddfu_worker::ERR_COMM, tr("Invalid baudrate %1").arg(m_baudrate));
        emit finished(hiddfu_worker::Setup, false);
        return;
    }
    _io->setBaud(m_baudrate);

    int m_address = params.value("address", -1).toInt();
    qWarning()<<"Connecting to "<<m_medium<<" at "<<m_baudrate;
    if(_io->open(m_medium)) {
        QString serial;

        /*
         * Some bootloaders may implement baudrate detection
         */
        _io->sendPattern();

        QList<UARTMessage> replies;
        if((m_address < 0) || (m_address >= 0xFF)) {
            QElapsedTimer requestTimer;
            requestTimer.start();
            int timeleft = 0;
            int maxwait = 256 * qRound(50/*really 28*/*100000. / m_baudrate);
            _io->sendto(0xFF, QByteArray(1, char(HID_ID_SERIAL)));
            UARTMessage msg;
            if(_io->waitMessageReady(maxwait)) {
                while(UARTCommunicator::messageIsValid((msg = _io->get()))) {
                    if(msg.second.size() == 24) {
                        qWarning()<<"Discovered device "<<msg.first;
                        replies.append(msg);
                    }
                }

                timeleft = maxwait - int(requestTimer.elapsed());
                while((timeleft > 0) && _io->waitMessageReady(timeleft)) {
                    while(UARTCommunicator::messageIsValid((msg = _io->get()))) {
                        if(msg.second.size() == 24) {
                            qWarning()<<"Discovered device "<<msg.first;
                            replies.append(msg);
                        }
                    }
                    timeleft = maxwait - int(requestTimer.elapsed());
                }
            }
            qWarning()<<"Discovery finished";
        } else {
            int maxwait = qRound(50/*really 28*/*100000. / m_baudrate);
            UARTMessage msg;
            _io->sendto(uint8_t(m_address), QByteArray(1, char(HID_ID_SERIAL)));
            if(_io->waitMessageReady(0) || _io->waitMessageReady(maxwait)) {
                msg = _io->get();
                if(UARTCommunicator::messageIsValid(msg) && (msg.first == m_address)) {
                    qWarning()<<"Found device "<<msg.first;
                    replies.append(msg);
                }
            }
        }

        if(replies.isEmpty()) {
            _io->close();
            delete _io;
            _io = nullptr;
        } else {
            _io->setTarget(replies.at(0).first);
            serial = QString::fromLatin1(replies.at(0).second.mid(1));
            if(postConnectionRoutine(serial)) {
                emit finished(Setup, true);
                return;
            } else {
                _io->close();
                delete _io;
                _io = nullptr;
            }
        }
    } else {
        qWarning()<<"Cannot connect to "<<m_medium;
        emit error(ERR_COMM, tr("Cannot connect to %1").arg(m_medium));
    }

    emit finished(Setup, false);
}

void hiddfu_uart_worker::getstatus()
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

void hiddfu_uart_worker::getinfo()
{
    if(!_io) {
        emit error(hiddfu_worker::ERR_COMM, tr("Cannot get device info - no handle"));
        emit finished(hiddfu_worker::Info, false);
        return;
    }

    if(!execCmd(HID_ID_INFO)) {
        qWarning()<<"GET INFO failed";
        emit error(hiddfu_worker::ERR_COMM, tr("GET INFO error"));
        emit finished(hiddfu_worker::Info, false);
        return;
    }

    UARTMessage infomsg = _io->get();
    if(!UARTCommunicator::messageIsValid(infomsg) || infomsg.first != _io->target()
            || infomsg.second.size() < 2 || quint8(infomsg.second.at(0)) != HID_ID_INFO) {
        emit error(hiddfu_worker::ERR_COMM, tr("Invalid GET INFO reply"));
        emit finished(hiddfu_worker::Info, false);
        return;
    }
    qWarning()<<"GET_INFO "<<infomsg;
    emit info(hiddfu_worker::IDescr, QString::fromLatin1(infomsg.second.mid(1)));
    emit finished(hiddfu_worker::Info, true);
}

#define FAIL_ABORT(reason) do{\
    emit error(ERR_COMM, (reason));\
    emit finished(Flash, false);\
    return;}while(0)

#define FLASH_CHUNK (248)

void hiddfu_uart_worker::flash(const QString& dfu_file)
{
    if(!_io) {
        FAIL_ABORT(tr("No active connection"));
    }
    force_stop=false;

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

    uint32_t fw_start = iofile.pos();
    uint32_t fw_size = iofile.size() - fw_start;

    DFUStatus status;
    uint8_t fw_data[FLASH_CHUNK+1];
    uint32_t bytes_written = 0;
    int read;

    //erase
    char erase_cmd[] = {char(HID_ID_CMD), char(HID_ERASE)};
    uint8_t addr_cmd[6] = {HID_ID_CMD, HID_SET_ADDR, 0,0,0,0};
    if(!busyWait()) {
        FAIL_ABORT(tr("Medium busy"));
    }

    if(!_io->send(QByteArray(erase_cmd, 2))) {
        qWarning("ERASE cmd returned an error");
        FAIL_ABORT(tr("erase"));
    }

    qWarning()<<"Start erasing";
    emit info(IStatus, tr("Erasing") + QLatin1String("..."));
    HIDProxy::qSleep(500);

    do {
        HIDProxy::qSleep(100);
        emit progress(-1);
        if(force_stop || !getStatus(&status)) {
            FAIL_ABORT(QLatin1String("GET_STATUS"));
        }
    }while((status.state & SB_Erasing)&&(status.error == OD_DFU_ERROR_OK));

    if(status.error != OD_DFU_ERROR_OK) {
        qWarning()<<"Error erasing: "<<status.error;
        FAIL_ABORT("cannot erase the old firmware");
    }

    qDebug("Erased");

    qDebug("Start flashing");
    emit info(IStatus, tr("Flashing") + QLatin1String("..."));
    emit progress(0);

    fw_data[0] = HID_ID_DATA;
    if(fw_size > FLASH_CHUNK) {
        //first write everything but the first chunk

        //set address to CHUNK_1
        write_uint32_le(FLASH_CHUNK, addr_cmd + 2);
        if(!_io->send(QByteArray((const char*)addr_cmd, 6))) {
            qWarning("SET ADDR cmd returned an error");
            FAIL_ABORT(QLatin1String("SET_ADDRESS"));
        }

        qDebug("Address should be set to %u", FLASH_CHUNK);
        if(!verifyAddressSet(status, FLASH_CHUNK, HIDDFU_DEFAULT_TIMEOUT)) {
            emit finished(Flash, false);
            return;
        }

        iofile.seek(fw_start + FLASH_CHUNK);

        while(!iofile.atEnd()) {
            if(force_stop) {
                FAIL_ABORT(tr("aborted"));
            }

            read = iofile.read((char*)(fw_data+1), FLASH_CHUNK);
            if(read < 0) {
                emit error(ERR_READ, iofile.errorString());
                FAIL_ABORT(tr("cannot read the firmware file"));
            } else if(read == 0) {
                qWarning("EOF");
                break;
            }

            if(!_io->send(QByteArray((const char*)fw_data, read+1))) {
                qWarning("DATA bulk transfer error");
                FAIL_ABORT(tr("data transfer"));
            }

            do {
                HIDProxy::qSleep(50);
                if(force_stop || !getStatus(&status)) {
                    FAIL_ABORT(QLatin1String("GET_STATUS"));
                }
            } while ((status.state & SB_Writing) && (status.error == OD_DFU_ERROR_OK));

            if(status.error != OD_DFU_ERROR_OK)
            {
                qWarning()<<"Error writing: "<<status.error;
                emit error(ERR_FLASH, tr("stopped flashing at %1 - internal error %2")
                           .arg(qRound(100. * bytes_written / fw_size))
                           .arg(dfu_error_string(status.error)));
                emit finished(hiddfu_worker::Flash, false);
                return;
            }

            qDebug("Write of  %u:%u finished", bytes_written, read);

            bytes_written += read;
            emit progress(qRound(100. * bytes_written / fw_size));
        }
    }

    //set address to 0
    write_uint32_le(0, &addr_cmd[2]);

    if(!_io->send(QByteArray((const char*)addr_cmd, 6))) {
        qWarning()<<"SET ADDR cmd returned an error";
        FAIL_ABORT(QLatin1String("SET_ADDRESS"));
    }

    qDebug("Address should be set to 0");
    if(!verifyAddressSet(status, 0, HIDDFU_DEFAULT_TIMEOUT)) {
        emit finished(Flash, false);
        return;
    }

    iofile.seek(fw_start);

    read = iofile.read((char*)(fw_data+1), FLASH_CHUNK);
    if(read < 0) {
        emit error(ERR_READ, iofile.errorString());
        FAIL_ABORT(tr("cannot read the firmware file"));
    }

    if(!_io->send(QByteArray((const char*)fw_data, read+1))) {
        qWarning()<<"DATA transfer error";
        FAIL_ABORT(tr("data transfer"));
    }

    do {
        HIDProxy::qSleep(50);
        if(force_stop || !getStatus(&status)) {
            FAIL_ABORT(QLatin1String("GET_STATUS"));
        }
    } while ((status.state & SB_Writing) && (status.error == OD_DFU_ERROR_OK));

    if(status.error != OD_DFU_ERROR_OK) {
        qWarning()<<"Error writing: "<<status.error;
        emit error(ERR_FLASH, tr("stopped flashing at %1 - internal error %2")
                   .arg(qRound(100. * bytes_written / fw_size))
                   .arg(dfu_error_string(status.error)));
        emit finished(hiddfu_worker::Flash, false);
        return;
    }

    emit progress(100);
    qDebug("Flashed OK");
    emit finished(hiddfu_worker::Flash, true);
}
#undef FLASH_CHUNK
#undef FAIL_ABORT

void hiddfu_uart_worker::reboot(int kind)
{
    if(!_io) {
        emit error(ERR_COMM, tr("Cannot reboot - not connected"));
        emit finished(Reboot, false);
        return;
    }

    uint8_t msg[3] = {HID_ID_CMD, static_cast<uint8_t>(kind), 0};
    bool res = _io->send(QByteArray((const char*)msg, 2));
    if(!res) {
        emit error(ERR_COMM, tr("Reboot request failure"));
    } else {
        /* Some RS485 adapters scramble the last bytes when disconnected too soon (for example, OD-RS485 v <= 1.3). */
        HIDProxy::qSleep(500);
        _io->close();
        _io->deleteLater();
        _io = nullptr;
    }
    emit finished(Reboot, res);
}


#ifndef OD_NO_DEVELOPER

void hiddfu_uart_worker::storage_erase()
{
    qWarning("Storage erasure not implemented for UART");
    emit finished(EraseStor, false);
}

void hiddfu_uart_worker::storage_write(const QString& source, int index)
{
    Q_UNUSED(source); Q_UNUSED(index);
    qWarning("Storage flashing not implemented for UART");
    emit finished(FlashStor, false);
}

void hiddfu_uart_worker::storage_read(const QString& source, const QList<LocationTypeDef>& locations, const QString& info)
{
    Q_UNUSED(source); Q_UNUSED(locations); Q_UNUSED(info);
    qWarning("Storage reading not implemented for UART");
    emit finished(ReadStor, false);
}

#endif  // OD_NO_DEVELOPER

bool hiddfu_uart_worker::busyWait() {
    int wait = 50;
    while(busy && wait--) {
        HIDProxy::qSleep(100);
    }
    return !busy;
}

bool hiddfu_uart_worker::execCmd(uint8_t cmd)
{
    if(!busyWait()) {
        qWarning("Medium busy - timeout waiting %u", cmd);
        return false;
    }
    qWarning("Busy take for %u", cmd);
    busy = true;
    bool res = _io->send(QByteArray(1, static_cast<char>(cmd))) && _io->waitMessageReady(4000000UL/_io->baud()) && _io->pending();
    busy = false;
    qWarning("Busy release for %u", cmd);
    return res;
}

#define FAIL_ABORT(reason) do{\
    emit error(ERR_COMM, (reason));\
    return false;}while(0)

bool hiddfu_uart_worker::erase_op(Commands op, uint8_t hid_cmd)
{
    Q_UNUSED(op)
    DFUStatus status;
    char erase_cmd[2];

    if(!_io) {
        emit error(ERR_COMM, tr("Cannot erase - not connected"));
        return false;
    }

    erase_cmd[0] = static_cast<char>(HID_ID_CMD);
    erase_cmd[1] = static_cast<char>(hid_cmd);

    if(!busyWait()) {
        FAIL_ABORT(tr("Medium busy"));
    }

    if(!_io->send(QByteArray(erase_cmd, 2))) {
        qWarning("ERASE cmd %u returned an error or NACK", hid_cmd);
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

void hiddfu_uart_worker::eeprom_erase()
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

bool hiddfu_uart_worker::postConnectionRoutine(const QString& sn)
{
    _io->clear();_io->drop();
    busy = false;

#ifndef OD_NO_DEVELOPER
    busy = true;
    _io->sendto(uint8_t(_io->target()), QByteArray(1, char(HID_ID_CFG)));
    if(_io->waitMessageReady(0) || _io->waitMessageReady(qRound(50/*really 28*/*100000. / _io->baud())))
    {
        UARTMessage msg = _io->get();
        if(UARTCommunicator::messageIsValid(msg) && (msg.first == _io->target()) && (msg.second.size() == 16)) {
            qWarning("UART config: magic 0x%08x, address 0x%02x, stop %u, parity %u, bits %u, baud %u",
                     read_uint32_le(msg.second.constData() + 0), *(unsigned char*)(msg.second.constData() + 4),
                     *(unsigned char*)(msg.second.constData() + 5), *(unsigned char*)(msg.second.constData() + 6),
                     *(unsigned char*)(msg.second.constData() + 7), read_uint32_le(msg.second.constData() + 8));
        }
    }
    busy = false;
#endif
    return hiddfu_worker::postConnectionRoutine(QString("0x%1:").arg(_io->target()) + sn);
}

bool hiddfu_uart_worker::getStatus(DFUStatus* status)
{
    if(!_io) {
        return false;
    }

    if(!execCmd(HID_ID_STAT)) {
        qWarning()<<"GET STAT failed - cannot send or NACK";
        return  false;
    }

    UARTMessage msg = _io->get();
    if(!UARTCommunicator::messageIsValid(msg) || (msg.first != _io->target()) || (msg.second.size() != 15)
            || quint8(msg.second.at(0)) != HID_ID_STAT)
    {
        qWarning()<<"GET STAT failed ( wrong addr "<<int(msg.first)<<" or wrong message size "<<msg.second.size()<<")";
        return  false;
    }

    const uint8_t* feature_buf = (const uint8_t*)msg.second.constData();

    status->state   = feature_buf[1];
    status->error   = feature_buf[2];
    status->address = read_uint32_le(feature_buf+3);
    status->maxsize = read_uint32_le(feature_buf+7);
    if(status->maxsize > 0x08000000U) {
        status->maxsize -= 0x08000000U;
    }
    status->id      = read_uint32_le(feature_buf+11);
#ifndef OD_NO_DEVELOPER
    dumpStatus(status);
#endif
    return true;
}
