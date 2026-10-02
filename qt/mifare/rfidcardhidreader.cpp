#include "rfidcardhidreader.h"

#include <QStringBuilder>

#ifdef Q_OS_ANDROID
#include <hidproxy_android.h>
#define HIDProxyPlatform HIDProxyAndroid
#else
#include <hidproxy_desktop.h>
#define HIDProxyPlatform HIDProxyDesktop
#endif

#include <QTimerEvent>

#define OD_VID      0x0483
#define OD_RFID_HID 0xA26A

#define HID_REPORT_ID_KBD            0x01
#define HID_REPORT_ID_CTRL           0x02
#define HID_REPORT_CTRL_SIZE         0x3F

namespace {
const char ODRFID_HID_PRODUCT_RX[] =
        "(?:USB HID RFID|ODRFID HID Interface|ODRFID3(?:-[MNE])?)";

bool isVendorHidDescriptor(const HIDProxy::Descriptor& descriptor)
{
    if(descriptor.product == QLatin1String("USB HID RFID"))
        return true;

    return (descriptor.product == QLatin1String("ODRFID HID Interface")) ||
           (descriptor.interfaceNumber == 3) ||
           (descriptor.usagePage == 0xFF00) ||
           (descriptor.product.startsWith(QLatin1String("ODRFID3")) &&
            (descriptor.interfaceNumber < 0) && !descriptor.usagePage);
}
}

namespace Legacy {
typedef enum {
    CMD_INV = 0,
    CMD_INFO,
    CMD_LED,
    CMD_BUZZ,
    CMD_SCAN_ALL,
    CMD_SCAN_TAG,
    CMD_NEXT_TAG,
    CMD_CHECK_TAG,
    CMD_SEL_TAG,
    CMD_TAG_INFO,
    CMD_READ_TAG,
    CMD_WRITE_TAG,
    CMD_SET_LED,
    CMD_SET_BUZZ,
    CMD_SET_GAIN,
    CMD_SET_INTERVAL,
    CMD_SET_KEY,
    CMD_SET_CAPS,
    CMD_SET_CRNL,
    CMD_SET_WSAK,
    CMD_SET_WRITE,
    CMD_ANT_STATE,
    CMD_SCAN_MODE,
    CMD_REBOOT_UAPP,
    CMD_REBOOT_DFU,
} HID_CMD_ID;
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

#define CMD_PAIR(hash,name) hash.insert(CMD_##name, Legacy::CMD_##name)
#define CMD_INV(hash,name)  hash.insert(CMD_##name, Legacy::CMD_INV)

QHash<int,int> fillLegacySet()
{
    QHash<int,int> ret;
    CMD_PAIR(ret,INV);
    CMD_PAIR(ret,INV);
    CMD_PAIR(ret,INFO);
    CMD_PAIR(ret,LED);
    CMD_PAIR(ret,BUZZ);
    CMD_PAIR(ret,SCAN_ALL);
    CMD_PAIR(ret,SCAN_TAG);
    CMD_PAIR(ret,NEXT_TAG);
    CMD_PAIR(ret,CHECK_TAG);
    CMD_PAIR(ret,SEL_TAG);
    CMD_PAIR(ret,TAG_INFO);
    CMD_PAIR(ret,READ_TAG);
    CMD_PAIR(ret,WRITE_TAG);
    CMD_INV(ret,INCREMENT);
    CMD_INV(ret,DECREMENT);
    CMD_INV(ret,WRITEEXT);
    CMD_INV(ret,COMMITPERSO);
    CMD_INV(ret,READEXT);
    CMD_INV(ret,PLUSAUTH);
    CMD_INV(ret,TAG_CHUID);
    CMD_PAIR(ret,SET_LED);
    CMD_PAIR(ret,SET_BUZZ);
    CMD_PAIR(ret,SET_GAIN);
    CMD_PAIR(ret,SET_INTERVAL);
    CMD_PAIR(ret,SET_KEY);
    CMD_PAIR(ret,SET_CAPS);
    CMD_PAIR(ret,SET_CRNL);
    CMD_PAIR(ret,SET_WSAK);
    CMD_INV(ret,SET_BLOCK);
    CMD_PAIR(ret,SET_WRITE);
    CMD_PAIR(ret,ANT_STATE);
    CMD_PAIR(ret,SCAN_MODE);
    CMD_PAIR(ret,REBOOT_UAPP);
    CMD_PAIR(ret,REBOOT_DFU);

    return ret;
}

static const QHash<int,int> legacy_cmd_set = fillLegacySet();

typedef enum {
    HID_PARAM_QUERY = 0,
    HID_PARAM_ON,
    HID_PARAM_OFF,
    HID_PARAM_SWITCH,
    HID_PARAM_DEFAULT,
} HID_CMD_PARAM;


RFIDCardHIDReader::RFIDCardHIDReader(QObject *parent) : RFIDCardHardwareReader(parent),
    handle(nullptr), legacy_cmdset(false), new_card_requested(false)
{
    m_card_present = false;
    current_card_info.clear();
}

RFIDCardHIDReader::~RFIDCardHIDReader()
{
    if(handle)
    {
        quint8 cmd = CMD_REBOOT_UAPP;
        handle->sendPacket(&cmd, 1, HID_REPORT_ID_CTRL);
        handle->close();
        delete handle;
    }
}

QStringList RFIDCardHIDReader::availableDevices()
{
    QStringList ret;
    const QList<HIDProxy::Descriptor> devices =
            HIDProxyPlatform().enumerateHidDevices(OD_VID, OD_RFID_HID,
                                                   QLatin1String(ODRFID_HID_PRODUCT_RX));
    for(const HIDProxy::Descriptor& dev : devices) {
        if(isVendorHidDescriptor(dev)) {
            ret.append(dev.toString().split(QLatin1Char(':')).mid(2).join(QLatin1String(":")) +
                       QLatin1String(" (HID)"));
        }
    }
    return ret;
}

QList<PortDescriptor> RFIDCardHIDReader::availableDeviceList()
{
    QList<PortDescriptor> ret;
    const QList<HIDProxy::Descriptor> devices =
            HIDProxyPlatform().enumerateHidDevices(OD_VID, OD_RFID_HID,
                                                   QLatin1String(ODRFID_HID_PRODUCT_RX));
    for(const HIDProxy::Descriptor& dev : devices) {
        if(!isVendorHidDescriptor(dev))
            continue;

        const QString serial = dev.serial.isEmpty() ? QStringLiteral("S/N") : dev.serial;
        ret.append(PortDescriptor(dev.product % QLatin1String(" (") % serial % QLatin1String(")"),
                                  QVariant(dev.toString().split(QLatin1Char(':')).mid(2).join(QLatin1String(":")) %
                                           QLatin1String(" (HID)"))));
    }
    return ret;
}

QString RFIDCardHIDReader::device() const
{
    if(handle)
    {
        return handle->device();
    }
    return QString();
}

void RFIDCardHIDReader::setDevice(const QString& name)
{
    if(handle)
    {
        close();
    }

    QRegExp devNameRX("(.*)\\s+\\(HID\\)");

    if(devNameRX.exactMatch(name))
    {
        QString devSN = devNameRX.cap(1);
        if(devSN.isEmpty())
        {
            qWarning()<<"Invalid serial number in device description "<<name;
            emit disconnected();
            return;
        }
#ifdef Q_OS_ANDROID
        if(devSN==QLatin1String("S/N"))
        {
            qWarning()<<"Cannot identify a serial-less HID device on Android";
            emit disconnected();
            return;
        }
#endif

        //open - send `SCAN0` cmd
        handle = new HIDProxyPlatform(this);
        if(!handle->setDevice(QString("%1:%2:").arg(OD_VID, 4, 16, QLatin1Char('0')).arg(OD_RFID_HID, 4, 16, QLatin1Char('0')) + devSN))
        {
            qWarning()<<"Cannot open HID device "<<devSN;
            emit disconnected();
            delete handle;
            handle = nullptr;
            return;
        }

        connect(handle, &HIDProxy::disconnected, this, &RFIDCardHIDReader::handleDisconnected);
        handle->setReportID(HID_REPORT_ID_CTRL);
        connect(handle, &HIDProxy::processInRequest, this, &RFIDCardHIDReader::hidQuery);
        handle->setPollInterval(25);
        sendCmd(CMD_INFO, 0);
    }
    else
    {
        qWarning()<<"Invalid device description "<<name<<": must be \"S/N (HID / path)\"";
        emit disconnected();
    }
}

bool RFIDCardHIDReader::isOpen() const
{
    return handle && handle->isOpen();
}

void RFIDCardHIDReader::close()
{
    RFIDCardReaderInterface::close();
    stopReplyTimer();
    commandQueue.clear();
    if(!handle)
    {
        return;
    }
    handle->close();
    handle->deleteLater();
    handle = nullptr;
}

QByteArray RFIDCardHIDReader::getUID()
{
    return m_card_present ? current_card_info.uid : QByteArray();
}

void RFIDCardHIDReader::setUID(const QByteArray& uid)
{
    if((uid.size() != 4) && (uid.size() != 7) && (uid.size() != 10))
    {
        qWarning()<<"Cannot change card UID - invalid UID length "<<uid.size();
        emit uidChanged(false);
        return;
    }
    QByteArray param(1+uid.size(), char(0));
    *(reinterpret_cast<quint8*>(param.data())) = quint8(uid.size());
    memcpy(param.data()+1, uid.constData(), uid.size());
    if(!sendPacket(CMD_TAG_CHUID, param))
    {
        emit uidChanged(false);
    }
}

void RFIDCardHIDReader::writeEM(const QByteArray& uid, const QByteArray& key, const QByteArray& pwd, int coding, int bits)
{
    QByteArray params(2, '\0');
    params[0] = (char)(coding ? HID_PARAM_ON : HID_PARAM_OFF);
    params[1] = (char)(bits ? HID_PARAM_ON : HID_PARAM_OFF);

    if(key.size() && !sendPacket(CMD_EM_UNROP, key))
    {
        emit error("Unlock write failure");
        return;
    }

    if(!sendPacket(CMD_EM_PARAMS, params) ||
       (pwd.isEmpty() ? !sendPacket(CMD_EM_WRITE, uid) : !sendPacket(CMD_EM_XWRITE, pwd+uid)))
    {
        emit error("Clone write failure");
    }
    /* card gone or changed at this moment - force! */
    removeCard();
}

MifareCards RFIDCardHIDReader::getType() const
{
    return m_card_present ? ((MifareCards)current_card_info.type) : MF_UNKNOWN;
}

bool RFIDCardHIDReader::sendPacket(quint8 cmd, const QByteArray& param)
{
    if(param.size() > (HID_REPORT_CTRL_SIZE - 1))
    {
        qWarning()<<"HID command payload is too large: "<<param.size();
        return false;
    }

    cmd = (legacy_cmdset ? legacy_cmd_set.value(cmd,CMD_INV) : cmd);
    qWarning()<<"HID OUT report: "<<QString("0x%1:").arg(cmd, 2, 16, QLatin1Char('0'))<<param;

    QByteArray hid_report(HID_REPORT_CTRL_SIZE + 1, char(0));
    uint8_t* hid_report_out = (uint8_t*)hid_report.data();

    hid_report_out[0] = HID_REPORT_ID_CTRL;
    hid_report_out[1] = cmd;
    if(param.size())
    {
        memcpy(hid_report_out+2,param.constData(),qMin<int>(param.size(),HID_REPORT_CTRL_SIZE-1));
    }
    else
    {
        memset(hid_report_out+2,0,HID_REPORT_CTRL_SIZE-1);
    }

    if(reply_timer && reply_timer->isActive())
    {
        commandQueue.enqueue(hid_report);
        return true;
    }
    else
    {
        startReplyTimer(900);
        const bool sent = transmit(hid_report);
        if(!sent)
        {
            stopReplyTimer();
        }
        return sent;
    }
}

bool RFIDCardHIDReader::plusAuthKey(quint16 keyBrn)
{
    QByteArray param(2, '\0');
    write_uint16_le(keyBrn, param.data());
    return sendPacket(CMD_PLUSAUTH, param);
}

bool RFIDCardHIDReader::hal_transmit(const QByteArray &data)
{
    if(!handle || (data.size() != (HID_REPORT_CTRL_SIZE + 1)))
    {
        return false;
    }
    // HIDProxy expects the report payload and report ID separately.
    return handle->sendPacket(data.constData() + 1, size_t(data.size() - 1),
                              quint8(data.at(0)));
}

void RFIDCardHIDReader::setKey(MifareClassicKeyType type, const MifareClassicKey& key)
{
    if(!isOpen() || !m_card_present)
    {
        qWarning()<<"Cannot set key - connection closed or no card";
        emit keyChanged(false);
    }
    else
    {
        QByteArray param(1+6,'\0');
        param[0] = (type==MifareClassicKeyA)?'A':'B';
        memcpy(param.data()+1,key.raw(),6);

        bool res = sendPacket(CMD_SET_KEY, param);
        if(!res)
        {
            emit keyChanged(false);
        }
        else
        {
            mfc_key_type = type;
            mfc_key = key;
        }
    }

}

void RFIDCardHIDReader::setPassword(quint32 pwd)
{
    if(!isOpen())
    {
        qWarning()<<"Cannot set password - connection closed or no card";
        emit passwordChanged(false, ul_pwd);
    }
    else
    {
        QByteArray param(1+4, '\0');
        param[0] = 'U';
        write_uint32_le(pwd, param.data()+1);
        bool res = sendPacket(CMD_SET_KEY, param);
        if(!res)
        {
            emit passwordChanged(false, ul_pwd);
        }
        else
        {
            ul_pwd = pwd;
        }
    }
}

void RFIDCardHIDReader::setPlusKey(MifareClassicKeyType type, const MifarePlusKey& key)
{
    if(!isOpen())
    {
        qWarning()<<"Cannot set aes key - connection closed";
        emit plusKeyChanged(false);
    }
    else if(fw_version < QVersionNumber(1, 6))
    {
        qWarning()<<"Cannot set aes key - not supported";
        emit plusKeyChanged(false);
    }
    else
    {
        QByteArray param(1+16, '\0');
        param[0] = 'X';
        memcpy(param.data()+1, key.raw(), 16);
        bool res = sendPacket(CMD_SET_KEY, param);
        if(!res)
        {
            emit plusKeyChanged(false);
        }
        else
        {
            mfp_aes_type = type;
            mfp_aes_key = key;
        }
    }
}

void RFIDCardHIDReader::readPack()
{
    if(!sendPacket(CMD_SET_KEY, QByteArray("P")))
    {
        emit packReceived(false, ul_pack);
    }
}

#define INVOKE_READFAIL(result) \
    QMetaObject::invokeMethod(this, "blockRead", Q_ARG(int, blockAddress), Q_ARG(int, 0), Q_ARG(QByteArray, QByteArray()), Q_ARG(int, result))
#define INVOKE_WRITEFAIL(result) \
    QMetaObject::invokeMethod(this, "blockWritten", Q_ARG(int, blockAddress), Q_ARG(int, result))

void RFIDCardHIDReader::readBlock(quint16 blockAddress, int blockCount)
{
    if(!m_card_present)
    {
        qWarning()<<"Cannot read block - no card";
        INVOKE_READFAIL(RW_Fatal);
        return;
    }
    else if(block_count)
    {
        qWarning()<<"Cannot read block - busy";
        INVOKE_READFAIL(RW_Retry);
        return;
    }
    else if((blockCount <= 0) || (blockAddress >= current_card_info.blocks) ||
            (blockCount > (current_card_info.blocks - blockAddress)))
    {
        qWarning()<<"Cannot read block(s) - out of bounds";
        INVOKE_READFAIL(RW_Invarg);
        return;
    }

    qWarning()<<"Requested read of blocks ["<<blockAddress<<" - "<<(blockAddress+blockCount-1)<<"]";
    block_address = blockAddress;
    block_count   = blockCount;

    readBlockInt(blockAddress);
}

void RFIDCardHIDReader::readBlockInt(uint16_t blockAddress)
{
    bool res = false;
    if(RFIDCardReaderInterface::isExtCard(current_card_info.type))
    {
        QByteArray param(2, char(0));
        write_uint16_le(blockAddress, param.data());
        res = sendPacket(CMD_READEXT, param);
    }
    else if(fw_version >= QVersionNumber(1, 6))
    {
        if(current_card_info.block_size == 0)
        {
            qWarning()<<"Cannot read block - invalid card block size";
            block_count=0;
            INVOKE_READFAIL(RW_Invarg);
            return;
        }
        const int maxBatchCount = 55 / current_card_info.block_size;
        if(maxBatchCount <= 0)
        {
            qWarning()<<"Cannot read block - block size does not fit into a HID report";
            block_count=0;
            INVOKE_READFAIL(RW_Invarg);
            return;
        }
        qWarning()<<"Batch read support detected";
        QByteArray param(2, char(0));
        param[0] = static_cast<char>(blockAddress & 0xFF);
        param[1] = static_cast<char>(qMin(block_count, maxBatchCount));
        res = sendPacket(CMD_READ_BATCH, param);
    }
    else
    {
        res = sendCmd(CMD_READ_TAG,quint8(blockAddress&0xFF));
    }

    if(!res)
    {
        qWarning()<<"Cannot read block - communication error";
        block_count=0;
        INVOKE_READFAIL(RW_Fatal);
    }
}

void RFIDCardHIDReader::writeBlock(quint16 blockAddress, const QByteArray& blk)
{
    if(!m_card_present)
    {
        qWarning()<<"Cannot write block - no card";
        INVOKE_WRITEFAIL(RW_Fatal);
        return;
    }
    else if(block_count)
    {
        qWarning()<<"Cannot write block - busy";
        INVOKE_WRITEFAIL(RW_Retry);
        return;
    }
    else if((blockAddress>=current_card_info.blocks) ||
            (blk.size() != current_card_info.block_size) || blk.isEmpty())
    {
        qWarning()<<"Cannot write block - out of bounds";
        INVOKE_WRITEFAIL(RW_Invarg);
        return;
    }

    const quint8 *data = (const quint8*)blk.constData();
    int blockSize = blk.size();

    block_address = blockAddress;

    QByteArray param;
    quint8 cmd;
    if(RFIDCardReaderInterface::isExtCard(current_card_info.type))
    {
        cmd = CMD_WRITEEXT;
        param = QByteArray(2+1+blockSize, char(0));
        write_uint16_le(blockAddress, param.data());
        *((unsigned char*)(param.data()+2)) = (unsigned char)(blockSize&0xFF);
        memcpy(param.data()+3, data, blockSize);
    }
    else
    {
        cmd = CMD_WRITE_TAG;
        param = QByteArray(1+1+blockSize,'\0');
        *((unsigned char*)param.data()) = (unsigned char)(block_address&0xFF);
        *((unsigned char*)(param.data()+1)) = (unsigned char)(blockSize&0xFF);
        memcpy(param.data()+2, data, blockSize);
    }

    if(!sendPacket(cmd, param))
    {
        qWarning()<<"Cannot write block - communication error";
        INVOKE_WRITEFAIL(RW_Fatal);
        return;
    }
}

bool RFIDCardHIDReader::selectCard(const QByteArray &uid)
{
    if((uid.size() != 4) && (uid.size() != 5) && (uid.size() != 6) &&
       (uid.size() != 7) && (uid.size() != 10))
    {
        qWarning()<<"Cannot select card - invalid UID length "<<uid.size();
        return false;
    }
    if(m_card_present)
    {
        clearCard();
        emit cardRemoved();
    }

    QByteArray param(1 + uid.size(), char(0));
    param[0] = static_cast<char>(uid.size());
    memcpy(param.data() + 1, uid.constData(), uid.size());
    return sendPacket(CMD_SEL_TAG, param);
}

void RFIDCardHIDReader::removeCard()
{
    if(m_card_present)
    {
        clearCard();
        new_card_requested = true;
        emit cardRemoved();
    }
}

void RFIDCardHIDReader::clearCard()
{
    current_card_info.clear();
    block_count = 0;
    m_card_present = false;
}

void RFIDCardHIDReader::poll()
{
    if(!sendPacket(new_card_requested ? CMD_NEXT_TAG : CMD_SCAN_TAG))
    {
        if(m_card_present)
        {
            clearCard();
            emit cardRemoved();
        }
        new_card_requested = false;
        return;
    }
    new_card_requested = false;
}

void RFIDCardHIDReader::reboot()
{
    stopReplyTimer();
    sendCmd(CMD_REBOOT_UAPP, HID_PARAM_ON);
}

bool RFIDCardHIDReader::dfu()
{
    stopReplyTimer();
    if(sendCmd(CMD_REBOOT_DFU, HID_PARAM_ON)) {
        emit misc(MC_DFUResult, RFIDCardReaderInterface::hasCapability(fw_version, RFIDCardReaderInterface::DFU_MS) ? int(DExternal) : int(DBuiltin));
        return true;
    }
    emit misc(MC_DFUResult, int(DFailure));
    return false;
}

bool RFIDCardHIDReader::selectIface(int sel)
{
    Q_UNUSED(sel);
#if 0 // not implemented/obsolete
    if(fw_version.microVersion() >= RFIDCardReaderInterface::REV_ThirdGen_High) {
        if(sendCmd(CMD_SET_IFACE, sel)) {
            reboot();
            return true;
        }
    }
#endif
    /* Not supported or failed */
    return false;
}

void RFIDCardHIDReader::hidQuery(const QByteArray& report)
{
    if(report.size() != (HID_REPORT_CTRL_SIZE+1))
    {
        qWarning()<<"Invalid RFID report - expected "<<(HID_REPORT_CTRL_SIZE+1)<<" bytes, got "<<report.size();
        return;
    }

    const uint8_t* hid_report_in = (const uint8_t*)report.constData();

    //qWarning()<<"read HID report of size "<<ret<<" - starts with "<<int(hid_report_in[0])<<", "<<int(hid_report_in[1]);
    if(hid_report_in[0]==HID_REPORT_ID_CTRL)
    {
        stopReplyTimer();
        if(!commandQueue.isEmpty())
        {
            startReplyTimer(900);
            if(!transmit(commandQueue.dequeue()))
            {
                stopReplyTimer();
                commandQueue.clear();
                QMetaObject::invokeMethod(this, "nack", Qt::QueuedConnection);
            }
        }

        const uint8_t* data = hid_report_in+1+1+4;
        const int dataSize = report.size() - int(data - hid_report_in);
        const auto hasData = [dataSize](int required) {
            return (required >= 0) && (required <= dataSize);
        };
        uint32_t ret_code = read_uint32_le(hid_report_in+2);
        int hid_cmd = (legacy_cmdset ? legacy_cmd_set.key(hid_report_in[1],CMD_INV) : hid_report_in[1]);

        if(ret_code)
        {
            QString error_desr;
            switch(hid_cmd)
            {
                case CMD_INFO:
                {
                    dev_fw = QStringLiteral("?.??");
                    fw_version = QVersionNumber();
                    emit error(tr("Invalid firmware information reply"));
                    emit versionChanged(dev_fw);
                    emit fullVersionChanged(dev_fw, fw_version);
                    break;
                }
                case CMD_READ_TAG:
                case CMD_READEXT:
                case CMD_READ_BATCH:
                {
                    checkStatus(ret_code,false,error_desr);
                    //reset
                    block_count = 0;
                    emit error(tr("Block %1 not read: %2").arg(block_address).arg(error_desr));
                    break;
                }
                case CMD_WRITE_TAG:
                case CMD_WRITEEXT:
                {
                    checkStatus(ret_code,true,error_desr);
                    emit error(tr("Block %1 not written: %2").arg(block_address).arg(error_desr));
                    break;
                }
                case CMD_PLUSAUTH:
                {
                    emit error(tr("Authorization error 0x%1")
                               .arg(ret_code, 8, 16, QLatin1Char('0')));
                    emit plusAuthorized(false);
                    break;
                }
                case CMD_TAG_CHUID:
                {
                    qWarning()<<"CHUID command ended with code "<<QString("0x%1").arg(ret_code, 8, 16, QLatin1Char('0'));
                    emit error(tr("UID could not be changed: either the firmware is too old or the tag's UID is not rewritable"));
                    emit uidChanged(false);
                    break;
                }
                case CMD_SCAN_TAG:
                case CMD_NEXT_TAG:
                {
                    if(m_card_present)
                    {
                        clearCard();
                        emit cardRemoved();
                    }
                    emit polled();
                    break;
                }
                case CMD_TAG_INFO:
                {
                    qWarning()<<"Card info request failed: "<<ret_code;
                    if(m_card_present)
                    {
                        clearCard();
                        emit cardRemoved();
                    }
                    break;
                }
                case CMD_SET_KEY:
                {
                    if(hid_report_in[6] == 'P')
                    {
                        qWarning()<<"Utlralight auth failed";
                        emit packReceived(false, 0);
                    }
                    else if(hid_report_in[6] == 'U')
                    {
                        emit passwordChanged(false, ul_pwd);
                    }
                    else if(hid_report_in[6] == 'X')
                    {
                        emit plusKeyChanged(false);
                    }
                    else
                    {
                        qWarning()<<"Key not set!";
                        emit error(tr("Key not set"));
                        emit keyChanged(false);
                        //emit passwordChanged(false, ul_pwd);
                        //emit plusKeyChanged(false);
                    }
                    break;
                }
                default:
                {
                    qWarning()<<"Command "<<int(hid_report_in[1])<<" returned an error "<<ret_code;
                    break;
                }
            }
            return;
        }

        /* CMD Success */
        switch(hid_cmd)
        {
            case CMD_INFO:
            {
                /*CMD | 4byte code | LEN | DATA*/
                //qWarning()<<"INFO data: "<<QString::fromLatin1(QByteArray((const char*)data, 64 - (data-hid_report_in)).toHex().toUpper());
                if(!hasData(1 + int(data[0])))
                {
                    qWarning()<<"Invalid firmware information reply length "<<int(data[0]);
                    emit error(tr("Invalid firmware information reply"));
                    break;
                }
                dev_fw = QString::fromLatin1((const char*)(data+1),int(data[0]));
                QString parsedVersion;
                if(!RFIDCardReaderInterface::parseFirmwareVersion(dev_fw, parsedVersion, fw_version))
                {
                    emit error(tr("Invalid firmware information reply"));
                }
                qWarning()<<"HID Reader fw: "<<dev_fw<<": "<<fw_version;
                emit versionChanged(dev_fw);
                emit fullVersionChanged(dev_fw, fw_version);
                if(fw_version == QVersionNumber(1, 0))
                {
                    qWarning()<<"Detected legacy cmd set";
                    legacy_cmdset = true;
                }
                else
                {
                    legacy_cmdset = false;
                }
                sendCmd(CMD_SCAN_MODE,HID_PARAM_OFF);
                break;
            }
            case CMD_LED:
            {
                qWarning()<<"LED state: "<<(*data ? "on" : "off");
                break;
            }
            case CMD_BUZZ:
            {
                qWarning()<<"BUZZ state: "<<(*data ? "on" : "off");
                break;
            }
            case CMD_SCAN_ALL:
            {
                /* LEN | UID | SAK */
                if(*data && hasData(2 + int(*data)))
                {
                    qWarning()<<"Tag detected: "<<QString::fromLatin1(QByteArray((const char*)(data+1),*data).toHex().toUpper());
                }
                else
                {
                    qWarning()<<"Scan all EOD";
                }
                break;
            }
            case CMD_SCAN_TAG:
            case CMD_NEXT_TAG:
            {
                //ret_code == 0 -> card detected
                emit polled();

                /* CMD | CODE (u32) | UID_LEN | UID | SAK | */
                const int uidSize = int(*data);
                if(!hasData(2 + uidSize))
                {
                    qWarning()<<"Invalid scan reply UID length "<<uidSize;
                    if(m_card_present)
                    {
                        clearCard();
                        emit cardRemoved();
                    }
                    break;
                }
                QByteArray uid((const char*)data+1,uidSize);
                if((uidSize==4)||(uidSize==5)||(uidSize==6)||
                   (uidSize==7)||(uidSize==10))
                {
                    if(!m_card_present || (current_card_info.uid!=uid))
                    {
                        if(m_card_present)
                        {
                            clearCard();
                            emit cardRemoved();
                        }

                        sendCmd(CMD_TAG_INFO, HID_PARAM_DEFAULT);
                    }
                }
                else if(m_card_present)
                {
                    clearCard();
                    emit cardRemoved();
                }
                break;
            }
            case CMD_CHECK_TAG:
            {
                const int uidSize = int(*data);
                if(!hasData(2 + uidSize))
                {
                    qWarning()<<"Invalid check-tag reply UID length "<<uidSize;
                    break;
                }
                qWarning()<<"Tag "<<QString::fromLatin1(QByteArray((const char*)data+1,uidSize).toHex().toUpper())
                         <<" is present, SAK "<<QString::number(int(data[1 + uidSize]),16);
                break;
            }
            case CMD_SEL_TAG:
            {
                break;
            }
            case CMD_TAG_INFO:
            {
                const int uidSize = int(*data);
                if(!hasData(uidSize + 6) ||
                   ((uidSize != 4) && (uidSize != 5) && (uidSize != 6) &&
                    (uidSize != 7) && (uidSize != 10)))
                {
                    qWarning()<<"Invalid card information reply length "<<uidSize;
                    const bool wasPresent = m_card_present;
                    clearCard();
                    if(wasPresent)
                    {
                        emit cardRemoved();
                    }
                    emit error(tr("Invalid card information reply"));
                    break;
                }
                current_card_info.uid = QByteArray((const char*)(data+1),uidSize);
                data+=current_card_info.uid.size()+1;
                current_card_info.sak = *data++;
                current_card_info.blocks = read_uint16_le(data);
                data+=2;
                current_card_info.block_size = *data++;
                current_card_info.type = *data;

                const bool lfCard = isLfCard(current_card_info.type);
                const bool uidSizeOk =
                        (current_card_info.type == MF_EM_4100 && uidSize == 5) ||
                        (current_card_info.type == MF_HID_PROX && uidSize == 6) ||
                        (!lfCard && ((uidSize == 4) || (uidSize == 7) ||
                                     (uidSize == 10)));
                const bool geometryOk = lfCard
                        ? (current_card_info.blocks == 0 &&
                           current_card_info.block_size == 0)
                        : ((isUnsupportedCard(current_card_info.type) &&
                            current_card_info.blocks == 0 &&
                            current_card_info.block_size == 0) ||
                           (current_card_info.blocks > 0 &&
                            current_card_info.block_size > 0 &&
                            current_card_info.block_size <= 55));
                if(!uidSizeOk || !geometryOk)
                {
                    qWarning()<<"Invalid card geometry in information reply";
                    const bool wasPresent = m_card_present;
                    clearCard();
                    if(wasPresent)
                    {
                        emit cardRemoved();
                    }
                    emit error(tr("Invalid card information reply"));
                    break;
                }

                if(
                   (fw_version == QVersionNumber(1,5))&&
                   ((current_card_info.type == MF_PLUS_S_SL3)||(current_card_info.type == MF_PLUS_X_SL3))
                   )
                {
                    /*partial support - make it UNSUPPORTED*/
                    current_card_info.type = MF_UNKNOWN;
                }

                if(current_card_info.type==MF_UNKNOWN)
                {
                    qWarning()<<"Unknown/unsupported card "<<mifareCardName(current_card_info.type);
                }

                qWarning()<<"Detected card - type "<<mifareCardName(current_card_info.type)
                         <<" UID "<<current_card_info.uid<<", "
                        <<current_card_info.blocks<<" blocks of "<<int(current_card_info.block_size)<<" bytes";
                m_card_present = true;
                block_count=0;
                emit cardPresent(current_card_info.uid, current_card_info.type);

                break;
            }
            case CMD_READ_BATCH:
            {
                /* data: BN, BS, BC, <tag mem> */
                const int replySize = int(data[1]);
                const int replyCount = int(data[2]);
                if((data[0] != block_address)||(data[1] != current_card_info.block_size) ||
                   (replyCount <= 0) || (replyCount > block_count) ||
                   !hasData(3 + replySize * replyCount))
                {
                   qWarning()<<"Invalid reply to 'read' request - requested block "
                            <<block_address<<" of size "<<current_card_info.block_size
                           <<", got reply for block "<<data[0]<<" of size "<<data[1];
                   block_count=0;
                   emit blockRead(block_address, 0, QByteArray(), RW_Invarg);
                   emit error(tr("Block %1 not read - invalid reply").arg(block_address));
                   break;
                }

                qWarning()<<"Read "<<int(data[2])<<" blocks of "<<block_count<<" - ["<<block_address<<"-"<<(block_address+data[2])<<"]";
                qWarning()<<"Data: "<<QString::fromLatin1(QByteArray((const char*)(data+3), data[1]*data[2]).toHex().toUpper());
                for(uint8_t i=0;i<data[2];++i)
                {
                    --block_count;
                    emit blockRead(block_address, block_count, QByteArray((const char*)(data + 3 + i*data[1]), int(data[1])), RW_OK);
                    ++block_address;
                }

                if(block_count)
                {
                    qWarning()<<"Block(s) "<<(block_address-1)<<"read - reading next (left "<<block_count<<")";
                    readBlockInt(block_address);
                }
                break;
            }
            case CMD_READ_TAG:
            case CMD_READEXT:
            {
                int replyBlock = 0;
                if(hid_cmd==CMD_READEXT)
                {
                    if(!hasData(3))
                    {
                        block_count=0;
                        emit blockRead(block_address, 0, QByteArray(), RW_Invarg);
                        break;
                    }
                    replyBlock = read_uint16_le(data);
                    data+=2;
                }
                else
                {
                    if(!hasData(2))
                    {
                        block_count=0;
                        emit blockRead(block_address, 0, QByteArray(), RW_Invarg);
                        break;
                    }
                    replyBlock = *data++;
                }

                int replySize  = *data++;
                const int headerSize = (hid_cmd==CMD_READEXT) ? 3 : 2;
                if(!hasData(headerSize + replySize))
                {
                    qWarning()<<"Invalid block reply length "<<replySize;
                    block_count=0;
                    emit blockRead(block_address, 0, QByteArray(), RW_Invarg);
                    emit error(tr("Block %1 not read - invalid reply").arg(block_address));
                    break;
                }
                QByteArray blockData = QByteArray((const char*)data, replySize);

                if((replyBlock != block_address) || (blockData.size()!=current_card_info.block_size))
                {
                    qWarning()<<"Invalid reply to 'read' request - requested block "
                             <<block_address<<" of size "<<current_card_info.block_size
                            <<", got reply for block "<<replyBlock<<" of size "<<blockData.size();
                    block_count=0;
                    emit blockRead(block_address, 0, QByteArray(), RW_Invarg);
                    emit error(tr("Block %1 not read - invalid reply").arg(block_address));
                }
                else
                {
                    qWarning()<<"Requested read "<<block_count<<" blocks - No "<<block_address<<" read";
                    --block_count;
                    qWarning()<<"Left: "<<block_count;

                    emit blockRead(replyBlock, block_count, blockData, RW_OK);

                    if(block_count)
                    {
                        qWarning()<<"Block "<<block_address<<"read - reading next (left "<<block_count<<")";
                        ++block_address;

                        readBlockInt(block_address);
                    }
                }
                break;
            }
            case CMD_WRITE_TAG:
            case CMD_WRITEEXT:
            {
                const int replyBlock = (hid_cmd == CMD_WRITEEXT) ? read_uint16_le(data) : int(*data);
                if(replyBlock != block_address)
                {
                    qWarning()<<"Invalid reply to 'write' request - requested block "
                              <<block_address<<", got reply for block "<<replyBlock;
                    emit blockWritten(block_address, RW_Invarg);
                    emit error(tr("Block %1 not written - invalid reply").arg(block_address));
                    break;
                }
                qWarning()<<"Block "<<block_address<<" written";
                emit blockWritten(block_address, RW_OK);
                break;
            }
            case CMD_PLUSAUTH:
            {
                emit plusAuthorized(true);
                break;
            }
            case CMD_TAG_CHUID:
            {
                emit uidChanged(true);
                break;
            }
            case CMD_SET_KEY:
            {
                /* CMD | TYPE | KEY (6bytes) */
                const int requiredSize = ((*data == 'A') || (*data == 'B')) ? 7 :
                                         (*data == 'U') ? 5 :
                                         (*data == 'X') ? 17 :
                                         (*data == 'P') ? 3 : 0;
                if(!requiredSize || !hasData(requiredSize))
                {
                    qWarning()<<"Invalid key reply for type "<<char(*data);
                    if(*data == 'U')
                    {
                        emit passwordChanged(false, ul_pwd);
                    }
                    else if(*data == 'X')
                    {
                        emit plusKeyChanged(false);
                    }
                    else if(*data == 'P')
                    {
                        emit packReceived(false, ul_pack);
                    }
                    else
                    {
                        emit keyChanged(false);
                    }
                    break;
                }
                if(*data == 'A')
                {
                    mfc_key_type = MifareClassicKeyA;
                    mfc_key.fromRaw(data+1);
                    qWarning()<<"Key set to type A - "<<mfc_key.toString();
                    emit keyChanged(true);
                }
                else if(*data == 'B')
                {
                    mfc_key_type = MifareClassicKeyB;
                    mfc_key.fromRaw(data+1);
                    qWarning()<<"Key set to type B - "<<mfc_key.toString();
                    emit keyChanged(true);
                }
                else if(*data == 'U')
                {
                    ul_pwd = read_uint32_le(data+1);
                    emit passwordChanged(true, ul_pwd);
                }
                else if(*data == 'X')
                {
                    mfp_aes_key.fromRaw(data+1);
                    emit plusKeyChanged(true);
                }
                else if(*data == 'P')
                {
                    emit packReceived(true, read_uint16_le(data+1));
                }
                else
                {
                    qWarning()<<"invalid key type "<<char(*data);
                    emit keyChanged(false);
                }
                break;
            }
            case CMD_SCAN_MODE:
            {
                qWarning()<<"Auto scan: "<<(*data ? "enabled" : "disabled");
                if(*data==0)
                {
                    sendCmd(CMD_ANT_STATE,HID_PARAM_ON);
                }
                break;
            }
            case CMD_ANT_STATE:
            {
                qWarning()<<"RFID ant state: "<<(*data ? "on" : "off");
                if(*data)
                {
                    sendCmd(CMD_SET_KEY,'?');
                }
                break;
            }
            case CMD_SET_IFACE:
            {
                qWarning("Iface changed to %u", *data);
                break;
            }
            case CMD_REBOOT_UAPP:
            {
                qWarning("UAPP reboot sheduled");
                break;
            }
            case CMD_REBOOT_DFU:
            {
                qWarning("DFU reboot sheduled");
                break;
            }
            default:
            {
                qWarning("Command %d success", int(hid_report_in[1]));
                break;
            }
        }
    }
}

void RFIDCardHIDReader::handleDisconnected()
{
    commandQueue.clear();
    stopReplyTimer();
    if(handle)
    {
        handle->deleteLater();
        handle = nullptr;
    }
    emit disconnected();
}

void RFIDCardHIDReader::nack()
{
    qWarning()<<"Device is not responding";
    close();
    emit disconnected();
}

void RFIDCardHIDReader::checkStatus(uint32_t code, bool write, QString& errorDescr)
{
    qDebug()<<"MFRC522 status "<<QString("0x%1 (%2)").arg(code,8,16,QLatin1Char('0')).arg(rfidUsbError(code));
    if(code & MFRC522_ERROR_AUTH)
    {
        qWarning()<<"Key authentication failed!";
        emit error(tr("Key authentication failed"));
        errorDescr = tr("authentication error");
        if(write)
        {
            emit blockWritten(block_address, RW_Denied);
        }
        else
        {
            block_count = 0;
            emit blockRead(block_address, 0, QByteArray(), RW_Denied);
        }
    }
    else if(code & (MFRC522_ERROR_TEAR|MFRC522_ERROR_TIMEOUT|MFRC522_ERROR_NACK))
    {
        if(write)
        {
            emit blockWritten(block_address, RW_Fatal);
        }
        else
        {
            block_count = 0;
            emit blockRead(block_address, 0, QByteArray(), RW_Fatal);
        }

#if 0
        errorDescr = tr("tag removed");
        m_card_present = false;
        emit error(tr("Tag removed"));
        emit cardRemoved();
#else
        //do not issue 'GONE' at all -> will be issued by 'AT+i'
#endif
    }
    else
    {
        errorDescr = tr("other error");
        if(write)
        {
            emit blockWritten(block_address, RW_Fatal);
        }
        else
        {
            block_count = 0;
            emit blockRead(block_address, 0, QByteArray(), RW_Fatal);
        }
    }
}
