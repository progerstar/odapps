#include "rfidcardreaderinterface.h"
#include "mifaresector.h"

#include <QDateTime>

const QStringList RFIDCardReaderInterface::readErrorStrings = QStringList()
                                                              <<RFIDCardReaderInterface::tr("No error")
                                                              <<RFIDCardReaderInterface::tr("Overflow")
                                                              <<RFIDCardReaderInterface::tr("Generic Error")
                                                              <<RFIDCardReaderInterface::tr("Access Denied");

quint16 RFIDCardReaderInterface::plusKeyBNr(quint16 blockAddress, MifareClassicKeyType type)
{
    /*
     * Sector keys:
     * 0x4000 - 0x403F for sector 0 - 31 (blocks 0 - 127)
     * 0x4040 - 0x404F for sector 32 .. 39
     *
     *  - 0x4000 + 2 * <sector> + (B ? 1 : 0) (needs self-auth to be changed)
     *
     *
     * Misc:
     * 0xB001 - installation identifier - needs Master Key
     *  - 0x9000
     * 0xB002 - ATS information - needs Master Key
     *  - 0x9000
     * 0xB003 - Field Configuration Block - needs Configuration Key
     *  - 0x9001
     *
     * Keys:
     * 0x8000 - originality key - as is
     * 0x9000 - Master Key - needs self
     * 0x9001 - Configuration key - needs self (or Master Key - docs say MK does it, experiment says NO!)
     * 0x9002 - L2 Switch key - ?
     * 0x9003 - L3 Switch key - ?
     * 0x9004 - SL1 AES key - ?
     * 0x9006 - L3SectorSwitchKey - ???
     * 0x9007 - L1L3MixSectorSwitchKey - ???
     * 0xA080 - VC Polling ENCKey - needs Configuration Key
     *  - 0x9001
     * 0xA081 - VC Polling MACKey - needs Configuration Key
     *  - 0x9001
     * 0xC000 - TransactionMACKey1 - ???
     * 0xC001 - TransactionMACConfKey1 - ???
     */

    uint16_t keyBRn = blockAddress;
    if(blockAddress < 256)
    {
        keyBRn = 0x4000 + (MifareClassicFunctions::sectorNumber(blockAddress)<<1) + (uint16_t)type;
    }
    else if((blockAddress == 0xB001)||(blockAddress == 0xB002))
    {
        keyBRn = 0x9000;
    }
    else if((blockAddress == 0xB003)||(blockAddress == 0xA080)||(blockAddress == 0xA081))
    {
        keyBRn = 0x9001;
    }
    return keyBRn;
}

bool RFIDCardReaderInterface::isExtCard(quint8 type)
{
    return ((type==MF_PLUS_S_SL0)||(type==MF_PLUS_X_SL0)||
            (type==MF_PLUS_S_2K_SL2)||(type==MF_PLUS_X_2K_SL2)||
            (type==MF_PLUS_S_4K_SL2)||(type==MF_PLUS_X_4K_SL2)||
            (type==MF_PLUS_S_SL3)||(type==MF_PLUS_X_SL3)||
            (type==MF_PLUS_S_SL3_4K)||(type==MF_PLUS_X_SL3_4K));
}

bool RFIDCardReaderInterface::hasCapability(const QVersionNumber& num, Capability cap)
{
    int rev = num.microVersion();
    switch(cap) {
        case HF_Reader: {
            return (rev == REV_FirstGen) ||
                    (rev == REV_SecondGen_High) ||
                    (rev == REV_SecondGen_HighLow) ||
                    (rev == REV_ThirdGen_High) ||
                    (rev == REV_ThirdGen_HighLow);
        }
        case EM_Reader: {
            return (rev == REV_SecondGen_Low) ||
                    (rev == REV_SecondGen_HighLow) ||
                    (rev == REV_ThirdGen_Low) ||
                    (rev == REV_ThirdGen_HighLow);
        }
        case MF_Plus: {
            return ((rev == REV_FirstGen) && (num >= QVersionNumber(1, 6, 0))) ||
                    (rev == REV_SecondGen_High) ||
                    (rev == REV_ThirdGen_High) ||
                    (rev == REV_ThirdGen_HighLow);
        }
        case MF_Ulev: {
            return ((rev == REV_FirstGen) && (num >= QVersionNumber(1, 5, 0))) ||
                        (rev == REV_SecondGen_High) || (rev == REV_SecondGen_HighLow) ||
                        (rev == REV_ThirdGen_High) || (rev == REV_ThirdGen_HighLow);
        }
        case IF_Switch: {
            //return ((rev >= REV_ThirdGen_High) && (rev <= REV_ThirdGen_HighLow));
            return (rev == REV_ThirdGen_Low) && (num.minorVersion() == 0);
        }
        case IF_Combo: {
            return ((rev >= REV_ThirdGen_High) && (rev <= REV_ThirdGen_HighLow)) &&
                    !((rev == REV_ThirdGen_Low) && (num.minorVersion() == 0));
        }
        case IND_Led: {
            return (rev == REV_FirstGen) ||
                    ((rev >= REV_ThirdGen_High) && (rev <= REV_ThirdGen_HighLow));
        }
        case IND_Buzz: {
            return true;
        }
        case DFU_MS: {
            return (num.majorVersion() >= 3) || ((num.majorVersion() == 2) && (num.minorVersion() >= 9));
        }
        case USB_Profile: {
            /* AT+USBMODE exists in the third generation combo firmware since 3.14 */
            return hasCapability(num, IF_Combo) && (num >= QVersionNumber(3, 14, 0));
        }
    }
    return false;
}

bool RFIDCardReaderInterface::hasCapabilities(const QVersionNumber& num, RFIDCardReaderInterface::Capabilities caps)
{
    if(!caps) {
        return false;
    }

    if((caps & HF_Reader) && !hasCapability(num, HF_Reader)) {
        return false;
    }
    if((caps & EM_Reader) && !hasCapability(num, EM_Reader)) {
        return false;
    }
    if((caps & MF_Plus) && !hasCapability(num, MF_Plus)) {
        return false;
    }
    if((caps & MF_Ulev) && !hasCapability(num, MF_Ulev)) {
        return false;
    }
    if((caps & IF_Switch) && !hasCapability(num, IF_Switch)) {
        return false;
    }
    if((caps & IF_Combo) && !hasCapability(num, IF_Combo)) {
        return false;
    }
    if((caps & IND_Led) && !hasCapability(num, IND_Led)) {
        return false;
    }
    if((caps & DFU_MS) && !hasCapability(num, DFU_MS)) {
        return false;
    }
    if((caps & USB_Profile) && !hasCapability(num, USB_Profile)) {
        return false;
    }

    /* BUZZER - all true */
    return true;
}

unsigned int RFIDCardReaderInterface::cfglineSize(const QVersionNumber& num) {
    if(num.microVersion() >= REV_ThirdGen_High) {
        return 58;
    }
    return 32;
}

RFIDCardReaderInterface::RFIDCardReaderInterface(QObject *parent) : QObject(parent),
    reply_timer(nullptr), m_card_present(false),
    tx_timestamp(QDateTime::currentMSecsSinceEpoch()),
    block_address(0), block_count(0),
    current_card_info(),
    mfc_key_type(MifareClassicKeyA),
    mfp_aes_type(MifareClassicKeyA),
    ul_pwd(0), ul_pack(0)
{
    current_card_info.clear();
}

RFIDCardReaderInterface::~RFIDCardReaderInterface()
{
    if(reply_timer)
    {
        reply_timer->stop();
        delete reply_timer;
        reply_timer = nullptr;
    }
}

void RFIDCardReaderInterface::close()
{
    if(m_card_present)
    {
        emit cardRemoved();
    }

    m_card_present = false;
    current_card_info.clear();
}

void RFIDCardReaderInterface::plusAuth(quint16 blockAddress)
{
    if(!plusAuthKey(RFIDCardReaderInterface::plusKeyBNr(blockAddress, mfp_aes_type)))
    {
        emit plusAuthorized(false);
    }
}

void RFIDCardReaderInterface::readLfMemory(bool fullScan, const QByteArray& password)
{
    Q_UNUSED(fullScan)
    Q_UNUSED(password)

    LFMemoryReadResult result;
    result.error = tr("125 kHz memory reading is not supported by this connection");
    emit lfMemoryReadFinished(result);
}

void RFIDCardReaderInterface::startReplyTimer(int ms)
{
    if(!reply_timer)
    {
        reply_timer = new QTimer(this);
        reply_timer->setInterval(ms);
        reply_timer->setSingleShot(true);
        connect(reply_timer, SIGNAL(timeout()), this, SLOT(nack()));
    }
    else
    {
        reply_timer->stop();
        reply_timer->setInterval(ms);
    }
    reply_timer->start();
}

void RFIDCardReaderInterface::stopReplyTimer()
{
    if(reply_timer)
    {
        reply_timer->stop();
    }
}

bool RFIDCardReaderInterface::transmit(const QByteArray& data)
{
    tx_timestamp = QDateTime::currentMSecsSinceEpoch();
    return hal_transmit(data);
}
