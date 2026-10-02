#include "rfidcardreaderinterface.h"
#include "mifaresector.h"

/*For old firmware only*/
inline bool isClassic1k(quint8 sak, size_t uid_size)
{
    Q_UNUSED(uid_size);
    return (sak==0x08);
}

inline bool isClassic4k(quint8 sak, size_t uid_size)
{
    Q_UNUSED(uid_size);
    return (sak==0x18);
}

inline bool isClassicMini(quint8 sak, size_t uid_size)
{
    Q_UNUSED(uid_size);
    return (sak==0x09);
}

inline bool isUltralight(quint8 sak, size_t uid_size)
{
    Q_UNUSED(uid_size);
    return (sak==0);
}

inline bool isPlus2K(quint8 sak, size_t uid_size)
{
    Q_UNUSED(uid_size);
    return ((sak==0x08)||(sak==0x10)||(sak==0x20));
}

inline bool isPlus4K(quint8 sak, size_t uid_size)
{
    Q_UNUSED(uid_size);
    return ((sak==0x18)||(sak==0x11)||(sak==0x20));
}

inline bool isDESFire(quint8 sak, size_t uid_size)
{
    return ((uid_size==7)&&((sak==0x24)||(sak==0x20)));
}

inline MifareCards selectMifareCard(quint8 sak, size_t uid_size)
{
    if(isClassic1k(sak,uid_size))
        return MF_CLASSIC_1K;
    else if(isClassic4k(sak,uid_size))
        return MF_CLASSIC_4K;
    else if(isClassicMini(sak,uid_size))
        return MF_CLASSIC_Mini;
    else if(isUltralight(sak,uid_size))
        return MF_ULTRALIGHT;
    else if(isDESFire(sak,uid_size))
        return MF_DESFire_2K;
    else if(isPlus2K(sak,uid_size))
        return MF_PLUS_S_2K;
    else if(isPlus4K(sak,uid_size))
        return MF_PLUS_S_4K;
    return MF_UNKNOWN;
}
//////////////////***OLD FW***////////////////


/*
 * >> sensor           | << mfrc522
 * >> version          | << chip version or "nan"
 * >> scan on/off      | << ok
 * >> beep on/off      | << ok
 * >> key  A/B HH..HH  | << ok
 * >> uid              | << UID+SAK (as hex) or "no card"
 * >> read NUM         | << block data (8/32 hex digits) or error state
 * >> write NUM  DATA  | << ok/inv or error code
 * >> any other cmd    | << inv
 */

RFIDCardUSBReader::RFIDCardUSBReader(QObject *parent) : RFIDCardReaderInterface(parent),
    handle(0), state(Wait), reply_timer(this)
{
    pollTimer.setInterval(1000);
    reply_timer.setInterval(500);
    reply_timer.setSingleShot(true);
    connect(&reply_timer,SIGNAL(timeout()),this,SLOT(nack()));

    m_card_present = false;
}

RFIDCardUSBReader::~RFIDCardUSBReader()
{
    handle=0; /*does not own usb*/
}

void RFIDCardUSBReader::setManager(OdHalAbstractMessenger* handler)
{
    handle = handler;
    connect(handle,SIGNAL(dataReady()),this,SLOT(msg_ready()));
    handle->setPollTime(100);
    state = Wait;
    sendPacket("scan off");
}

QByteArray RFIDCardUSBReader::getUID()
{
    return m_card_present ? current_card_uid : QByteArray();
}

MifareCards RFIDCardUSBReader::getType()
{
    return m_card_present ? selectMifareCard(current_card_sak,current_card_uid.size()) : MF_UNKNOWN;
}

bool RFIDCardUSBReader::sendPacket(const QByteArray& data)
{
    OdHalPacket pkt;
    pkt.src = ODC_ADDRESS_LOCAL_PC;
    pkt.dst = ODC_ADDRESS_BROADCAST;
    pkt.sport = OD_PORT_PC;
    pkt.dport = OD_PORT_PLC;
    pkt.data = data;
#warning crypt
    reply_timer.stop();
    reply_timer.start();
    return (handle && handle->isOpen() && handle->send(pkt));
}

bool RFIDCardUSBReader::setKey(MifareClassicKeyType type, const MifareClassicKey& key)
{
    if(!handle || !handle->isOpen() || !m_card_present)
    {
        qWarning()<<"Cannot set key - connection closed or no card";
        return false;
    }

    mfc_key_type = type;
    mfc_key = key;
    return sendPacket(QString("key %1:%2").arg((type==MifareClassicKeyA)?"A":"B").arg(key.toString()).toLatin1());
}

#define INVOKE_READFAIL(result)  QMetaObject::invokeMethod(this,"blockRead",Q_ARG(int,blockAddress),Q_ARG(QByteArray,QByteArray()),Q_ARG(int,result))
#define INVOKE_WRITEFAIL(result) QMetaObject::invokeMethod(this,"blockWritten",Q_ARG(int,blockAddress),Q_ARG(int,result))

void RFIDCardUSBReader::readBlock(quint16 blockAddress, int blockCount)
{
    if(!m_card_present)
    {
        qWarning()<<"Cannot read block - no card";
        INVOKE_READFAIL(RW_Fatal);
        return;
    }
    else if(state != Idle)
    {
        qWarning()<<"Inappropriate state "<<state<<" - cannot read block";
        INVOKE_READFAIL(RW_Fatal);
        return;
    }

    qDebug()<<"Requested read of blocks ["<<blockAddress<<" - "<<(blockAddress+blockCount-1)<<"]";
    block_address = blockAddress;
    block_count   = blockCount;
    state         = Read;

    if(!sendPacket(QString("read %1").arg(blockAddress).toLatin1()))
    {
        qWarning()<<"Cannot read block - commnication error";
        INVOKE_READFAIL(RW_Fatal);
        return;
    }
}

void RFIDCardUSBReader::writeBlock(quint16 blockAddress, const quint8 *data, int blockSize)
{
    if(!m_card_present)
    {
        qWarning()<<"Cannot write block - no card";
        INVOKE_WRITEFAIL(RW_Fatal);
        return;
    }
    else if(state != Idle)
    {
        qWarning()<<"Inappropriate state "<<state<<" - cannot write block";
        INVOKE_WRITEFAIL(RW_Fatal);
        return;
    }

    block_address = blockAddress;
    state = Write;
    if(!sendPacket(QString("write %1:%2").arg(block_address).arg(QStringFromBin(data,blockSize)).toLatin1()))
    {
        qWarning()<<"Cannot write block - communication error";
        INVOKE_WRITEFAIL(RW_Fatal);
        return;
    }
}

void RFIDCardUSBReader::startPolling()
{
    pollTimer.stop();
    if(!handle || !handle->isOpen())
        return;
    pollTimer.start();
}

void RFIDCardUSBReader::poll()
{
    if(reply_timer.isActive() || ((state!=Wait) && (state!=Idle))) return;

    if(!sendPacket("uid"))
    {
        if(m_card_present)
        {
            emit cardRemoved();
        }
        pollTimer.stop();
        return;
    }
}

void RFIDCardUSBReader::reboot()
{
    reply_timer.stop();

    OdHalPacket pkt;
    pkt.src = ODC_ADDRESS_LOCAL_PC;
    pkt.dst = ODC_ADDRESS_BROADCAST;
    pkt.sport = OD_PORT_PC;
    pkt.dport = OD_PORT_MC;

    pkt.data.resize(3);
    *((uint8_t*)pkt.data.data()) = OD_COMMAND_CFG_CTRL;
    write_uint16_le(uint16_t(OD_CFG_REBOOT_IAP),pkt.data.data()+1);

    if(handle && handle->isOpen())
    {
        handle->send(pkt);
        handle->close();
    }
}

void RFIDCardUSBReader::nack()
{
    qWarning()<<"Device is not responding";
    if(handle)
    {
        handle->close();
    }
}

void RFIDCardUSBReader::msg_ready()
{
    OdHalPacket pkt;
    while(handle && handle->queue())
    {
        pkt = handle->pop();
        if(pkt.isEncrypted())
        {
#warning "crypt"
            qWarning()<<"Skipping cryped packet";
            continue;
        }
        processDataPacket(pkt);
    }
}

void RFIDCardUSBReader::processDataPacket(const OdHalPacket &pkt)
{
    reply_timer.stop();
    QString data = QString::fromLatin1(pkt.data);
    qDebug()<<"RFID data: "<<data;
    if(data.startsWith("uid:"))
    {
        if((data.mid(4)=="no card")&&(m_card_present))
        {
            m_card_present = false;
            state = Wait;
            emit cardRemoved();
        }
        else
        {
            QByteArray uid = QByteArray::fromHex(pkt.data.mid(4));
            if(
               ((uid.size()==5)||(uid.size()==8)||(uid.size()==11))&&
               (!m_card_present || (current_card_uid!=uid.left(uid.size()-1)))
               )
            {
                if(m_card_present)
                {
                    m_card_present = false;
                    emit cardRemoved();
                }

                current_card_sak = static_cast<uint8_t>(uid.at(uid.size()-1));
                current_card_uid = uid.left(uid.size()-1);

                MifareCards type = selectMifareCard(current_card_sak,current_card_uid.size());
                qDebug()<<"Detected card "<<mifareCardName(type)<<" from sak "<<int(current_card_sak);

                if(type<MF_DESFire_EV2)
                {
                    m_card_present = true;
                    state = Idle;
                    emit cardPresent();
                }
                else
                {
                    qWarning()<<"Unknown/unsupported card "<<mifareCardName(type);
                    state = Wait;
                }
            }
        }
    }
    else if(data.startsWith("key"))
    {
        if(data.endsWith("inv"))
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
    else if(data.startsWith("read "))
    {
        if(state==Read)
        {
            int colon = data.indexOf(':');
            if((colon<0)||(colon>=(data.size()-1)))
            {
                qWarning()<<"Invalid reply to 'read' request - not enough data";
                emit blockRead(block_address,QByteArray(),RW_Invarg);
                emit error(tr("Block %1 not read - invalid reply").arg(block_address));
                state = Idle;
                return;
            }

            bool ok;
            int replyBlock = data.mid(5,colon-5).toInt(&ok);
            if(!ok || (replyBlock != block_address))
            {
                qWarning()<<"Invalid reply to 'read' request - requested block "
                          <<block_address<<", got reply for block "<<data.mid(5,colon-5);
                emit blockRead(block_address,QByteArray(),RW_Invarg);
                emit error(tr("Block %1 not read - invalid reply").arg(block_address));
                state = Idle;
                return;
            }

            data = data.mid(colon+1);

            if(data.at(0)=='-')
            {
                //error
                int semicolon = data.indexOf(';');
                uint32_t code = ((semicolon==-1) ? data.mid(1) : data.mid(1,semicolon-1)).toUInt(&ok);
                if(!ok)
                {
                    qWarning()<<"Invalid reply to 'read' request - "
                              <<((semicolon==-1) ? data.mid(1) : data.mid(1,semicolon-1))
                              <<" is not an integer";
                    emit blockRead(block_address,QByteArray(),RW_Invarg);
                    emit error(tr("Block %1 not read - invalid reply").arg(block_address));
                }
                else
                {
                    checkStatus(code,false);
                    emit error(tr("Block %1 not read: %2").arg(block_address)
                               .arg((semicolon>0)?data.mid(semicolon+1):tr("unknown error")));
                }
            }
            else
            {
                QByteArray block_data = QByteArray::fromHex(data.toLatin1());
                if(block_data.size()!=16)
                {
                    qWarning()<<"Invalid reply to 'read' request - expect 16 hex digits (32bytes), got"
                             <<QString::fromLatin1(pkt.data.mid(5));
                    emit blockRead(block_address,QByteArray(),RW_Invarg);
                    emit error(tr("Block %1 not read - invalid reply").arg(block_address));
                }
                else
                {
                    if((getType()>=MF_ULTRALIGHT)&&(getType()<=MF_ULTRALIGHT_EV1_164))
                    {
                        //4byte block
                        block_count = (block_count>4) ? (block_count-4) : 0;
                        emit blockRead(block_address,block_data,RW_OK);
                        block_address+=4;
                    }
                    else
                    {
                        --block_count;
                        emit blockRead(block_address++,block_data,RW_OK);
                    }

                    if(block_count)
                    {
                        if(sendPacket(QString("read %1").arg(block_address).toLatin1()))
                        {
                            return;
                        }
                        qWarning()<<"Canot read block - communication error";
                        emit blockRead(block_address,QByteArray(),RW_Fatal);
                    }

                }
            }
            state = Idle;
        }
        else
        {
            qWarning()<<"'read' reply ("<<data<<") unexpected - state "<<state;
        }
    }
    else if(data.startsWith("write "))
    {
        if(state==Write)
        {
            state = Idle;

            int colon = data.indexOf(':');
            if((colon<0)||(colon>=(data.size()-1)))
            {
                qWarning()<<"Invalid reply to 'write' request - not enough data";
                emit error(tr("Block %1 not written - invalid reply").arg(block_address));
                emit blockWritten(block_address,RW_Invarg);
                return;
            }

            bool ok;
            int replyBlock = data.mid(6,colon-6).toInt(&ok);
            if(!ok || (replyBlock != block_address))
            {
                qWarning()<<"Invalid reply to 'write' request - requested write block "
                          <<block_address<<", got reply for "<<data.mid(6,colon-6);
                emit error(tr("Block %1 not written - invalid reply").arg(block_address));
                emit blockWritten(block_address,RW_Invarg);
                return;
            }

            data = data.mid(colon+1);

            if(data.at(0)=='-')
            {
                //error
                int semicolon = data.indexOf(';');
                bool ok;
                uint32_t code = data.mid(1,(semicolon==-1)?-1:(semicolon-1)).toUInt(&ok);
                if(!ok)
                {
                    qWarning()<<"Invalid reply to 'write' request - "
                             <<data.mid(1,(semicolon==-1)?-1:(semicolon-1))<<" is not an integer";
                    emit error(tr("Block %1 not written - invalid reply").arg(block_address));
                    emit blockWritten(block_address,RW_Invarg);
                }
                else
                {
                    checkStatus(code,true);
                    emit error(tr("Block %1 not written: %2").arg(block_address)
                               .arg((semicolon>0)?data.mid(semicolon+1):tr("unknown error")));
                }
            }
            else
            {
                qWarning()<<"Block "<<block_address<<" written";
                emit blockWritten(block_address, RW_OK);
            }
        }
        else
        {
            qWarning()<<"'write' reply ("<<data<<") unexpected - state "<<state;
        }
    }
}

void RFIDCardUSBReader::checkStatus(uint32_t code, bool write)
{
    qDebug()<<"MFRC522 status "<<QString("0x%1 (%2)").arg(code,8,16,QLatin1Char('0')).arg(rfidUsbError(code));
    if(code & MFRC522_ERROR_AUTH)
    {
        qWarning()<<"Key authentication failed!";
        emit error(tr("Key authentication failed"));
        if(write)
        {
            emit blockWritten(block_address,RW_Denied);
        }
        else
        {
            emit blockRead(block_address,QByteArray(),RW_Denied);
        }
        return;
    }

    if(code & (MFRC522_ERROR_TEAR|MFRC522_ERROR_TIMEOUT|MFRC522_ERROR_NACK))
    {
        if(!(code & MFRC522_ERROR_AUTH))
        {
            if(write)
            {
                emit blockWritten(block_address,RW_Fatal);
            }
            else
            {
                emit blockRead(block_address,QByteArray(),RW_Fatal);
            }
        }

        m_card_present = false;
        emit error(tr("Card removed"));
        emit cardRemoved();
    }
    else
    {
        if(write)
        {
            emit blockWritten(block_address,RW_Retry);
        }
        else
        {
            emit blockRead(block_address,QByteArray(),RW_Retry);
        }
    }
}
