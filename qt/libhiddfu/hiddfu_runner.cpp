#include "hiddfu_runner.h"
#include "odhid_global.h"

#include <endianrw.h>
#include <QDebug>
#include <QApplication>
#include <QVariant>
#include <QTime>
#include <QFile>
#include <QFileInfo>

#include <math.h>
#include <limits>

namespace {
constexpr quint64 MaxPackageInfoSize = 1024U * 1024U;
constexpr uint32_t MaxStorageEntries = 65536U;
}

QString dfu_error_string(uint8_t err)
{
    switch(err)
    {
        case OD_DFU_ERROR_OK: return "no error";
        case OD_DFU_ERROR_FLASHOP: return "flash operation";
        case OD_DFU_ERROR_BUSY: return "busy";
        case OD_DFU_ERROR_ADDRESS: return "invalid address";
        case OD_DFU_ERROR_ALIGN: return "alignment error";
        case OD_DFU_ERROR_NOERASE: return "write to non-erased area";
        case OD_DFU_ERROR_PROTECT: return "read/write protected";
        case OD_DFU_ERROR_WCRC: return "crc error";
        case OD_DFU_ERROR_OVERRUN: return "overrun";
        default:break;
    }
    return QStringLiteral("unknown error");
}

/************************************/

void dumpStatus(const DFUStatus* status)
{
    qWarning()<<"HID DFU Status:\n  state:";
    if(status->state & SB_Erasing)    qWarning()<<"    Erasing";
    if(status->state & SB_Writing)    qWarning()<<"    Writing";
    if(status->state & SB_LauchSDFU)  qWarning()<<"    SDFU pending";
    if(status->state & SB_LauchDFU)   qWarning()<<"    DFU pending";
    if(status->state & SB_WaitLaunch) qWarning()<<"    Restart pending";
    if(status->state & SB_Launched)   qWarning()<<"    Launched";
    qWarning()<<"  error: "<<QString("%1").arg(status->error,2,16,QLatin1Char('0'));
    qWarning()<<"  address: "<<QString("%1").arg(status->address,8,16,QLatin1Char('0'));
    qWarning()<<"  maxsize: "<<status->maxsize;
    qWarning()<<"  id: "<<QString("%1").arg(status->id,8,16,QLatin1Char('0'));
}

#define BAIL(reason) do{\
    emit error(ERR_DFU, (reason));\
    emit finished(CheckFW, false);\
    return;}while(0)
void hiddfu_worker::checkfw(const QString& file)
{
    QFile dfu_file(file);
    if(!dfu_file.open(QFile::ReadOnly)) {
        BAIL(tr("Cannot read from %1: %2").arg(file, dfu_file.errorString()));
    }

    QString errorString;
    QString fw_info;
    if(!parseDfuHeader(&dfu_file, fw_info, &errorString)) {
        BAIL(errorString);
    }
    emit info(IDfuDescr, fw_info);

    dfu_file.close();
    emit finished(CheckFW, true);
}
#undef BAIL

bool hiddfu_worker::parseDfuHeader(QFile* dfu_file, QString& info, QString* errorString)
{
    if(dfu_file->size() < 16) {
        if(errorString) {
            *errorString = tr("Firmware package %1 is invalid").arg(QFileInfo(dfu_file->fileName()).fileName());
        }
        return false;
    }

    char head[12];
    dfu_file->seek(0);

    if(dfu_file->read(head, 12) != 12)
    {
        if(errorString) {
            *errorString = tr("Cannot read firmware package %1").arg(QFileInfo(dfu_file->fileName()).fileName());
        }
        return false;
    }
    if(read_uint32_le(head) != 0x424210F0)
    {
        if(errorString) {
            *errorString = tr("%1 is not a firmware package")
                           .arg(QFileInfo(dfu_file->fileName()).fileName());
        }
        return false;
    }

    DFUStatus status;
    if(!getStatus(&status) || (read_uint32_le(head+4)!=status.id)) {
        if(errorString) {
            *errorString = tr("Incompatible hardware detected.\n"
                                "%1 firmware package is designed for a different board")
                             .arg(QFileInfo(dfu_file->fileName()).fileName());
        }
        return false;
    }

    uint32_t info_sz = read_uint32_le(head+8);
    if(info_sz > MaxPackageInfoSize)
    {
        if(errorString) {
            *errorString = tr("Firmware package %1 contains an oversized description")
                    .arg(QFileInfo(dfu_file->fileName()).fileName());
        }
        return false;
    }
    const quint64 paddedInfoSize = quint64(info_sz) + ((4U - (info_sz & 0x03U)) & 0x03U);
    if(paddedInfoSize >= quint64(dfu_file->size() - 12))
    {
        if(errorString) {
            *errorString = tr("Firmware package %1 is truncated").arg(QFileInfo(dfu_file->fileName()).fileName());
        }
        return false;
    }
    if(info_sz)
    {
        const QByteArray encodedInfo = dfu_file->read(info_sz);
        if(encodedInfo.size() != int(info_sz))
        {
            if(errorString) {
                *errorString = tr("Firmware package %1 is truncated").arg(QFileInfo(dfu_file->fileName()).fileName());
            }
            return false;
        }
        QByteArray fw_info = QByteArray::fromBase64(encodedInfo);
        if(info_sz&0x03) {
            const int padding = 4-(info_sz&0x03);
            if(dfu_file->read(head, padding) != padding)
            {
                if(errorString) {
                    *errorString = tr("Firmware package %1 is truncated").arg(QFileInfo(dfu_file->fileName()).fileName());
                }
                return false;
            }
        }

        info = QString::fromLatin1(fw_info);
    } else {
        info.clear();
    }

    return true;
}

/*
 * SPI flash image format:
 *
 *(v1 - no table)
 * 1. Magic1 0xFEAD7373  32LE
 * 2. Magic2 0x69DDCE3A  32LE
 * 3. Address            32LE
 * 4. Data offset        32LE
 * 5. Info size          32LE
 * 6. Info               (base64 data)
 * 7. Data
 *
 * (v2 - with table)
 * 1. Magic1 0xFEAD7373    32LE
 * 2. Magic2 0x69DDCE4F    32LE
 * 3. Table entries count  32LE
 * 4. Address/Offset/Size  32LE x 3 (x <count>)
 * 5. Info size            32LE
 * 6. Info                 (base64 data)
 * 7. Data                 (x <count>)
 */

#define SPI_HEADER_MAGIC1      (0xFEAD7373U)
#define SPI_HEADER_MAGIC2_V1   (0x69DDCE3AU)
#define SPI_HEADER_MAGIC2_V2   (0x69DDCE4FU)
#define SPI_IMG_MINSZ          (20)

#define FAIL(reason) do{\
    if(errorString){*errorString = (reason);}\
    return false;}while(0)

bool hiddfu_worker::parseStorageHeader(QFile* dfu_file, QString& info, QList<StorageTableEntry>& vtable, QString* errorString)
{
    QString fileName = QFileInfo(dfu_file->fileName()).fileName();
    vtable.clear();
    uint32_t info_size = 0;
    qint64 dfu_file_size = dfu_file->size();

    if(dfu_file_size < SPI_IMG_MINSZ) {
        FAIL(tr("Data package %1 is invalid").arg(fileName));
    }

    uint32_t hdr[5];
    dfu_file->seek(0);
    if(dfu_file->read((char*)hdr, 20) != 20) {
        FAIL(tr("Error reading from %1: %2").arg(fileName, dfu_file->errorString()));
    }

    if(read_uint32_le(&hdr[0]) != SPI_HEADER_MAGIC1) {
        FAIL(tr("File %1 is not a data package").arg(fileName));
    }

    switch(read_uint32_le(&hdr[1])) {
        case SPI_HEADER_MAGIC2_V1: {
            StorageTableEntry entry;
            entry.address = read_uint32_le(&hdr[2]);
            entry.offset = read_uint32_le(&hdr[3]);
            if(dfu_file->size() <= entry.offset) {
                FAIL(tr("Corrupted package - no data"));
            }
            const quint64 entrySize = quint64(dfu_file->size()) - entry.offset;
            if(entrySize > std::numeric_limits<uint32_t>::max()) {
                FAIL(tr("File %1 is too large").arg(fileName));
            }
            entry.size = uint32_t(entrySize);
            vtable.append(entry);
            info_size = read_uint32_le(&hdr[4]);
            break;
        }
        case SPI_HEADER_MAGIC2_V2: {
            uint32_t vtable_size = read_uint32_le(&hdr[2]);
            if(!vtable_size) {
                FAIL(tr("No data in the package"));
            }
            if(vtable_size > MaxStorageEntries) {
                FAIL(tr("File %1 contains too many data sections").arg(fileName));
            }
            //1st entry is partially read - hdr[3], hdr[4]
            const quint64 tableEnd = 3U * 4U + quint64(3U * 4U) * vtable_size + 4U;
            if(tableEnd > quint64(dfu_file_size)) {
                FAIL(tr("File %1 is corrupted (data missing)").arg(fileName));
            }
            StorageTableEntry entry;
            entry.address = read_uint32_le(&hdr[3]);
            entry.offset = read_uint32_le(&hdr[4]);
            if(dfu_file->read((char*)hdr, 4) != 4) {
                FAIL(tr("Error reading from %1: %2").arg(fileName, dfu_file->errorString()));
            }
            entry.size = read_uint32_le(&hdr[0]);
            vtable.append(entry);
            for(uint32_t i = 1; i < vtable_size; ++i) {
                if(dfu_file->read((char*)hdr, 3 * 4) != (3 * 4)) {
                    FAIL(tr("Error reading from %1: %2").arg(fileName, dfu_file->errorString()));
                }
                entry.address = read_uint32_le(&hdr[0]);
                entry.offset = read_uint32_le(&hdr[1]);
                entry.size = read_uint32_le(&hdr[2]);
                vtable.append(entry);
            }
            if(dfu_file->read((char*)hdr, 4) != 4) {
                FAIL(tr("Error reading from %1: %2").arg(fileName, dfu_file->errorString()));
            }
            info_size = read_uint32_le(&hdr[0]);
            break;
        }
        default: {
            FAIL(tr("File %1 is not a data package").arg(fileName));
        }
    }

    //@ info start;
    if(info_size) {
        if(info_size > MaxPackageInfoSize) {
            FAIL(tr("File %1 contains an oversized description").arg(fileName));
        }
        if(quint64(info_size) > quint64(dfu_file_size - dfu_file->pos())) {
            FAIL(tr("File %1 is corrupted (info data missing)").arg(fileName));
        }
        const QByteArray encodedInfo = dfu_file->read(info_size);
        if(encodedInfo.size() != int(info_size)) {
            FAIL(tr("Error reading from %1: %2").arg(fileName, dfu_file->errorString()));
        }
        QByteArray file_info = QByteArray::fromBase64(encodedInfo);
        info = QString::fromUtf8(file_info);
    } else {
        info.clear();
    }

    //@ data start- validate vtable;
    const quint64 dataStart = quint64(dfu_file->pos());
    for(const StorageTableEntry& entry: qAsConst(vtable)) {
        if(entry.offset < dataStart || (quint64(entry.offset) + entry.size) > quint64(dfu_file_size)) {
            FAIL(tr("File %1 is corrupted (entry data missing)").arg(fileName));
        }
    }

    return true;
}
#undef FAIL

bool hiddfu_worker::createStorageHeader(QFile* dfu_file, const QString& info, const QList<LocationTypeDef>& locations)
{
    QByteArray info64;
    if(info.size()) {
        info64 = info.toUtf8().toBase64();
    }
    uint32_t info_size = info64.size();

    uint32_t header[3];
    write_uint32_le(SPI_HEADER_MAGIC1, &header[0]);
    write_uint32_le(SPI_HEADER_MAGIC2_V2, &header[1]);
    write_uint32_le((uint32_t)locations.size(), &header[2]);
    dfu_file->write((const char*)header, 12);

    uint32_t file_pos = 12 /* fixed header */ + 12 * locations.size() /* vtable */ + 4 /* info_size */ + info_size /* info */;
    for(const LocationTypeDef& loc : locations) {
        write_uint32_le(loc.first, &header[0]);
        write_uint32_le(file_pos, &header[1]);
        write_uint32_le(loc.second, &header[2]);
        dfu_file->write((const char*)header, 12);
        file_pos += loc.second;
    }

    write_uint32_le(info_size, &header[0]);
    dfu_file->write((const char*)header, 4);
    if(info_size) {
        dfu_file->write(info64);
    }

    return true;
}

bool hiddfu_worker::postConnectionRoutine(const QString &sn)
{
    emit info(ISerial, sn);
    return true;
}

bool hiddfu_worker::preOperationRoutine(Commands cmd)
{
    DFUStatus status;
    if(!getStatus(&status)) {
        emit error(ERR_COMM, QLatin1String("GET_STATUS"));
        emit finished(cmd, false);
        return false;
    }

    if(status.state & (SB_Writing|SB_Erasing|SB_Reading)) {
        emit error(ERR_COMM, tr("The device is busy. Please try again later."));
        emit finished(cmd, false);
        return false;
    }

    return true;
}

bool hiddfu_worker::preFlashRoutine(QFile* dfu_file)
{
    QString fileInfo, errorString;
    if(!parseDfuHeader(dfu_file, fileInfo, &errorString)) {
        emit error(ERR_DFU, errorString);
        return false;
    }

    emit info(IDfuDescr, fileInfo);

    DFUStatus status;
    if(!getStatus(&status)) {
        emit error(ERR_COMM, QLatin1String("GET_STATUS"));
        return false;
    }

    const qint64 firmwareSize = dfu_file->size() - dfu_file->pos();
    qDebug("Requested flash firmware of size %lld (0x%08x)", firmwareSize, (unsigned int)firmwareSize);

    if(firmwareSize <= 0) {
        emit error(ERR_DFU, tr("The firmware package contains no firmware data."));
        return false;
    }

    if(firmwareSize > status.maxsize) {
        emit error(ERR_DFU, tr("The firmware %1 is %2 bytes bigger than the maximum amount of space available.")
                   .arg(QFileInfo(*dfu_file).fileName()).arg(firmwareSize - status.maxsize));
        return false;
    }

    return true;
}

bool hiddfu_worker::verifyAddressSet(DFUStatus& status, uint32_t address, int timeout)
{
    /* This do-while loop is needed because on Android the response may be slow and out of sync. */
    timeout = (timeout + 49) / 50;
    for(int i=0; i<timeout; ++i) {
        if(force_stop) {
            return false;
        }
        if(!getStatus(&status)) {
            emit error(ERR_COMM, QLatin1String("GET_STATUS"));
            return false;
        }
        if(status.error != OD_DFU_ERROR_OK) {
            break;
        }
        if(!(status.state & (SB_Writing|SB_Erasing)) && (status.address == address)) {
            qWarning("Address %u (0x%08x) set and verified", address, address);
            return true;
        }
        HIDProxy::qSleep(50);
    }

    qWarning("Timeout at set address %u (0x%08x)", address, address);
    emit error(ERR_COMM, QStringLiteral("SET_ADDRESS"));
    return false;
}

/**************************************************************/

#include <hiddfu_usb_worker.h>
#include <hiddfu_uart_worker.h>

hiddfu_runner::hiddfu_runner(Mode m, QObject* parent) : QObject(parent),
    dfumode(m), busy(false), connected(false),
    worker(nullptr)
{
    switch(m) {
        case USB: worker = new hiddfu_usb_worker;break;
        case UART: worker = new hiddfu_uart_worker;break;
    }

    worker->moveToThread(&workerThread);
    connect(worker, &hiddfu_worker::finished, this, &hiddfu_runner::done, Qt::QueuedConnection);
    connect(worker, &hiddfu_worker::info, this, &hiddfu_runner::info, Qt::QueuedConnection);
    connect(worker, &hiddfu_worker::error, this, &hiddfu_runner::error, Qt::QueuedConnection);
    connect(worker, &hiddfu_worker::progress, this, &hiddfu_runner::progress, Qt::QueuedConnection);
    connect(&workerThread, &QThread::finished, worker, &QObject::deleteLater);
    connect(&workerThread, &QThread::finished, this, &hiddfu_runner::threadFinished);
    workerThread.start();
}

hiddfu_runner::~hiddfu_runner()
{
    abort();
}

void hiddfu_runner::exec(int command, const QVariantMap& params)
{
    if(busy) {
        emit error(hiddfu_worker::ERR_COMM, tr("Busy"));
        return;
    }

    bool queued = false;
    switch(command) {
        case hiddfu_worker::Setup: {
            if(connected) {
                emit error(hiddfu_worker::ERR_COMM, tr("Already connected to a device"));
                emit result(hiddfu_worker::Setup, false);
                return;
            }
            queued = QMetaObject::invokeMethod(worker, "setup", Qt::QueuedConnection, Q_ARG(QVariantMap, params));
            break;
        }
        case hiddfu_worker::Status: {
            queued = QMetaObject::invokeMethod(worker, &hiddfu_worker::getstatus, Qt::QueuedConnection);
            break;
        }
        case hiddfu_worker::Info: {
            queued = QMetaObject::invokeMethod(worker, &hiddfu_worker::getinfo, Qt::QueuedConnection);
            break;
        }
        case hiddfu_worker::CheckFW:
        case hiddfu_worker::Flash: {
            if(params.contains("file")) {
                queued = QMetaObject::invokeMethod(worker, (command == hiddfu_worker::Flash) ? "flash" : "checkfw", Qt::QueuedConnection, Q_ARG(QString, params.value("file").toString()));
            } else {
                qWarning("Check fw: file param missing");
                emit result(command, false);
                return;
            }
            break;
        }
#ifndef OD_NO_DEVELOPER
        case hiddfu_worker::FlashStor: {
            if(params.contains("file")) {
                queued = QMetaObject::invokeMethod(worker, "storage_write", Qt::QueuedConnection, Q_ARG(QString, params.value("file").toString()), Q_ARG(int, params.value("index", -1).toInt()));
            } else {
                qWarning("Storage upload: file param missing");
                emit result(command, false);
                return;
            }
            break;
        }
        case hiddfu_worker::ReadStor: {
            if(params.contains("file")) {
                QList<QVariant> vlocs = params.value("locations").toList();
                QList<LocationTypeDef> locs;
                for(const QVariant& v : qAsConst(vlocs)) {
                    if(v.canConvert<LocationTypeDef>()) {
                        locs.append(v.value<LocationTypeDef>());
                    }
                }
                queued = QMetaObject::invokeMethod(worker, "storage_read", Qt::QueuedConnection,
                                          Q_ARG(QString, params.value("file").toString()),
                                          Q_ARG(QList<LocationTypeDef>, locs),
                                          Q_ARG(QString, params.value("info").toString()));
            } else {
                qWarning("Storage download: file param missing");
                emit result(command, false);
                return;
            }
            break;
        }
        case hiddfu_worker::EraseStor: {
            queued = QMetaObject::invokeMethod(worker, &hiddfu_worker::storage_erase, Qt::QueuedConnection);
            break;
        }
#endif
        case hiddfu_worker::EraseEEPROM: {
            queued = QMetaObject::invokeMethod(worker, &hiddfu_worker::eeprom_erase, Qt::QueuedConnection);
            break;
        }
        case hiddfu_worker::Reboot: {
            if(params.contains("mode")) {
                queued = QMetaObject::invokeMethod(worker, "reboot", Qt::QueuedConnection, Q_ARG(int, params.value("mode").toInt()));
            } else {
                qWarning()<<"Not enough params - reboot mode missing";
                emit result(hiddfu_worker::Reboot, false);
                return;
            }
            break;
        }
        default: {
            qWarning("Unsupported hiddfu exec command %d", command);
            emit result(command, false);
            return;
        }
    }

    if(!queued) {
        emit error(hiddfu_worker::ERR_COMM, tr("Cannot queue DFU command"));
        emit result(command, false);
        return;
    }
    busy = true;
}

void hiddfu_runner::abort()
{
    if(worker && workerThread.isRunning())
    {
        worker->abort();
        workerThread.requestInterruption();
        workerThread.quit();
        workerThread.wait();
    }
    worker = nullptr;
    busy = false;
    connected = false;
}

void hiddfu_runner::done(int cmd, bool status)
{
    qWarning("hiddfu_worker finished %d with status: %d", cmd, status);
    busy = false;
    switch(cmd) {
        case hiddfu_worker::Setup: {
            connected = status;
            break;
        }
        default:break;
    }
    emit result(cmd, status);
}

void hiddfu_runner::threadFinished()
{
    qWarning()<<"hiddfu_worker thread finished";
}
