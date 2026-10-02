#include "rfidcardreaderoperator.h"
#include "rfidcardreaderinterface.h"

#include "rfidcardsimulatorreader.h"
#include "rfidcardcdcatreader.h"
#include "rfidcardhidreader.h"

#include <QCoreApplication>
#include <QDebug>

RFIDCardReaderOperator::RFIDCardReaderOperator(QObject *parent) : QObject(parent),
    poller(this), workerThread(this), _reader(nullptr),
    _reader_type(RFIDCardReaderInterface::RFIDReader_Invalid),
    _reader_uid(QByteArray()), _reader_tcard(MF_UNKNOWN),
    _lf_memory_supported(false),
    card_state(CS_NoCard), reader_busy(false)
{
    qRegisterMetaType<LFMemoryReadResult>("LFMemoryReadResult");
    poller.setInterval(1000);
    QObject::connect(&poller, &QTimer::timeout, this, &RFIDCardReaderOperator::poller_timeout);
    QObject::connect(&workerThread, &QThread::finished, this, &RFIDCardReaderOperator::worker_finished);
}

RFIDCardReaderOperator::~RFIDCardReaderOperator()
{
    poller.stop();
    workerThread.disconnect(this);
    if(workerThread.isRunning())
    {
        workerThread.quit();
        workerThread.wait(10000);
        if(!workerThread.isFinished())
        {
            workerThread.terminate();
            workerThread.wait();
        }
    }
}

bool RFIDCardReaderOperator::isValid() const
{
    return _reader && workerThread.isRunning();
}

QString RFIDCardReaderOperator::uidString() const
{
    return QString::fromLatin1(_reader_uid.toHex().toUpper());
}

void RFIDCardReaderOperator::openCard(const QString& id)
{
    if(!isValid())
    {
        qWarning()<<"No reader - cannot open "<<id;
        emit error(Warning, tr("Open Tag"), tr("No active reader"));
        return;
    }

    if(_reader_type == RFIDCardReaderInterface::RFIDReader_Simulator)
    {
        QMetaObject::invokeMethod(dynamic_cast<RFIDCardSimulatorReader*>(_reader), "open", Qt::QueuedConnection, Q_ARG(QString, id));
    }
    else if(_reader_type == RFIDCardReaderInterface::RFIDReader_Hardware)
    {
        if(id.isEmpty())
        {
            QMetaObject::invokeMethod(_reader, &RFIDCardReaderInterface::removeCard);
        }
        else
        {
            QMetaObject::invokeMethod(dynamic_cast<RFIDCardHardwareReader*>(_reader), "selectCard", Qt::QueuedConnection, Q_ARG(QByteArray, QByteArray::fromHex(id.toLatin1())));
        }
    }
    else
    {
        emit error(Warning, tr("Open Tag"), tr("Reader does not support this operation"));
    }
}

void RFIDCardReaderOperator::newCard(MifareCards type, const QString& output)
{
    if(!isValid() || (_reader_type != RFIDCardReaderInterface::RFIDReader_Simulator))
    {
        emit error(Warning, tr("Create Tag"), tr("Reader does not support this operation"));
        return;
    }

    QMetaObject::invokeMethod(dynamic_cast<RFIDCardSimulatorReader*>(_reader), "create", Qt::QueuedConnection, Q_ARG(int, type), Q_ARG(QString, output));
}

void RFIDCardReaderOperator::setReader(int type, quint32 subtype, const QVariant& param)
{
    close();

    subtype &= 0xFF;

    switch(type)
    {
        case RFIDCardReaderInterface::RFIDReader_Simulator:
            _reader = new RFIDCardSimulatorReader();
            break;
        case RFIDCardReaderInterface::RFIDReader_Hardware:
        {
            if(subtype == RFIDCARDHIDREADER_ID)
            {
                _reader = new RFIDCardHIDReader();
            }
            else if(subtype == RFIDCARDCDCATREADER_ID)
            {
                _reader = new RFIDCardCdcAtReader();
                dynamic_cast<RFIDCardCdcAtReader*>(_reader)->setProtocol(RFIDCardCdcAtReader::CDC);
            }
            else if(subtype == RFIDCARDUARTREADER_ID)
            {
                _reader = new RFIDCardCdcAtReader();
                dynamic_cast<RFIDCardCdcAtReader*>(_reader)->setProtocol(RFIDCardCdcAtReader::UART);
            }
            else if(subtype == RFIDCARDRS485READER_ID)
            {
                _reader = new RFIDCardCdcAtReader();
                dynamic_cast<RFIDCardCdcAtReader*>(_reader)->setProtocol(RFIDCardCdcAtReader::RS485);
            }
            else
            {
                emit error(Warning, tr("Connect"), tr("Unsupported reader class"));
                return;
            }
            break;
        }
        default:
            emit error(Warning, tr("Connect"), tr("Unsupported reader class"));
            return;
    }

    if(_reader)
    {
        _reader_type = _reader->type();
        _lf_memory_supported = _reader->supportsLfMemory();
        _reader->moveToThread(&workerThread);
        connect(&workerThread, SIGNAL(finished()), _reader, SLOT(deleteLater()));
        if(_reader_type == RFIDCardReaderInterface::RFIDReader_Hardware)
        {
            connect(dynamic_cast<RFIDCardHardwareReader*>(_reader), &RFIDCardHardwareReader::disconnected, &workerThread, &QThread::quit);
            QMetaObject::invokeMethod(dynamic_cast<RFIDCardHardwareReader*>(_reader), "setDevice", Qt::QueuedConnection, Q_ARG(QString, param.toString()));
        }
        else if(_reader_type == RFIDCardReaderInterface::RFIDReader_Simulator)
        {
            connect(dynamic_cast<RFIDCardSimulatorReader*>(_reader), &RFIDCardSimulatorReader::cardOpened, this, &RFIDCardReaderOperator::reader_cardOpened, Qt::QueuedConnection);
        }

        connect(_reader, &RFIDCardReaderInterface::fullVersionChanged, this, &RFIDCardReaderOperator::reader_versionChanged, Qt::QueuedConnection);
        connect(_reader, &RFIDCardReaderInterface::passwordChanged, this, &RFIDCardReaderOperator::reader_passwordChanged, Qt::QueuedConnection);
        connect(_reader, &RFIDCardReaderInterface::keyChanged, this, &RFIDCardReaderOperator::reader_keyChanged, Qt::QueuedConnection);
        connect(_reader, &RFIDCardReaderInterface::plusKeyChanged, this, &RFIDCardReaderOperator::reader_plusKeyChanged, Qt::QueuedConnection);
        connect(_reader, &RFIDCardReaderInterface::plusAuthorized, this, &RFIDCardReaderOperator::reader_plusAuthorized, Qt::QueuedConnection);
        connect(_reader, &RFIDCardReaderInterface::polled, this, &RFIDCardReaderOperator::reader_polled, Qt::QueuedConnection);
        connect(_reader, &RFIDCardReaderInterface::cardPresent, this, &RFIDCardReaderOperator::reader_cardDetected, Qt::QueuedConnection);
        connect(_reader, &RFIDCardReaderInterface::cardRemoved, this, &RFIDCardReaderOperator::reader_cardRemoved, Qt::QueuedConnection);
        connect(_reader, &RFIDCardReaderInterface::uidChanged, this, &RFIDCardReaderOperator::reader_cardUidChanged, Qt::QueuedConnection);
        connect(_reader, &RFIDCardReaderInterface::packReceived, this, &RFIDCardReaderOperator::reader_cardPackReceived, Qt::QueuedConnection);
        connect(_reader, &RFIDCardReaderInterface::blockRead, this, &RFIDCardReaderOperator::reader_blockRead, Qt::QueuedConnection);
        connect(_reader, &RFIDCardReaderInterface::blockWritten, this, &RFIDCardReaderOperator::reader_blockWritten, Qt::QueuedConnection);
        connect(_reader, &RFIDCardReaderInterface::lfMemoryReadFinished,
                this, &RFIDCardReaderOperator::reader_lfMemoryReadFinished,
                Qt::QueuedConnection);
        connect(_reader, &RFIDCardReaderInterface::error, this, [this](const QString& message) {
            emit error(Info, QString(), message);
        }, Qt::QueuedConnection);
        connect(_reader, &RFIDCardReaderInterface::misc, this, &RFIDCardReaderOperator::misc, Qt::QueuedConnection);
        //QMetaObject::invokeMethod(_reader, &RFIDCardReaderInterface::startPolling, Qt::QueuedConnection);
        workerThread.start();
        poller.start();
        emit readerValid(true);
    }
}

void RFIDCardReaderOperator::close()
{
    _queue.clear();
    reader_busy = false;
    _reader_type = RFIDCardReaderInterface::RFIDReader_Invalid;
    _reader_uid.clear();
    _reader_tcard = MF_UNKNOWN;
    _lf_memory_supported = false;
    _reader = nullptr; /*will be deleted by the worker*/
    poller.stop();
    if(card_state != CS_NoCard)
    {
        card_state = CS_NoCard;
        emit cardStateChanged(card_state);
    }

    if(workerThread.isRunning())
    {
        workerThread.quit();
        workerThread.wait(5000);
        if(!workerThread.isFinished())
        {
            workerThread.terminate();
            workerThread.wait();
        }
        emit readerValid(false);
    }
}

bool RFIDCardReaderOperator::dfu()
{
    if(!isValid() || (_reader_type != RFIDCardReaderInterface::RFIDReader_Hardware))
    {
        emit error(Warning, tr("Firmware Update"), tr("No active hardware RFID reader"));
        return false;
    }

    QMetaObject::invokeMethod(dynamic_cast<RFIDCardHardwareReader*>(_reader), &RFIDCardHardwareReader::dfu, Qt::QueuedConnection);
    return true;
}

bool RFIDCardReaderOperator::switchInterface()
{
    if(!isValid() || (_reader_type != RFIDCardReaderInterface::RFIDReader_Hardware))
    {
        emit error(Warning, tr("Firmware Update"), tr("No active hardware RFID reader"));
        return false;
    }

    switch(_reader->subtype()) {
        case RFIDCARDHIDREADER_ID: {
            QMetaObject::invokeMethod(dynamic_cast<RFIDCardHardwareReader*>(_reader), "selectIface", Qt::QueuedConnection, Q_ARG(int, RFIDCardHardwareReader::iCDC));
            break;
        }
        default: {
            QMetaObject::invokeMethod(dynamic_cast<RFIDCardHardwareReader*>(_reader), "selectIface", Qt::QueuedConnection, Q_ARG(int, RFIDCardHardwareReader::iHID));
            break;
        }
    }
    return true;
}

void RFIDCardReaderOperator::setPassword(quint32 pwd)
{
    _queue.append(PendingOP(SetPassword, QVariantList()<<pwd));
    queuePop();
}

void RFIDCardReaderOperator::setUID(const QByteArray& uid)
{
    _queue.append(PendingOP(ChangeUID, QVariantList()<<uid));
    queuePop();
}

void RFIDCardReaderOperator::writeUID(const QByteArray& uid, const QByteArray& key, const QByteArray& pwd/*4 bytes, BE*/, int coding, int speed)
{
    _queue.append(PendingOP(ChangeUID, QVariantList()<<uid<<key<<pwd<<coding<<speed));
    queuePop();
}

void RFIDCardReaderOperator::setKey(int type, const MifareClassicKey& key)
{
    _queue.append(PendingOP(SetKey, QVariantList()<<type<<QVariant::fromValue(key)));
    queuePop();
}

void RFIDCardReaderOperator::setPlusKey(int type, const MifarePlusKey& key)
{
    _queue.append(PendingOP(SetAESKey, QVariantList()<<type<<QVariant::fromValue(key)));
    queuePop();
}

void RFIDCardReaderOperator::removeCard()
{
    _queue.append(PendingOP(RemoveCard));
    queuePop();
}

void RFIDCardReaderOperator::rectifyCard()
{
    if(card_state == CS_ReadFailed)
    {
        //emit cardStateChanged(CS_ReadOK);
        card_state = CS_Idle;
        emit cardStateChanged(card_state);
    }
}

void RFIDCardReaderOperator::readPack()
{
    _queue.append(PendingOP(RequestPack));
    queuePop();
}

void RFIDCardReaderOperator::plusAuth(int block)
{
    _queue.append(PendingOP(PlusAuth, QVariantList()<<block));
    queuePop();
}

void RFIDCardReaderOperator::read(int blk, int count)
{
    _queue.append(PendingOP(ReadBlock, QVariantList()<<blk<<count));
    queuePop();
}

void RFIDCardReaderOperator::write(int blk, const QByteArray& data)
{
    if(blk>=0)
    {
        _queue.append(PendingOP(WriteBlock, QVariantList()<<blk<<data));
    }
    else
    {
        _queue.append(PendingOP(WriteFinalize));
    }
    queuePop();
}

void RFIDCardReaderOperator::readLfMemory(bool fullScan, const QByteArray& password)
{
    if(!supportsLfMemory())
    {
        LFMemoryReadResult result;
        result.error = tr("The connected interface does not support 125 kHz memory reading");
        emit lfMemoryReadFinished(result);
        return;
    }
    if(!password.isEmpty() && password.size() != 4)
    {
        LFMemoryReadResult result;
        result.error = tr("T55xx password must contain exactly 8 HEX characters");
        emit lfMemoryReadFinished(result);
        return;
    }

    _queue.append(PendingOP(ReadLFMemory, QVariantList()
                            << fullScan << password));
    queuePop();
}

void RFIDCardReaderOperator::worker_finished()
{
    // A queued finished() signal from the previous reader can arrive after the
    // reusable worker thread has already been started for a new reader.
    if(workerThread.isRunning()) {
        return;
    }

    queueAbort();
    if(card_state != CS_NoCard)
    {
        card_state = CS_NoCard;
        emit cardStateChanged(card_state);
    }
    _reader_type = RFIDCardReaderInterface::RFIDReader_Invalid;
    _reader_uid.clear();
    _reader_tcard = MF_UNKNOWN;
    _lf_memory_supported = false;
    _reader = nullptr; /*will be deleted by the worker in a slot*/
    emit readerValid(false);
}

void RFIDCardReaderOperator::poller_timeout()
{
    if(!_reader) {
        poller.stop();
        return;
    }
    if(reader_busy) {
        return;
    }

    if((card_state == CS_NoCard) || (card_state == CS_Idle) || (card_state == CS_ReadFailed)) {
        _queue.append(PendingOP(Poll));
        queuePop();
    }
}

void RFIDCardReaderOperator::reader_versionChanged(const QString& v, const QVersionNumber &vn)
{
    qWarning()<<"Detected reader version "<<v<<" ("<<vn<<")";
    emit readerVersion(v, vn);
}

void RFIDCardReaderOperator::reader_passwordChanged(bool success, quint32 pwd)
{
    qWarning()<<"Reader reports password changed";
    reader_busy = false;

    if(!success)
    {
        emit error(Critical, tr("Ultralight Authentication"),
                   tr("Reader communication error: the ultralight authentication credentials could not be set"));

        if(card_state!=CS_NoCard)
        {
            card_state = (card_state == CS_Writing) ? CS_WriteFailed : CS_ReadFailed;
            emit cardStateChanged(card_state);
        }
        _queue.clear();
    }
    emit passwordChanged(success, pwd);
    queuePop();
}

void RFIDCardReaderOperator::reader_keyChanged(bool success)
{
    qWarning()<<"Reader classic key change success: "<<success;
    reader_busy = false;
    if(!success)
    {
        emit error(Critical, tr("Mifare Authentication"),
                   tr("Reader communication error: Mifare Classic key clould not be set"));
        if(card_state != CS_NoCard)
        {
            card_state = (card_state == CS_Writing) ? CS_WriteFailed : CS_ReadFailed;
            emit cardStateChanged(card_state);
        }
        _queue.clear();
    }
    queuePop();
}

void RFIDCardReaderOperator::reader_plusKeyChanged(bool success)
{
    qWarning()<<"Reader plus key change success: "<<success;
    reader_busy = false;
    if(!success)
    {
        emit error(Critical, tr("Mifare Authentication"),
                   tr("Reader communication error: Mifare Plus key could not be set"));
        if(card_state != CS_NoCard)
        {
            card_state = (card_state == CS_Writing) ? CS_WriteFailed : CS_ReadFailed;
            emit cardStateChanged(card_state);
        }
        _queue.clear();
    }
    queuePop();
}

void RFIDCardReaderOperator::reader_plusAuthorized(bool success)
{
    qWarning()<<"Reader plus authorized: "<<success;
    reader_busy = false;
    if(!success)
    {
        emit error(Critical, tr("Mifare Authentication"),
                   tr("Reader communication error: Mifare Plus authorization failure"));
        if(card_state != CS_NoCard)
        {
            card_state = (card_state == CS_Writing) ? CS_WriteFailed : CS_ReadFailed;
            emit cardStateChanged(card_state);
        }
        _queue.clear();
    }
    queuePop();
}

void RFIDCardReaderOperator::reader_polled()
{
    reader_busy = false;
    queuePop();
}

void RFIDCardReaderOperator::reader_cardOpened(bool success, const QString& error)
{
    qWarning()<<"Reader card opened: "<<success<<", "<<error;
    emit cardOpened(success, error);
}

void RFIDCardReaderOperator::reader_cardDetected(const QByteArray& uid, int type)
{
    _reader_tcard = ((type>=0)&&(type<=MF_LAST_KNOWN_CARD)) ? static_cast<MifareCards>(type) : MF_UNKNOWN;
    _reader_uid = uid;
    if((card_state != CS_Idle) && (_reader_tcard != MF_UNKNOWN))
    {
        card_state = CS_Idle;
        emit cardStateChanged(card_state);
    }
    emit cardDetected(_reader_uid, _reader_tcard);
    if(isLfCard(type)) {
        emit cardStateChanged(CS_ReadOK);
        emit cardStateChanged(CS_Idle);
    }
    queuePop();
}

void RFIDCardReaderOperator::reader_cardRemoved()
{
    queueAbort();
    _reader_tcard = MF_UNKNOWN;
    _reader_uid.clear();

    if(card_state != CS_NoCard)
    {
        qWarning("Op: card removed");
        card_state = CS_NoCard;
        emit cardStateChanged(card_state);
    }
}

void RFIDCardReaderOperator::reader_cardUidChanged(bool success)
{
    reader_busy = false;
    if(success)
    {
        //card is not usable at this moment
        queueAbort();
        emit cardUidChanged();
    }
    else
    {
        queuePop();
    }
}

void RFIDCardReaderOperator::reader_cardPackReceived(bool success, quint16 pack)
{
    reader_busy = false;
    if(!success)
    {
        queueAbort();
        card_state = (card_state==CS_Writing) ? CS_WriteFailed : CS_ReadFailed;
        emit cardStateChanged(card_state);
    }
    queuePop();
    if((card_state != CS_Writing) && (card_state != CS_WriteFailed))
    {
        emit packReceived(success, pack);
    }
}

void RFIDCardReaderOperator::reader_blockRead(int number, int left, QByteArray data, int result)
{
    reader_busy = false;
    if(result != RFIDCardReaderInterface::RW_OK)
    {
        queueAbort();
        if(card_state != CS_ReadFailed)
        {
            card_state = CS_ReadFailed;
            emit cardStateChanged(card_state);
        }
    }
    else
    {
        queuePop();
    }

    emit blockRead(number, data, result);
    if((result == RFIDCardReaderInterface::RW_OK) && (left == 0))
    {
        emit cardStateChanged(CS_ReadOK);
        card_state = CS_Idle;
        emit cardStateChanged(card_state);
    }
}

void RFIDCardReaderOperator::reader_blockWritten(int address, int code)
{
    reader_busy = false;
    if(code!=RFIDCardReaderInterface::RW_OK)
    {
        queueAbort();
        if(card_state != CS_WriteFailed)
        {
            card_state = CS_WriteFailed;
            emit cardStateChanged(card_state);
        }
    }
    else
    {
        queuePop();
    }
    emit blockWritten(address, code);
}

void RFIDCardReaderOperator::reader_lfMemoryReadFinished(
        const LFMemoryReadResult& result)
{
    reader_busy = false;
    emit lfMemoryReadFinished(result);
    queuePop();
}

void RFIDCardReaderOperator::queuePop()
{
    if(_queue.isEmpty())
    {
        return;
    }

    if(reader_busy)
    {
        qWarning()<<"Queue pop delayed - reader busy: queue size "<<_queue.size();
        return;
    }
    reader_busy = true;
    qWarning()<<"Reader now busy "<<reader_busy<<" - schedule next queue cmd execution";
    QMetaObject::invokeMethod(this, &RFIDCardReaderOperator::do_queuePop, Qt::QueuedConnection);
}

void RFIDCardReaderOperator::do_queuePop()
{
    if(!isValid())
    {
        queueAbort();
        return;
    }

    if(!_queue.isEmpty())
    {
        PendingOP pop = _queue.dequeue();
        qWarning()<<"Operator: executing command "<<pop.cmd;
        switch(pop.cmd)
        {
            case NOP:
            {
                //hm.. what for?
                reader_busy = false;
                break;
            }
            case Poll:
            {
                QMetaObject::invokeMethod(_reader, "poll", Qt::QueuedConnection);
                break;
            }
            case ReadBlock:
            {
                if(pop.params.size()==2)
                {
                    if(card_state != CS_ReadPend)
                    {
                        card_state = CS_ReadPend;
                        emit cardStateChanged(card_state);
                    }
                    QMetaObject::invokeMethod(_reader, "readBlock", Qt::QueuedConnection,
                                              Q_ARG(quint16, quint16(pop.params.at(0).toUInt())),
                                              Q_ARG(int, pop.params.at(1).toInt()));
                    //_reader->readBlock(quint16(pop.params.at(0).toInt()), pop.params.at(1).toInt());
                }
                else
                {
                    queueAbort();
                    emit error(Warning, tr("Read Tag"), tr("Cannot read the tag data - invalid parameters passed"));
                }
                break;
            }
            case WriteBlock:
            {
                if((pop.params.size()==2)&&(pop.params.at(1).canConvert(QVariant::ByteArray)))
                {
                    if(card_state != CS_Writing)
                    {
                        card_state = CS_Writing;
                        emit cardStateChanged(card_state);
                    }
                    QByteArray data = pop.params.at(1).toByteArray();
                    QMetaObject::invokeMethod(_reader, "writeBlock", Qt::QueuedConnection,
                                              Q_ARG(quint16, quint16(pop.params.at(0).toUInt())),
                                              Q_ARG(QByteArray, data));
                    //_reader->writeBlock(quint16(pop.params.at(0).toInt()), data);
                }
                else
                {
                    queueAbort();
                    emit error(Warning, tr("Write Tag"), tr("Cannot write the data - invalid parameters passed"));
                }
                break;
            }
            case WriteFinalize:
            {
                if(card_state == CS_Writing) {
                    emit cardStateChanged(CS_Written);
                    card_state = CS_Idle;
                    emit cardStateChanged(card_state);
                }
                // Finalizing an empty write is valid and must not leave the
                // operator permanently busy.
                reader_busy = false;
                break;
            }
            case SetKey:
            {
                if((pop.params.size()==2)&&(pop.params.at(1).canConvert<MifareClassicKey>()))
                {
                    int kt = pop.params.at(0).toInt();
                    if((kt!=MifareClassicKeyA)&&(kt!=MifareClassicKeyB))
                    {
                        queueAbort();
                        emit error(Warning, tr("Classic Authentication"),
                                   tr("Cannot set key - wrong key type passed"));
                        break;
                    }

                    QMetaObject::invokeMethod(_reader, "setKey", Qt::QueuedConnection,
                                              Q_ARG(MifareClassicKeyType, static_cast<MifareClassicKeyType>(kt)), Q_ARG(MifareClassicKey, pop.params.at(1).value<MifareClassicKey>()));
                }
                else
                {
                    queueAbort();
                    emit error(Warning, tr("Classic Authentication"),
                               tr("Cannot set key - invalid parameters passed"));
                }
                break;
            }
            case SetAESKey:
            {
                if((pop.params.size() == 2) && (pop.params.at(1).canConvert<MifarePlusKey>()))
                {
                    int kt = pop.params.at(0).toInt();
                    if((kt!=MifareClassicKeyA)&&(kt!=MifareClassicKeyB))
                    {
                        queueAbort();
                        emit error(Warning, tr("Plus Authentication"),
                                   tr("Cannot set key - wrong key type passed"));
                        break;
                    }

                    QMetaObject::invokeMethod(_reader, "setPlusKey", Qt::QueuedConnection,
                                              Q_ARG(MifareClassicKeyType, static_cast<MifareClassicKeyType>(kt)), Q_ARG(MifarePlusKey, pop.params.at(1).value<MifarePlusKey>()));
                }
                else
                {
                    queueAbort();
                    emit error(Warning, tr("Plus Authentication"),
                               tr("Cannot set key - invalid parameters passed"));
                }
                break;
            }
            case SetPassword:
            {
                if(pop.params.size()==1)
                {
#if 0
                    if(!_reader->setPassword(pop.params.at(0).toUInt()))
                    {
                        queueAbort();
                        emit error(Warning, tr("Ultralight Authentication"),
                                   tr("Ultralight password could not be set"));
                    }
#else
                    QMetaObject::invokeMethod(_reader, "setPassword", Qt::QueuedConnection, Q_ARG(quint32, pop.params.at(0).toUInt()));
#endif
                }
                else
                {
                    qWarning()<<"Invalid queued 'SetPassword' - params do not match";
                    queueAbort();
                    emit error(Warning, tr("Ultralight Authentication"),
                               tr("Ultralight password could not be set - invalid password passed"));
                }
                break;
            }
            case RequestPack:
            {
                QMetaObject::invokeMethod(_reader, "readPack", Qt::QueuedConnection);
                break;
            }
            case PlusAuth:
            {
                if(pop.params.size()==1)
                {
                    QMetaObject::invokeMethod(_reader, "plusAuth", Qt::QueuedConnection, Q_ARG(quint16, quint16(pop.params.at(0).toInt())));
                }
                else
                {
                    queueAbort();
                    emit error(Warning, tr("Plus Authentication"),
                               tr("Cannot authorize - invalid number passed"));
                }
                break;
            }
            case ChangeUID:
            {
                if((pop.params.size()==1)&&(pop.params.at(0).canConvert(QVariant::ByteArray)))
                {
                    qWarning()<<"Requested ChangeUID (for Classic)";
                    QMetaObject::invokeMethod(_reader, "setUID", Qt::QueuedConnection, Q_ARG(QByteArray, pop.params.at(0).value<QByteArray>()));
                }
                else if((pop.params.size()==5) &&
                        pop.params.at(0).canConvert(QVariant::ByteArray) &&
                        pop.params.at(1).canConvert(QVariant::ByteArray) &&
                        pop.params.at(2).canConvert(QVariant::ByteArray))
                {
                    qWarning()<<"Requested ChangeUID (clone for EM)";
                    QMetaObject::invokeMethod(_reader, "writeEM", Qt::QueuedConnection,
                                              Q_ARG(QByteArray, pop.params.at(0).value<QByteArray>()),
                                              Q_ARG(QByteArray, pop.params.at(1).value<QByteArray>()),
                                              Q_ARG(QByteArray, pop.params.at(2).value<QByteArray>()),
                                              Q_ARG(int, pop.params.at(3).toInt()), Q_ARG(int, pop.params.at(4).toInt()));
                }
                else
                {
                    qWarning()<<"Invalid queued 'Change UID' - params do not match or not a bytearray";
                    queueAbort();
                    emit error(Warning, tr("Change UID"),
                               tr("Cannot send UID change/write request - wrong parameters passed"));
                }
                break;
            }
            case RemoveCard:
            {
                QMetaObject::invokeMethod(_reader, &RFIDCardReaderInterface::removeCard, Qt::QueuedConnection);
                reader_busy = false;
                break;
            }
            case SelectCard:
            {
                if(_reader_type != RFIDCardReaderInterface::RFIDReader_Hardware)
                {
                    queueAbort();
                    emit error(Warning, tr("Select TAG"), tr("Cannot select tag - the simulator does not support that"));
                }
                else if((pop.params.size()==1)&&(pop.params.at(0).canConvert(QVariant::ByteArray)))
                {
#if 0
                    if(!dynamic_cast<RFIDCardHardwareReader*>(_reader)->selectCard(pop.params.at(0).toByteArray()))
                    {
                        queueAbort();
                        emit error(Warning, tr("Select TAG"), tr("Select TAG query failed"));
                    }
#else
                    QMetaObject::invokeMethod(dynamic_cast<RFIDCardHardwareReader*>(_reader), "selectCard",
                                              Qt::QueuedConnection, Q_ARG(QByteArray, pop.params.at(0).toByteArray()));
#endif
                }
                else
                {
                    queueAbort();
                    emit error(Warning, tr("Select TAG"), tr("Select TAG query failed"));
                }
                break;
            }
            case ReadLFMemory:
            {
                if(pop.params.size() == 2 &&
                   pop.params.at(1).canConvert(QVariant::ByteArray))
                {
                    QMetaObject::invokeMethod(_reader, "readLfMemory",
                                              Qt::QueuedConnection,
                                              Q_ARG(bool, pop.params.at(0).toBool()),
                                              Q_ARG(QByteArray, pop.params.at(1).toByteArray()));
                }
                else
                {
                    queueAbort();
                    LFMemoryReadResult result;
                    result.error = tr("Cannot read 125 kHz memory: invalid parameters");
                    emit lfMemoryReadFinished(result);
                }
                break;
            }
        }

        if(!reader_busy)
        {
            qWarning()<<"Reader is busy "<<reader_busy<<" after command execution - schedule next cmd";
            QMetaObject::invokeMethod(this, &RFIDCardReaderOperator::do_queuePop, Qt::QueuedConnection);
        }
    }
}

void RFIDCardReaderOperator::queueAbort()
{
    _queue.clear();
    reader_busy = false;
}
