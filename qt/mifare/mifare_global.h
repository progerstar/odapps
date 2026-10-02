#ifndef MIFARE_GLOBAL_H
#define MIFARE_GLOBAL_H

#include <QString>
#include <QHash>
#include <QChar>
#include <QDebug>

#define MifareClassicFirstJumboSector 32

enum RFIDErrors
{
    MFRC522_ERROR_OK        = 0x00,  /* No error */
    MFRC522_ERROR_INVARG    = 0x01,  /* Protocol error -> auth fail if command==authenticate */
    MFRC522_ERROR_PAR       = 0x02,  /* Parity error*/
    MFRC522_ERROR_CRC       = 0x04,  /* Checksum error */
    MFRC522_ERROR_COLL      = 0x08,  /* Collision */
    MFRC522_ERROR_OVERFLOW  = 0x10,  /* Buffer overflow */
    MFRC522_ERROR_TEAR      = 0x20,  /* Tear event */
    MFRC522_ERROR_TEMP      = 0x40,  /* Overheating */
    MFRC522_ERROR_WRITE     = 0x80,  /* FIFO Write Error */

    MFRC522_ERROR_TIMEOUT   = 0x100, /* Timeout */
    MFRC522_ERROR_NACK      = 0x200, /* Mifare NAK */
    MFRC522_ERROR_AUTH      = 0x400, /* Authentication failure */
    MFRC522_ERROR_COMM      = 0x800, /* Generic communication error */
};

Q_DECLARE_FLAGS(RFIDError,RFIDErrors)

inline QString rfidUsbError(quint32 error)
{
    QString ret;
    if(error==MFRC522_ERROR_OK)
        return QT_TRANSLATE_NOOP("MFRC522","No Error");

    if(error&MFRC522_ERROR_COMM)
        ret+=QT_TRANSLATE_NOOP("MFRC522","Communication error;");
    if(error&MFRC522_ERROR_COLL)
        ret+=QT_TRANSLATE_NOOP("MFRC522","Collision error;");
    if(error&MFRC522_ERROR_OVERFLOW)
        ret+=QT_TRANSLATE_NOOP("MFRC522","Overflow error;");
    if(error&MFRC522_ERROR_INVARG)
        ret+=QT_TRANSLATE_NOOP("MFRC522","Invalid argument / Authentication error;");
    if(error&MFRC522_ERROR_CRC)
        ret+=QT_TRANSLATE_NOOP("MFRC522","Checksum error;");
    if(error&MFRC522_ERROR_NACK)
        ret+=QT_TRANSLATE_NOOP("MFRC522","NACK error;");
    if(error&MFRC522_ERROR_TIMEOUT)
        ret+=QT_TRANSLATE_NOOP("MFRC522","Timeout error;");
    if(error&MFRC522_ERROR_TEAR)
        ret+=QT_TRANSLATE_NOOP("MFRC522","Tear event;");
    if(error&MFRC522_ERROR_AUTH)
        ret+=QT_TRANSLATE_NOOP("MFRC522","Authentication error;");
    if(error&MFRC522_ERROR_PAR)
        ret+=QT_TRANSLATE_NOOP("MFRC522","Parity error;");
    if(error&MFRC522_ERROR_TEMP)
        ret+=QT_TRANSLATE_NOOP("MFRC522","Overheating;");
    if(error&MFRC522_ERROR_WRITE)
        ret+=QT_TRANSLATE_NOOP("MFRC522","FIFO write error;");

    return ret;
}

//////////////////////////////////////

/*Keep synced with mfrc522.h -> PICC_Types */
enum MifareCards
{
    MF_CLASSIC_1K          = 0,
    MF_CLASSIC_4K          = 1,
    MF_CLASSIC_Mini        = 2,
    MF_ULTRALIGHT          = 3,
    MF_ULTRALIGHT_C        = 4,
    MF_ULTRALIGHT_EV1_80   = 5,
    MF_ULTRALIGHT_EV1_164  = 6,
    MF_PLUS_S_2K_SL1       = 7,
    MF_PLUS_S_4K_SL1       = 8,
    MF_DESFIRE             = 9,
/*Keep values 10 & 11 reserved for compatibility*/
    MF_DESFIRE_C1          = 10,
    MF_DESFIRE_C2          = 11,

    MF_CLASSIC_2K           = 12,
    MF_PLUS_X_2K_SL1        = 13,
    MF_PLUS_X_4K_SL1        = 14,

    MF_PLUS_X_SL0           = 15,
    MF_PLUS_X_2K_SL2        = 16,
    MF_PLUS_X_4K_SL2        = 17,
    MF_PLUS_X_SL3           = 18,

    MF_PLUS_S_SL0           = 19,
    MF_PLUS_S_2K_SL2        = 20,
    MF_PLUS_S_4K_SL2        = 21,
    MF_PLUS_S_SL3           = 22,

    MF_NTAG213              = 23,
    MF_NTAG215              = 24,
    MF_NTAG216              = 25,
    MF_ULTRALIGHT_NANO      = 26,

    MF_EM_4100              = 27,
    MF_NTAG413              = 28,
    MF_NTAG424              = 29,
    MF_PLUS_X_SL3_4K        = 30,
    MF_PLUS_S_SL3_4K        = 31,
    MF_ISO_14443_4          = 32,
    MF_HID_PROX             = 33,

    MF_UNKNOWN              = 255
};

Q_DECLARE_METATYPE(MifareCards)

#define MF_LAST_KNOWN_CARD (MF_HID_PROX)

extern const QHash<int,QString> icManufacturerDB;
extern const QStringList mifareCardsNames;

inline QString mifareCardName(int card)
{
    if((card>=0)&&(card<=MF_LAST_KNOWN_CARD))
        return mifareCardsNames.at(card);
    return QT_TRANSLATE_NOOP("MifareCards","Unknown Tag");
}

inline bool isUnsupportedCard(int type)
{
    switch(type)
    {
        case MF_CLASSIC_1K:
        case MF_CLASSIC_2K:
        case MF_CLASSIC_4K:
        case MF_CLASSIC_Mini:
        case MF_ULTRALIGHT:
        case MF_ULTRALIGHT_NANO:
        case MF_ULTRALIGHT_EV1_80:
        case MF_ULTRALIGHT_EV1_164:
        case MF_PLUS_S_2K_SL1:
        case MF_PLUS_X_2K_SL1:
        case MF_PLUS_S_4K_SL1:
        case MF_PLUS_X_4K_SL1:
        case MF_NTAG213:
        case MF_NTAG215:
        case MF_NTAG216:
        case MF_EM_4100:
        case MF_HID_PROX:
            return false;
        default: break;
    }
    return true;
}

inline bool isUltralightCard(int type)
{
    switch(type)
    {
        case MF_ULTRALIGHT:
        case MF_ULTRALIGHT_NANO:
        case MF_ULTRALIGHT_C:
        case MF_ULTRALIGHT_EV1_80:
        case MF_ULTRALIGHT_EV1_164:
        case MF_NTAG213:
        case MF_NTAG215:
        case MF_NTAG216:
            return true;
        default:
            break;
    }
    return false;
}

inline bool isUltralightEV1Card(int type)
{
    switch(type)
    {
        case MF_ULTRALIGHT_EV1_80:
        case MF_ULTRALIGHT_EV1_164:
        case MF_NTAG213:
        case MF_NTAG215:
        case MF_NTAG216:
            return true;
        default:
            break;
    }
    return false;
}

inline bool isClassicCard(int type)
{
    switch (type)
    {
        case MF_CLASSIC_1K:
        case MF_CLASSIC_2K:
        case MF_CLASSIC_4K:
        case MF_CLASSIC_Mini:
        case MF_PLUS_S_2K_SL1:
        case MF_PLUS_S_4K_SL1:
        case MF_PLUS_X_2K_SL1:
        case MF_PLUS_X_4K_SL1:
            return true;
        default:
            break;
    }
    return false;
}

inline bool isEmMarineCard(int type)
{
    return (type == MF_EM_4100);
}

inline bool isHidProxCard(int type)
{
    return (type == MF_HID_PROX);
}

inline bool isLfCard(int type)
{
    return isEmMarineCard(type) || isHidProxCard(type);
}

inline quint8 classicSectorCount(int type)
{
    switch(type)
    {
        case MF_CLASSIC_1K:
            return 16;
        case MF_CLASSIC_2K:
        case MF_PLUS_S_2K_SL1:
        case MF_PLUS_S_2K_SL2:
        case MF_PLUS_X_2K_SL1:
        case MF_PLUS_X_2K_SL2:
            return 32;
        case MF_CLASSIC_4K:
        case MF_PLUS_S_4K_SL1:
        case MF_PLUS_S_4K_SL2:
        case MF_PLUS_X_4K_SL1:
        case MF_PLUS_X_4K_SL2:
            return 40;
        case MF_CLASSIC_Mini:
            return 5;
        case MF_PLUS_X_SL3:
        case MF_PLUS_S_SL3:
        case MF_PLUS_X_SL3_4K:
        case MF_PLUS_S_SL3_4K:
            /*Is there a way to tell apart 2K / 4K at this point?*/
            return 40;
        default:
            break;
    }

    return 0;
}

inline bool isClassicSectorStart(quint8 block)
{
    if(block<128)
    {
        return ((block & 0x03)==0);
    }
    else
    {
        return (((block-128) & 0x0F)==0);
    }
}

enum DataFormat
{
    DR_Bin   =   2,
    DR_Oct   =   8,
    DR_Dec   =  10,
    DR_Hex   =  16,
    DR_ASCII = 255
};

enum AccessMode
{
    AccessBlocked = 0x00,
    ReadAccess    = 0x01,
    WriteAccess   = 0x02
};

enum ExtendedAccessMode
{
    AccessExBlocked   = 0x00,
    ReadExAccess      = 0x01,
    WriteExAccess     = 0x02,
    IncrementExAccess = 0x04,
    DecrementExAccess = 0x08
};

enum MifareClassicPermission
{
    MC_Permissions_None = 0x00,
    MC_Permissions_KeyA = 0x01,
    MC_Permissions_KeyB = 0x02
};

/*Keep this 0 and 1 - this is vital!*/
enum MifareClassicKeyType
{
    MifareClassicKeyA = 0,
    MifareClassicKeyB = 1
};

Q_DECLARE_METATYPE(MifareClassicKeyType)

enum MifareUltralightCKeyType
{
    MifareUltralightCKey1 = 0,
    MifareUltralightCKey2 = 1
};

class MifareUltralightEV1Security
{
    public:
        MifareUltralightEV1Security() : password(0xFFFFFFFFUL), pack(0x00){}
        MifareUltralightEV1Security(quint32 psw, quint16 pk) : password(psw), pack(pk){}

        quint32 password;
        quint16 pack;
};

inline QDebug operator<<(QDebug dbg, const MifareUltralightEV1Security& acc)
{
    dbg.nospace() << QString("[%1:%2]").arg(acc.password, 8, 16, QLatin1Char('0')).arg(acc.pack, 4, 16, QLatin1Char('0'));
    return dbg.maybeSpace();
}

Q_DECLARE_FLAGS(AccessModes,AccessMode)
Q_DECLARE_METATYPE(AccessModes)
Q_DECLARE_OPERATORS_FOR_FLAGS(AccessModes)

Q_DECLARE_FLAGS(ExtendedAccessModes, ExtendedAccessMode)
Q_DECLARE_OPERATORS_FOR_FLAGS(ExtendedAccessModes)
Q_DECLARE_FLAGS(MifareClassicPermissions, MifareClassicPermission)
Q_DECLARE_OPERATORS_FOR_FLAGS(MifareClassicPermissions)

inline QString mifareClassicPermissionString(MifareClassicPermissions p)
{
    if(p==(MC_Permissions_KeyA|MC_Permissions_KeyB))
    {
        return QT_TRANSLATE_NOOP("MifarePermissions","key A|B");
    }
    else if(p==MC_Permissions_KeyA)
    {
        return QT_TRANSLATE_NOOP("MifarePermissions","key A");
    }
    else if(p==MC_Permissions_KeyB)
    {
        return QT_TRANSLATE_NOOP("MifarePermissions","key B");
    }

    return QT_TRANSLATE_NOOP("MifarePermissions","never");
}

typedef struct
{
        MifareClassicPermissions keyA_read, keyA_write;
        MifareClassicPermissions bits_read, bits_write;
        MifareClassicPermissions keyB_read, keyB_write;
} MifareClassicTrailerAccess;

inline QDebug operator<<(QDebug dbg, const MifareClassicTrailerAccess &acc)
{
    dbg.nospace() << "KEYA: read - "
                  << mifareClassicPermissionString(acc.keyA_read)
                  << ", write - "
                  << mifareClassicPermissionString(acc.keyA_write)
                  << "; Access Bits: read - "
                  << mifareClassicPermissionString(acc.bits_read)
                  << ", write - "
                  << mifareClassicPermissionString(acc.bits_write)
                  << "; KEYB: read - "
                  << mifareClassicPermissionString(acc.keyB_read)
                  << ", write - "
                  << mifareClassicPermissionString(acc.keyB_write)<< ";";
    return dbg.maybeSpace();
}

typedef struct
{
        MifareClassicPermissions block_read, block_write;
        MifareClassicPermissions block_incr, block_decr;
} MifareClassicBlockAccess;

inline QDebug operator<<(QDebug dbg, const MifareClassicBlockAccess& acc)
{
    dbg.nospace() << "Read: "<< mifareClassicPermissionString(acc.block_read)
                  << "; Write: "<< mifareClassicPermissionString(acc.block_write)
                  << "; Incr: "<< mifareClassicPermissionString(acc.block_incr)
                  << "; Decr: "<< mifareClassicPermissionString(acc.block_decr);
    return dbg.maybeSpace();
}

inline QString numberToString(int number, DataFormat fmt)
{
    switch(fmt)
    {
        case DR_Bin: return QString("%1").arg(number,8,int(fmt),QLatin1Char('0')).toUpper();
        case DR_Dec: return QString("%1").arg(number,0,int(fmt),QLatin1Char('0')).toUpper();
        case DR_Oct: return QString("%1").arg(number,3,int(fmt),QLatin1Char('0')).toUpper();
        case DR_Hex: return QString("%1").arg(number,2,int(fmt),QLatin1Char('0')).toUpper();
        default:break;
    }

    //ascii
    return ((number>=32)&&(number<=126)?QString(QChar(number)):QString("."));
}

inline int stringToNumber(const QString& text, DataFormat fmt, bool* res = 0)
{
    bool ok;
    if(fmt != DR_ASCII)
    {
        int n = text.toInt(&ok,int(fmt));
        if(ok)
        {
            if(res) *res = true;
            return n;
        }
    }
    else
    {
        if((text.size()==1)&&(text.at(0).unicode()<=255) )
        {
            if(res) *res = true;
            return text.at(0).unicode();
        }
    }

    if(res) *res = false;
    return 0;
}

#endif // MIFARE_GLOBAL_H
