#include "rfidcardcdcatreader.h"

#include <QStringBuilder>
#include <QDateTime>

namespace {
constexpr int MaxCdcReplyBufferSize = 64 * 1024;
constexpr int MaxCdcReplyLines = 64;
constexpr int LFReadTimeout = 3000;
}

RFIDCardCdcAtReader::RFIDCardCdcAtReader(QObject *parent) : RFIDCardHardwareReader(parent),
    handle(new QSerialPort(this)), rs485(new UARTCommunicator(this)), _proto(CDC),
    repl_cmd(""), new_card_requested(false),
    lf_read_state(LFRead_Idle), lf_address(0), repl_timeout(900)
{
    m_card_present = false;
    current_card_info.clear();

    connect(handle, &QSerialPort::readyRead, this, &RFIDCardCdcAtReader::msg_ready);
    connect(handle, &QSerialPort::errorOccurred, this, &RFIDCardCdcAtReader::serialError);
    connect(handle, &QSerialPort::aboutToClose, this, &RFIDCardCdcAtReader::aboutToClose);

    connect(rs485, &UARTCommunicator::readyRead, this, &RFIDCardCdcAtReader::rs485_ready);
    connect(rs485, &UARTCommunicator::disconnected, this, &RFIDCardCdcAtReader::aboutToClose);
}

RFIDCardCdcAtReader::~RFIDCardCdcAtReader()
{
    handle->disconnect(this);
    rs485->disconnect(this);
    closeInt();

    delete handle;
    handle = nullptr;

    delete rs485;
    rs485 = nullptr;
}

QStringList RFIDCardCdcAtReader::availableDevices(PROTOCOL proto)
{
    QList<QSerialPortInfo> availList = QSerialPortInfo::availablePorts();
    QStringList ret;
    if(!availList.size())
        return ret;

    ret.reserve(availList.size());
    foreach(const QSerialPortInfo& info, availList)
    {
#ifdef Q_OS_WIN
        if(info.portName()=="COM1") {continue;}
#endif
        qWarning()<<"Found port: "<<info.portName()<<" ("<<info.description()<<") - "<<info.manufacturer()<<" VID "
                 <<QString::number(info.vendorIdentifier(),16)<<", PID "<<QString::number(info.productIdentifier(),16);

        switch(proto)
        {
            case CDC:
            {
                if((info.vendorIdentifier()==0x0483)&&(info.productIdentifier()==0xa26a))
                {
                    qWarning()<<"Found true CDC READER at "<<info.portName();
                    ret.append( (!info.serialNumber().isEmpty() ? info.serialNumber() : QString("S/N")) + QString(" (CDC-AT @ %1)").arg(info.portName()));
                }
                break;
            }
            case UART:
            {
                //add all ports
                ret.append( (!info.serialNumber().isEmpty() ? info.serialNumber() : QString("S/N")) + QString(" (UART @ %1)").arg(info.portName()));
                break;
            }
            case RS485:
            {
                //add all ports
                ret.append( (!info.serialNumber().isEmpty() ? info.serialNumber() : QString("S/N")) + QString(" (RS485 @ %1)").arg(info.portName()));
                break;
            }
        }
    }
    return ret;
}

QList<PortDescriptor> RFIDCardCdcAtReader::availableDeviceList(PROTOCOL proto)
{
    QList<PortDescriptor> ret;
    QList<QSerialPortInfo> availList = QSerialPortInfo::availablePorts();

    for(const QSerialPortInfo& info : qAsConst(availList)) {
#ifdef Q_OS_WIN
        if(info.portName()=="COM1") {continue;}
#endif
        switch(proto)
        {
            case CDC: {
                if((info.vendorIdentifier() == 0x0483) && (info.productIdentifier() == 0xA26A)) {
                    const QString serial = info.serialNumber().isEmpty() ? QStringLiteral("S/N") : info.serialNumber();
                    ret.append(PortDescriptor(serial % QLatin1String("@") % info.portName() % QLatin1String(" (CDC-AT)"),
                                              QVariant(info.portName() % QLatin1String("@CDC-AT"))));
                }
                break;
            }
            case UART: {
                ret.append(PortDescriptor(info.portName() % QLatin1String(" (UART)"), QVariant(info.portName() % QLatin1String("@UART"))));
                break;
            }
            case RS485: {
                ret.append(PortDescriptor(info.portName() % QLatin1String(" (RS485)"), QVariant(info.portName() % QLatin1String("@RS485"))));
                break;
            }
        }
    }

    return ret;
}

void RFIDCardCdcAtReader::setDevice(const QString& name)
{
    closeInt();

#if 0
    QRegExp portNameRX("(?:.*)\\s+\\((CDC\\-AT|UART|RS485) \\@ (.*)\\)");
    //extract port name
    if(!portNameRX.exactMatch(name))
    {
        qWarning()<<"Cannot extract port name from "<<name;
        emit disconnected();
        return;
    }
#else
    QStringList parts = name.split('@');
#endif

    if((parts.size() != 2) || parts.at(0).isEmpty() ||
       ((parts.at(1) != QLatin1String("CDC-AT")) &&
        (parts.at(1) != QLatin1String("UART")) &&
        (parts.at(1) != QLatin1String("RS485")))) {
        qWarning()<<"Invalid serial reader description "<<name;
        emit disconnected();
        return;
    }

#if 0
    if(portNameRX.cap(1) == "RS485")
#else
    if(parts.at(1) == "RS485")
#endif
    {
        rs485->blockSignals(true);
        rs485->drop();
        rs485->setTarget(QSettings().value(QCOMPORT_SETTING_ADDR(RS485_SETTINGS_PREFIX), 0x5F).toUInt());
#if 0
        if(!rs485->open(portNameRX.cap(2)))
#else
        if(!rs485->open(parts.at(0)))
#endif
        {
            rs485->blockSignals(false);
            qWarning()<<"RS485 port "<<name<<" setup failed!";
            emit disconnected();
            return;
        }

#if 0
        qWarning()<<"Connection to "<<portNameRX.cap(2)<<"@RS485 established";
#else
        qWarning()<<"Connection to "<<parts.at(0)<<"@RS485 established";
#endif
        rs485->send("ATH\r");
        rs485->waitMessageReady(1000);
        rs485->clear();

        rs485->blockSignals(false);
        rs485->drop();
        _proto = RS485;
    }
    else
    {
        handle->blockSignals(true);
        QSettings set;
#if 0
        if(!QComPort::setupComPortFromSettings(portNameRX.cap(2), set, "CDC", *handle))
#else
        if(!QComPort::setupComPortFromSettings(parts.at(0), set, "CDC", *handle))
#endif
        {
            handle->blockSignals(false);
            qWarning()<<"Com port "<<parts.at(0)<<" setup failed!";
            emit disconnected();
            return;
        }

#if 0
        qWarning()<<"Connection to "<<portNameRX.cap(2)<<" established";
#else
        qWarning()<<"Connection to "<<parts.at(0)<<" established";
#endif
        //drop all pending data
        handle->clear();

        //do drop (for newer fw)
        handle->write("ATH\r");
        handle->flush();
        while(handle->waitForReadyRead(500)) {
            (void)handle->readAll();
        }

        handle->blockSignals(false);
#if 0
        _proto = (portNameRX.cap(1) == "UART") ? UART : CDC;
#else
        _proto = (parts.at(1) == "UART") ? UART : CDC;
#endif
    }

    //drop all buffers inside the device
    if(_proto != RS485) {
        handle->write("\r");
        handle->flush();
    }

    //disable scan
    sendPacket("I");
    sendPacket("+SCAN0");
    sendPacket("+RF=1");
}

void RFIDCardCdcAtReader::close()
{
    RFIDCardReaderInterface::close();
    closeInt();
}

QByteArray RFIDCardCdcAtReader::getUID()
{
    return m_card_present ? current_card_info.uid : QByteArray();
}

void RFIDCardCdcAtReader::setUID(const QByteArray& uid)
{
    if(!sendPacket("+~", uid.toHex()))
    {
        emit uidChanged(false);
    }
}

void RFIDCardCdcAtReader::writeEM(const QByteArray& uid, const QByteArray& key, const QByteArray& pwd, int coding, int bits)
{
    qWarning()<<"AT: Requested clone of "<<uid;
    if(key.size() && !sendPacket("+u", key.toHex()))
    {
        emit error("Unlock write failure");
        return;
    }

    if(!sendPacket("+c", QString("%1,%2").arg(!!coding).arg(!!bits).toLatin1()) ||
       (pwd.isEmpty() ? !sendPacket("+@", uid.toHex()) : !sendPacket("+x", pwd.toHex()+QByteArray(",")+uid.toHex()))) {
        emit error("Clone write failure");
    }
    /* card gone or changed at this moment - force! */
    removeCard();
}

MifareCards RFIDCardCdcAtReader::getType() const
{
    return m_card_present ? ((MifareCards)current_card_info.type) : MF_UNKNOWN;
}

void RFIDCardCdcAtReader::readLfMemory(bool fullScan, const QByteArray& password)
{
    LFMemoryReadResult failed;
    if(!isOpen())
    {
        failed.error = tr("RFID reader is not connected");
        emit lfMemoryReadFinished(failed);
        return;
    }
    if(lf_read_state != LFRead_Idle)
    {
        failed.error = tr("A 125 kHz memory operation is already running");
        emit lfMemoryReadFinished(failed);
        return;
    }
    if(!password.isEmpty() && password.size() != 4)
    {
        failed.error = tr("T55xx password must contain exactly 8 HEX characters");
        emit lfMemoryReadFinished(failed);
        return;
    }

    lf_read_result = LFMemoryReadResult();
    lf_password = password;
    lf_address = 0;
    lf_read_state = LFRead_Classify;
    if(!sendLfMemoryPacket(QStringLiteral("+LFCLASS="),
                           fullScan ? QByteArrayLiteral("FULL")
                                    : QByteArrayLiteral("FAST")))
    {
        return;
    }
}

bool RFIDCardCdcAtReader::sendLfMemoryPacket(const QString& cmd,
                                             const QByteArray& param)
{
    if(sendPacket(cmd, param, LFReadTimeout))
    {
        return true;
    }

    finishLfMemoryRead(tr("Could not send the 125 kHz memory command"));
    return false;
}

QString RFIDCardCdcAtReader::lfPacketError(const QString& fallback) const
{
    for(const QByteArray& line : cmd_reply)
    {
        if(line.startsWith(QByteArrayLiteral("+CME ERROR: ")))
        {
            bool ok = false;
            const uint code = line.mid(12).toUInt(&ok);
            if(ok)
            {
                if(code == MFRC522_ERROR_COMM)
                {
                    return tr("Communication error with the 125 kHz tag");
                }
                return tr("Reader error 0x%1").arg(code, 8, 16, QLatin1Char('0'));
            }
        }
    }
    return fallback;
}

void RFIDCardCdcAtReader::setLfBlockError(int block, const QString& error)
{
    if(block >= 0 && block < lf_read_result.blockErrors.size())
    {
        lf_read_result.blockErrors[block] = error;
    }
}

bool RFIDCardCdcAtReader::prepareLfMemoryRead(
        const LFMemoryClassification& classification)
{
    lf_read_result.classification = classification;

    if(!classification.hasMemoryChip())
    {
        if(classification.status == QLatin1String("AIR_ONLY"))
        {
            finishLfMemoryRead(tr("The air protocol was detected, but the memory chip family was not identified"));
        }
        else if(classification.status == QLatin1String("NO_SIGNAL"))
        {
            finishLfMemoryRead(tr("No 125 kHz tag signal was detected"));
        }
        else if(classification.status == QLatin1String("BUSY"))
        {
            finishLfMemoryRead(tr("The RFID reader is busy"));
        }
        else
        {
            finishLfMemoryRead(tr("The 125 kHz memory chip was not identified"));
        }
        return false;
    }

    if(!LFMemoryProtocol::isT55xx(classification.chipType) &&
       !LFMemoryProtocol::isEm4x05(classification.chipType) &&
       classification.chipType != QLatin1String("EM4X50"))
    {
        finishLfMemoryRead(tr("Memory reading is not supported for %1")
                           .arg(classification.chipType));
        return false;
    }

    lf_read_result.blockLabels = LFMemoryProtocol::blockLabels(classification.chipType);
    lf_read_result.blocks.resize(lf_read_result.blockLabels.size());
    lf_read_result.blockErrors.clear();
    for(int i = 0; i < lf_read_result.blockLabels.size(); ++i)
    {
        lf_read_result.blockErrors.append(QString());
    }

    const int seed = LFMemoryProtocol::seedBlock(classification);
    if(seed >= 0 && seed < lf_read_result.blocks.size() &&
       classification.chipInfo.size() == 4)
    {
        lf_read_result.blocks[seed] = classification.chipInfo;
    }

    lf_address = 0;
    if(LFMemoryProtocol::isT55xx(classification.chipType))
    {
        lf_read_state = LFRead_T55xx;
    }
    else if(LFMemoryProtocol::isEm4x05(classification.chipType))
    {
        lf_read_state = LFRead_Em4x05;
    }
    else
    {
        lf_read_state = LFRead_Stream;
    }
    readNextLfMemoryBlock();
    return true;
}

void RFIDCardCdcAtReader::readNextLfMemoryBlock()
{
    if(lf_read_state == LFRead_T55xx)
    {
        if(lf_address < 8)
        {
            QByteArray param = QByteArrayLiteral("T,0,") + QByteArray::number(lf_address);
            if(!lf_password.isEmpty())
            {
                param += QByteArrayLiteral(",P,") + lf_password.toHex().toUpper();
            }
            sendLfMemoryPacket(QStringLiteral("+LFREAD="), param);
            return;
        }

        if(lf_read_result.classification.chipType == QLatin1String("T5577") ||
           lf_read_result.classification.chipType == QLatin1String("T5555"))
        {
            lf_read_state = LFRead_Trace;
            sendLfMemoryPacket(QStringLiteral("+LFTRACE?"));
            return;
        }

        finishLfMemoryRead();
        return;
    }

    if(lf_read_state == LFRead_Em4x05)
    {
        if(lf_address < 16)
        {
            sendLfMemoryPacket(QStringLiteral("+LFREAD="),
                               QByteArrayLiteral("E,") + QByteArray::number(lf_address));
        }
        else
        {
            finishLfMemoryRead();
        }
        return;
    }

    if(lf_read_state == LFRead_Stream)
    {
        sendLfMemoryPacket(QStringLiteral("+LFSTREAM?"));
    }
}

bool RFIDCardCdcAtReader::processLfMemoryPacket(bool success)
{
    if(lf_read_state == LFRead_Idle)
    {
        return false;
    }

    const bool expectedCommand =
            (lf_read_state == LFRead_Classify && repl_cmd == QLatin1String("+LFCLASS=")) ||
            (lf_read_state == LFRead_LegacyInfo && repl_cmd == QLatin1String("+LFINFO?")) ||
            ((lf_read_state == LFRead_T55xx || lf_read_state == LFRead_Em4x05) &&
             repl_cmd == QLatin1String("+LFREAD=")) ||
            (lf_read_state == LFRead_Trace && repl_cmd == QLatin1String("+LFTRACE?")) ||
            (lf_read_state == LFRead_Stream && repl_cmd == QLatin1String("+LFSTREAM?"));
    if(!expectedCommand)
    {
        return false;
    }

    if(lf_read_state == LFRead_Classify)
    {
        LFMemoryClassification classification;
        if(success && LFMemoryProtocol::parseClassification(cmd_reply, classification))
        {
            prepareLfMemoryRead(classification);
        }
        else
        {
            // LFINFO was the first public LF-memory API. Keep it as a fallback
            // for readers that predate the structured LFCLASS response.
            lf_read_state = LFRead_LegacyInfo;
            sendLfMemoryPacket(QStringLiteral("+LFINFO?"));
        }
        return true;
    }

    if(lf_read_state == LFRead_LegacyInfo)
    {
        QString airProtocol = QStringLiteral("UNKNOWN");
        if(current_card_info.type == MF_HID_PROX)
        {
            airProtocol = QStringLiteral("HID_PROX");
        }
        else if(current_card_info.type == MF_EM_4100)
        {
            airProtocol = QStringLiteral("EM4100_COMPAT");
        }

        LFMemoryClassification classification;
        if(success && LFMemoryProtocol::parseLegacyInfo(cmd_reply, airProtocol,
                                                        classification))
        {
            prepareLfMemoryRead(classification);
        }
        else
        {
            finishLfMemoryRead(lfPacketError(
                tr("The reader firmware does not support 125 kHz memory reading")));
        }
        return true;
    }

    if(lf_read_state == LFRead_T55xx || lf_read_state == LFRead_Em4x05)
    {
        const bool t55xx = lf_read_state == LFRead_T55xx;
        QByteArray data;
        const int displayBlock = lf_address;
        const bool parsed = success && LFMemoryProtocol::parseBlock(
                    cmd_reply, t55xx ? 'T' : 'E', 0, lf_address, data);
        if(parsed)
        {
            lf_read_result.blocks[displayBlock] = data;
        }
        else
        {
            setLfBlockError(displayBlock,
                success ? tr("Invalid LFREAD reply")
                        : lfPacketError(tr("The block could not be read")));
        }
        ++lf_address;
        readNextLfMemoryBlock();
        return true;
    }

    if(lf_read_state == LFRead_Trace)
    {
        QByteArray block1;
        QByteArray block2;
        if(success && LFMemoryProtocol::parseTrace(cmd_reply, block1, block2))
        {
            lf_read_result.blocks[9] = block1;
            lf_read_result.blocks[10] = block2;
        }
        else
        {
            const QString error = success ? tr("Invalid LFTRACE reply")
                                          : lfPacketError(tr("Traceability data could not be read"));
            setLfBlockError(9, error);
            setLfBlockError(10, error);
        }
        finishLfMemoryRead();
        return true;
    }

    if(lf_read_state == LFRead_Stream)
    {
        QVector<QByteArray> words;
        if(success && LFMemoryProtocol::parseStream(cmd_reply, words) &&
           words.size() <= lf_read_result.blocks.size())
        {
            for(int i = 0; i < words.size(); ++i)
            {
                lf_read_result.blocks[i] = words.at(i);
            }
        }
        else
        {
            const QString error = success ? tr("Invalid LFSTREAM reply")
                                          : lfPacketError(tr("The standard-read stream could not be read"));
            for(int i = 0; i < lf_read_result.blockErrors.size(); ++i)
            {
                setLfBlockError(i, error);
            }
        }
        finishLfMemoryRead();
        return true;
    }

    return false;
}

void RFIDCardCdcAtReader::finishLfMemoryRead(const QString& error)
{
    if(lf_read_state == LFRead_Idle)
    {
        return;
    }

    lf_read_result.success = lf_read_result.classification.hasMemoryChip() &&
                             lf_read_result.readBlockCount() > 0;
    if(!error.isEmpty())
    {
        lf_read_result.error = error;
    }
    else
    {
        int failedBlocks = 0;
        for(const QString& blockError : lf_read_result.blockErrors)
        {
            if(!blockError.isEmpty())
            {
                ++failedBlocks;
            }
        }
        if(failedBlocks)
        {
            lf_read_result.error = tr("%1 memory block(s) could not be read")
                    .arg(failedBlocks);
        }
    }

    const LFMemoryReadResult result = lf_read_result;
    lf_read_state = LFRead_Idle;
    lf_read_result = LFMemoryReadResult();
    lf_password.clear();
    lf_address = 0;
    emit lfMemoryReadFinished(result);
}

bool RFIDCardCdcAtReader::sendPacket(const QString& cmd, const QByteArray& param,
                                     int timeout)
{
    if(!repl_cmd.isEmpty())
    {
        qWarning()<<"Requested new AT command, currently busy - queue";
        Command qcmd;
        qcmd.cmd = cmd;
        qcmd.param = param;
        qcmd.timeout = timeout;
        commandQueue.enqueue(qcmd);
        return true;
    }

    cmd_reply.clear();
    repl_cmd = cmd;
    repl_timeout = qMax(1, timeout);
    startReplyTimer(repl_timeout);
    const QByteArray packet = QByteArrayLiteral("AT") + cmd.toLatin1() +
            param + QByteArrayLiteral("\r");
    QByteArray packetForLog = packet;
    if(cmd == QLatin1String("+LFREAD="))
    {
        const int passwordMarker = packetForLog.indexOf(QByteArrayLiteral(",P,"));
        if(passwordMarker >= 0 && passwordMarker + 11 <= packetForLog.size())
        {
            packetForLog.replace(passwordMarker + 3, 8,
                                 QByteArrayLiteral("********"));
        }
    }
    qWarning()<<"COM: >> "<<packetForLog;
    if(!transmit(packet)) {
        stopReplyTimer();
        repl_cmd.clear();
        commandQueue.clear();
        return false;
    }
    return true;
}

bool RFIDCardCdcAtReader::plusAuthKey(quint16 keyBrn)
{
    return sendPacket("+N", QString("%1").arg(keyBrn).toLatin1());
}

bool RFIDCardCdcAtReader::hal_transmit(const QByteArray &data)
{
    if(_proto == RS485) {
        return rs485 && rs485->isOpen() && rs485->send(data);
    } else {
        return handle && handle->isOpen() &&
               (handle->write(data) == data.size()) && handle->flush();
    }
}

void RFIDCardCdcAtReader::setKey(MifareClassicKeyType type, const MifareClassicKey& key)
{
    if(!isOpen())
    {
        qWarning()<<"Cannot set key - connection closed or no card";
        emit keyChanged(false);
    }
    else
    {
        bool res = (type==MifareClassicKeyA) ? sendPacket("+KA",key.toHex()) : sendPacket("+KB",key.toHex());
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

void RFIDCardCdcAtReader::setPassword(quint32 pwd)
{
    if(!isOpen())
    {
        qWarning()<<"Cannot set password - connection closed";
        emit passwordChanged(false, ul_pwd);
    }
    else if(fw_version < QVersionNumber(1, 5))
    {
        qWarning()<<"Cannot set UL password - not supported on version "<<fw_version;
        emit passwordChanged(false, ul_pwd);
    }
    else
    {
        //need to send as bytes - i.e. little endian
        QByteArray pwd_bytes(4,char(0));
        write_uint32_le(pwd, pwd_bytes.data());
        bool res = sendPacket("+KU", pwd_bytes.toHex());
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

void RFIDCardCdcAtReader::setPlusKey(MifareClassicKeyType type, const MifarePlusKey& key)
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
        bool res = sendPacket("+KX", key.toHex());
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

void RFIDCardCdcAtReader::readPack()
{
    if(!sendPacket("+KP"))
    {
        emit packReceived(false, ul_pack);
    }
}

#define INVOKE_READFAIL(result) \
    QMetaObject::invokeMethod(this, "blockRead", Q_ARG(int,blockAddress), Q_ARG(int, 0), Q_ARG(QByteArray,QByteArray()), Q_ARG(int,result))
#define INVOKE_WRITEFAIL(result) \
    QMetaObject::invokeMethod(this, "blockWritten", Q_ARG(int,blockAddress), Q_ARG(int,result))

void RFIDCardCdcAtReader::readBlock(quint16 blockAddress, int blockCount)
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
            (blockCount > (int(current_card_info.blocks) - int(blockAddress))))
    {
        qWarning()<<"Cannot read block(s) - out of bounds";
        INVOKE_READFAIL(RW_Invarg);
        return;
    }

    qWarning()<<"Requested read of blocks ["<<blockAddress<<" - "<<(blockAddress+blockCount-1)<<"]";
    block_address = blockAddress;
    block_count   = blockCount;

    if(!sendPacket(RFIDCardReaderInterface::isExtCard(current_card_info.type) ? "+M" : "+R", QByteArray::number(blockAddress)))
    {
        qWarning()<<"Cannot read block - communication error";
        block_count = 0;
        INVOKE_READFAIL(RW_Fatal);
        return;
    }
}

void RFIDCardCdcAtReader::writeBlock(quint16 blockAddress, const QByteArray& blk)
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
    else if(blockAddress>=current_card_info.blocks)
    {
        qWarning()<<"Cannot write block - out of bounds";
        INVOKE_WRITEFAIL(RW_Invarg);
        return;
    }
    else if((blk.size() <= 0) || (blk.size() != current_card_info.block_size))
    {
        qWarning()<<"Cannot write block - invalid data size "<<blk.size()
                  <<", expected "<<current_card_info.block_size;
        INVOKE_WRITEFAIL(RW_Invarg);
        return;
    }

    const quint8 *data = (const quint8*)blk.constData();
    int blockSize = blk.size();

    block_address = blockAddress;

    if(!sendPacket(RFIDCardReaderInterface::isExtCard(current_card_info.type) ? "+E" : "+W", QByteArray::number(block_address)+":"+QByteArray((const char*)data,blockSize).toHex().toUpper()))
    {
        qWarning()<<"Cannot write block - communication error";
        INVOKE_WRITEFAIL(RW_Fatal);
        return;
    }
}

bool RFIDCardCdcAtReader::selectCard(const QByteArray& uid)
{
    if(m_card_present)
    {
        clearCard();
        emit cardRemoved();
    }
    return sendPacket("+SELECT=", uid.toHex());
}

void RFIDCardCdcAtReader::removeCard()
{
    if(m_card_present)
    {
        clearCard();
        new_card_requested = true;
        emit cardRemoved();
    }
}

void RFIDCardCdcAtReader::clearCard()
{
    current_card_info.clear();
    block_count = 0;
    m_card_present = false;
}

void RFIDCardCdcAtReader::poll()
{
    if(!sendPacket(new_card_requested ? "+n" : "+i"))
    {
        if(m_card_present)
        {
            clearCard();
            emit cardRemoved();
        }
        new_card_requested = false;
        return;
    }
    qWarning()<<"Polled at "<<QDateTime::currentDateTime();
    new_card_requested = false;
}

void RFIDCardCdcAtReader::reboot()
{
    stopReplyTimer();

    if(isOpen())
    {
        sendPacket("+Q");
    }
}

bool RFIDCardCdcAtReader::dfu()
{
    if(isOpen()) {
        stopReplyTimer();
        if(sendPacket("+X")) {
            emit misc(MC_DFUResult, RFIDCardReaderInterface::hasCapability(fw_version, RFIDCardReaderInterface::DFU_MS) ? int(DExternal) : int(DBuiltin));
            return true;
        }
    }
    emit misc(MC_DFUResult, int(DFailure));
    return false;
}

bool RFIDCardCdcAtReader::selectIface(int iface)
{
    Q_UNUSED(iface);
#if 0 /* not implemented / obsolete */
    if(fw_version.microVersion() >= RFIDCardReaderInterface::REV_ThirdGen_High) {
        if(sendPacket("+IF", QByteArray(1, char(iface)))) {
            reboot();
            return true;
        }
    }
#endif
    /* Not supported or failed */
    return false;
}

void RFIDCardCdcAtReader::serialError(QSerialPort::SerialPortError error)
{
    switch(error)
    {
        case QSerialPort::DeviceNotFoundError:
        case QSerialPort::NotOpenError:
        case QSerialPort::WriteError:
        case QSerialPort::ReadError:
        case QSerialPort::ResourceError:
        case QSerialPort::TimeoutError:
        {
            qWarning()<<"Fatal serial port error"<<error;
            if(handle->isOpen())
            {
                handle->close();
            }
            break;
        }
        default:break;
    }
}

void RFIDCardCdcAtReader::aboutToClose()
{
    stopReplyTimer();
    if(lf_read_state != LFRead_Idle)
    {
        finishLfMemoryRead(tr("Connection closed during the 125 kHz memory operation"));
    }
    repl_cmd.clear();
    cmd_reply.clear();
    commandQueue.clear();
    port_data.clear();
    block_count = 0;
    emit disconnected();
}

void RFIDCardCdcAtReader::nack()
{
    qWarning()<<"Device is not responding";
    if(lf_read_state != LFRead_Idle)
    {
        finishLfMemoryRead(tr("The 125 kHz memory command timed out"));
    }
    if(handle->isOpen())
    {
        handle->close();
    }
    if(rs485->isOpen())
    {
        rs485->close();
    }
}

void RFIDCardCdcAtReader::msg_ready()
{
    port_data.append(handle->readAll());
    if(port_data.size() > MaxCdcReplyBufferSize)
    {
        qWarning()<<"CDC reply buffer limit exceeded";
        port_data.clear();
        handle->close();
        return;
    }
    qWarning()<<"Port buffer: "<<port_data;
    int start = 0, end = 0;
    while((end = port_data.indexOf("\r\n",start))>=0)
    {
        if(end!=start)
        {
            qWarning()<<"COM: << "<<port_data.mid(start,end-start);
            processDataLine(port_data.mid(start,end-start));
        }
        start = end+2;
    }
    if(start==port_data.size())
    {
        port_data.clear();
    }
    else if(start)
    {
        port_data = port_data.mid(start);
    }
}

void RFIDCardCdcAtReader::rs485_ready()
{
    while(rs485->queue())
    {
        UARTMessage msg = rs485->get();
        if(msg.first == rs485->target())
        {
            qWarning()<<"RS485: "<<msg.second;
            int start = 0, end = 0;
            while((end = msg.second.indexOf("\r\n", start)) >= 0)
            {
                if(end != start)
                {
                    qWarning()<<"RS485: << "<<msg.second.mid(start, end - start);
                    processDataLine(msg.second.mid(start, end - start));
                }
                start = end + 2;
            }
        }
        else
        {
            qWarning()<<"RS485 - from device "<<msg.first;
        }
    }
}

void RFIDCardCdcAtReader::processDataLine(const QByteArray &line)
{
    if(!line.size()) return;

    if((line=="OK")||(line=="ERROR"))
    {
        stopReplyTimer();
        //reply to `repl_cmd` assembled in `cmd_reply`
        processDataPacket(line=="OK");
    }
    else
    {
        if(cmd_reply.size() >= MaxCdcReplyLines)
        {
            qWarning()<<"CDC reply line limit exceeded";
            nack();
            return;
        }
        if(!repl_cmd.isEmpty()) {
            // Keep the command alive while a multi-line reply is making
            // progress; the timeout is an inactivity timeout.
            startReplyTimer(repl_timeout);
        }
        cmd_reply.append(line);
    }
}

void RFIDCardCdcAtReader::processDataPacket(bool success)
{
    const bool lfPacket = processLfMemoryPacket(success);
    if(lfPacket)
    {
        qWarning()<<"LF memory reply to "<<repl_cmd<<": "<<(success ? "OK" : "ERROR");
    }
    else if(repl_cmd=="I")
    {
        qWarning()<<"Got INFO command"<<cmd_reply;
        bool parsed = false;
        if(success) {
            for(const QByteArray& line : qAsConst(cmd_reply)) {
                if(RFIDCardReaderInterface::parseFirmwareVersion(QString::fromLatin1(line), dev_fw, fw_version)) {
                    parsed = true;
                    break;
                }
            }
        }

        if(parsed) {
            emit versionChanged(dev_fw);
            emit fullVersionChanged(dev_fw, fw_version);
        } else {
            dev_fw = QStringLiteral("?.??");
            fw_version = QVersionNumber();
            qWarning()<<"Firmware version not found in "<<cmd_reply;
            emit versionChanged(dev_fw);
            emit fullVersionChanged(dev_fw, fw_version);
        }
    }
    else if(repl_cmd=="+SCAN0")
    {
        qWarning()<<"Auto scan disabled: "<<(success ? "OK" : "FAIL");
        sendPacket("+K","?");
    }
    else if((repl_cmd=="+i")||(repl_cmd=="+n"))
    {
        //OK & empty reply - no card
        //+UID=HEX & OK - card detected (reselected)
        //ERROR - an error occured
        emit polled();

        if((cmd_reply.size()!=1)|| (!cmd_reply.at(0).startsWith("+UID=")) || !success)
        {
            if(m_card_present)
            {
                clearCard();
                emit cardRemoved();
            }
        }
        else
        {
            const QByteArray uidHex = cmd_reply.at(0).mid(5);
            const QByteArray uid = QByteArray::fromHex(uidHex);
            /*
             * UID + SAK:
             *     - 4+1=5 for mifare 1
             *     - 7+1=8 for mifare 2
             *     - 10+1=11 for mifare 3
             *     - 5+1=6 for EM4100
             *     - 6+1=7 for HID Prox
             */
            if(!(uidHex.size() % 2) &&
               ((uid.size()==5)||(uid.size()==8)||(uid.size()==6)||
                (uid.size()==7)||(uid.size()==11)))
            {
                if(!m_card_present || (current_card_info.uid != uid.left(uid.size()-1)))
                {
                    if(m_card_present)
                    {
                        clearCard();
                        emit cardRemoved();
                    }

                    sendPacket("+S");
                }
            }
            else if(m_card_present)
            {
                clearCard();
                emit cardRemoved();
            }
        }
    }
    else if(repl_cmd=="+S")
    {
        if(!success || cmd_reply.isEmpty())
        {
            qWarning()<<"Card info request failed";
            if(m_card_present)
            {
                clearCard();
                emit cardRemoved();
            }
        }
        else
        {
            qWarning()<<"+S reply: "<<cmd_reply.at(0);
            //+UID=HEX,BC=DEC,BS=DEC,T=DEC
            QRegExp parseCardInfo("\\+UID\\=([A-F0-9]+)\\,BC\\=(\\d+)\\,BS\\=(\\d+)\\,T\\=(\\d+)");
            if(parseCardInfo.exactMatch(QString::fromLatin1(cmd_reply.at(0))))
            {
                QStringList caps = parseCardInfo.capturedTexts();
                const QByteArray uidAndSak = QByteArray::fromHex(caps.at(1).toLatin1());
                bool blocksOk = false;
                bool blockSizeOk = false;
                bool typeOk = false;
                const uint blocks = caps.at(2).toUInt(&blocksOk);
                const uint blockSize = caps.at(3).toUInt(&blockSizeOk);
                const uint type = caps.at(4).toUInt(&typeOk);
                const bool lfCard = typeOk && isLfCard(int(type));
                const bool uidSizeOk =
                        (type == MF_EM_4100 && uidAndSak.size() == 6) ||
                        (type == MF_HID_PROX && uidAndSak.size() == 7) ||
                        (!lfCard && ((uidAndSak.size() == 5) ||
                                    (uidAndSak.size() == 8) ||
                                    (uidAndSak.size() == 11)));
                const bool geometryOk = lfCard
                        ? (blocks == 0 && blockSize == 0)
                        : ((isUnsupportedCard(int(type)) &&
                            blocks == 0 && blockSize == 0) ||
                           (blocks > 0 && blocks <= 0xFFFFu &&
                            blockSize > 0 && blockSize <= 0xFFu));
                if((caps.at(1).size() % 2) ||
                   !uidSizeOk || !blocksOk || !blockSizeOk || !typeOk ||
                   !geometryOk || (type > 0xFFu)) {
                    qWarning()<<"Invalid card info reply "<<cmd_reply.at(0);
                    const bool wasPresent = m_card_present;
                    clearCard();
                    if(wasPresent) {
                        emit cardRemoved();
                    }
                } else {
                    current_card_info.uid = uidAndSak.left(uidAndSak.size() - 1);
                    current_card_info.sak = static_cast<uint8_t>(uidAndSak.at(uidAndSak.size() - 1));
                    current_card_info.blocks = static_cast<quint16>(blocks);
                    current_card_info.block_size = static_cast<quint8>(blockSize);
                    current_card_info.type = static_cast<quint8>(type);
                    qWarning()<<"Detected card "<<mifareCardName(current_card_info.type);

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

                    m_card_present = true;
                    block_count=0;
                    emit cardPresent(current_card_info.uid, current_card_info.type);
                }

            }
            else
            {
                qWarning()<<"+S regexp not matched";
                const bool wasPresent = m_card_present;
                clearCard();
                if(wasPresent) {
                    emit cardRemoved();
                }
            }
        }
    }
    else if(repl_cmd=="+~")
    {
        if(!success)
        {
            emit error(tr("UID could not be changed: either the firmware is too old or the tag's UID is not rewritable"));
            emit uidChanged(false);
        }
        else
        {
            qWarning()<<"UID successfully changed";
            emit uidChanged(true);
        }
    }
    else if(repl_cmd=="+K")
    {
        if(success && cmd_reply.size())
        {
            QString key_repl = QString::fromLatin1(cmd_reply.at(0));
            if(key_repl.size()==14)
            {
                qWarning()<<"Current reader classic key - "<<key_repl;
                mfc_key_type = (key_repl.at(1)=='B')?MifareClassicKeyB:MifareClassicKeyA;
                mfc_key.fromString(key_repl.mid(2));
            }
            if((cmd_reply.size()>1) && (cmd_reply.at(1).startsWith("+U")) && (cmd_reply.at(1).size()==10))
            {
                qWarning()<<"Current reader ultralight password - "<<cmd_reply.at(1).mid(2);
                ul_pwd = read_uint32_le(QByteArray::fromHex(cmd_reply.at(1).mid(2)).constData());
            }
        }
    }
    else if((repl_cmd=="+KA")||(repl_cmd=="+KB"))
    {
        if(!success)
        {
            qWarning()<<"Key not set";
            emit keyChanged(false);
            emit error(tr("Key not set"));
        }
        else
        {
            emit keyChanged(true);
        }
    }
    else if(repl_cmd=="+KU")
    {
        qWarning()<<"Ultralight password set: "<<success;
        emit passwordChanged(success, ul_pwd);
    }
    else if(repl_cmd=="+KX")
    {
        qWarning()<<"AES key set: "<<success;
        emit plusKeyChanged(success);
    }
    else if(repl_cmd=="+KP")
    {
        qWarning()<<"Ultralight auth reply: "<<success;
        if(success && !cmd_reply.isEmpty() && (cmd_reply.at(0).size() == 6) &&
           cmd_reply.at(0).startsWith("+P"))
        {
            QByteArray pack_raw = QByteArray::fromHex(cmd_reply.at(0).mid(2));
            if(pack_raw.size()==2)
            {
                emit packReceived(true, read_uint16_le(pack_raw.constData()));
            }
            else
            {
                emit packReceived(false,0);
            }
        }
        else
        {
            emit packReceived(false,0);
        }
    }
    else if(repl_cmd=="+N")
    {
        emit plusAuthorized(success);
    }
    else if((repl_cmd=="+R") || (repl_cmd=="+M"))
    {
        QRegExp blockRX("\\+DATA\\s(\\d+)\\:([A-F0-9]+)");
        if(!success || cmd_reply.isEmpty() || !blockRX.exactMatch(QString::fromLatin1(cmd_reply.at(0))))
        {
            quint32 code = MFRC522_ERROR_COMM;
            foreach(const QByteArray& repl, cmd_reply)
            {
                if(repl.startsWith("+CME ERROR: "))
                {
                    code = repl.mid(12).toUInt();
                    break;
                }
            }

            QString errDescr;
            checkStatus(code,false,errDescr);
            //reset
            block_count = 0;
            emit error(tr("Block %1 not read: %2").arg(block_address).arg(errDescr));
        }
        else
        {
            int replyBlock = blockRX.cap(1).toInt();
            const QString blockHex = blockRX.cap(2);
            const QByteArray blockData = QByteArray::fromHex(blockHex.toLatin1());

            if((replyBlock != block_address) ||
               (blockHex.size() != (int(current_card_info.block_size) * 2)) ||
               (blockData.size()!=current_card_info.block_size))
            {
                qWarning()<<"Invalid reply to 'read' request - requested block "
                         <<block_address<<", got reply for block "<<replyBlock<<" of size "<<blockData.size();
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

                    if(!sendPacket(RFIDCardReaderInterface::isExtCard(current_card_info.type) ? "+M" : "+R", QByteArray::number(block_address)))
                    {
                        qWarning()<<"Canot read block - communication error";
                        block_count = 0;
                        emit blockRead(block_address, 0, QByteArray(), RW_Fatal);
                    }
                }
            }
        }
    }
    else if((repl_cmd=="+W")||(repl_cmd=="+w")||(repl_cmd=="+E"))
    {
        if(!success)
        {
            quint32 code = MFRC522_ERROR_COMM;
            foreach(const QByteArray& repl, cmd_reply)
            {
                if(repl.startsWith("+CME ERROR: "))
                {
                    code = repl.mid(12).toUInt();
                    break;
                }
            }

            QString errDescr;
            checkStatus(code,true,errDescr);
            emit error(tr("Block %1 not written: %2").arg(block_address).arg(errDescr));
        }
        else
        {
            qWarning()<<"Block "<<block_address<<" written";
            emit blockWritten(block_address, RW_OK);
        }
    }
    else if(repl_cmd=="+SELECT=")
    {
        if(!success)
        {
            qWarning()<<"The requested tag could not be selected (not in field or comm error)";
        }
        else
        {
            qWarning()<<"The requested tag has been selected";
        }
    }
    else
    {
        qWarning()<<"Reply to command "<<repl_cmd<<": "<<(success? "OK" : "ERROR");
    }

    cmd_reply.clear();
    repl_cmd.clear();
    if(!commandQueue.isEmpty())
    {
        Command cmd = commandQueue.dequeue();
        if(!sendPacket(cmd.cmd, cmd.param, cmd.timeout) &&
           lf_read_state != LFRead_Idle)
        {
            finishLfMemoryRead(tr("Could not send the 125 kHz memory command"));
        }
    }
}

void RFIDCardCdcAtReader::checkStatus(uint32_t code, bool write, QString& errorDescr)
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
        if(!write && isUltralightCard(current_card_info.type)) {
            //will report TIMEOUT on read failure due to ACL -> do not issues 'GONE' - if gone, will realy be issued by 'AT+i'
        } else {
            errorDescr = tr("tag removed");
            m_card_present = false;
            emit error(tr("Tag removed"));
            emit cardRemoved();
        }
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
            emit blockRead(block_address,0, QByteArray(), RW_Fatal);
        }
    }
}

void RFIDCardCdcAtReader::closeInt()
{
    stopReplyTimer();
    lf_read_state = LFRead_Idle;
    lf_read_result = LFMemoryReadResult();
    lf_password.clear();
    lf_address = 0;
    repl_timeout = 900;
    commandQueue.clear();
    cmd_reply.clear();
    repl_cmd.clear();
    port_data.clear();
    if(handle->isOpen())
    {
        handle->write("AT+Q\r");
        handle->flush();
        handle->close();
    }
    if(rs485->isOpen())
    {
        rs485->drop();
        qWarning()<<"Sending AT+Q";
        rs485->send("AT+Q\r");
        /*Some (ex. CH340) readers stop transmitting (or send garbage) once connection is closed*/
        qWarning()<<"waiting for reply";
        rs485->clear();
        rs485->waitMessageReady(100);
        qWarning()<<"closing connection";
        rs485->close();
    }
}
