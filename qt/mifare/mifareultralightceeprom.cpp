#include "mifaresector.h"

#include <endianrw.h>

const QStringList MifareUltralightCEEPROM::secondLockBitsLabels =
        QStringList()<<QT_TRANSLATE_NOOP("MifareUltralightCEEPROM","16-27 Bits")
                     <<QT_TRANSLATE_NOOP("MifareUltralightCEEPROM","16-19")
                     <<QT_TRANSLATE_NOOP("MifareUltralightCEEPROM","20-23")
                     <<QT_TRANSLATE_NOOP("MifareUltralightCEEPROM","24-27")
                     <<QT_TRANSLATE_NOOP("MifareUltralightCEEPROM","28-39 Bits")
                     <<QT_TRANSLATE_NOOP("MifareUltralightCEEPROM","28-31")
                     <<QT_TRANSLATE_NOOP("MifareUltralightCEEPROM","32-35")
                     <<QT_TRANSLATE_NOOP("MifareUltralightCEEPROM","36-39")
                     <<QT_TRANSLATE_NOOP("MifareUltralightCEEPROM","CNT Bit")
                     <<QT_TRANSLATE_NOOP("MifareUltralightCEEPROM","AUTH Page Bit")
                     <<QT_TRANSLATE_NOOP("MifareUltralightCEEPROM","AUTH Type Bit")
                     <<QT_TRANSLATE_NOOP("MifareUltralightCEEPROM","KEY Bit")
                     <<QT_TRANSLATE_NOOP("MifareUltralightCEEPROM","CNT")
                     <<QT_TRANSLATE_NOOP("MifareUltralightCEEPROM","AUTH Page")
                     <<QT_TRANSLATE_NOOP("MifareUltralightCEEPROM","AUTH Type")
                     <<QT_TRANSLATE_NOOP("MifareUltralightCEEPROM","KEY");


bool MifareUltralightCEEPROM::isReadProtected(int block, int /*byte*/, BlockData)
{
    return (block>=44);
}

bool MifareUltralightCEEPROM::isDeadLocked(int block, BlockData sel)
{
    if(block<2)
        return false;


    if(block==2)
    {
        //any of the block-locking bits
        return (firstLockBits(sel) & 0x7);
    }
    else if(block==40)
    {
        return (secondLockBits(sel) & 0xF11);
    }
    return isWriteProtected(block,0,sel);
}

quint16 MifareUltralightCEEPROM::firstLockBits(BlockData sel) const
{
    return read_uint16_le(blocks.at(2).constData(sel)+2);
}

quint16 MifareUltralightCEEPROM::secondLockBits(BlockData sel) const
{
    return read_uint16_le(blocks.at(40).constData(sel));
}

void MifareUltralightCEEPROM::setFirstLockBits(quint16 bits)
{
    write_uint16_le(bits,&(blocks[2][2]));
}

void MifareUltralightCEEPROM::setSecondLockBits(quint16 bits)
{
    write_uint16_le(bits,&(blocks[40][0]));
}

quint32 MifareUltralightCEEPROM::otp(BlockData sel) const
{
    return read_uint32_le(blocks.at(3).constData(sel));
}

void MifareUltralightCEEPROM::setOTP(quint32 value)
{
    quint32 new_otp = otp(BD_Saved) | value;
    write_uint32_le(new_otp,&(blocks[3][0]));
}

MifareUltralightKey MifareUltralightCEEPROM::getKey(MifareUltralightCKeyType type, BlockData sel) const
{
    quint64 key_val = 0;
    switch(type)
    {
        case MifareUltralightCKey1:
        {
            key_val = read_uint32_le(blocks.at(45).constData(sel));
            key_val<<=32;
            key_val |= read_uint32_le(blocks.at(44).constData(sel));
            break;
        }
        case MifareUltralightCKey2:
        {
            key_val = read_uint32_le(blocks.at(47).constData(sel));
            key_val<<=32;
            key_val |= read_uint32_le(blocks.at(46).constData(sel));
            break;
        }
    }
    return MifareUltralightKey(key_val);
}

bool MifareUltralightCEEPROM::setKey(MifareUltralightCKeyType type, const MifareUltralightKey& key)
{
    if(isWriteProtected(44, 0, BD_Saved))
        return false;

    int start = (type==MifareUltralightCKey1)?44:46;
    blocks[start].fromRaw(key.raw());
    blocks[start+1].fromRaw(key.raw()+4);
    return true;
}

bool MifareUltralightCEEPROM::isWriteProtected(int block, int byte, BlockData sel)
{
    if((block<2)||((block==2)&&(byte<2)))
    {
        //UID && internal data
        return true;
    }

    if(block==2)
    {
        //the locking bits themselves
        return (byte==2) ? ((blocks.at(2).constData(sel)[2] & 0x3)==0x3) /*OTP and 9-4 locked*/ :
                           ((blocks.at(2).constData(sel)[2] & 0x6)==0x6) /*15-10 and 9-4*/;
    }

    if(block==40)
    {
        return (byte==0) ? ((blocks.at(40).constData(sel)[0] & 0x11)==0x11):
                           ((blocks.at(40).constData(sel)[1] & 0x0F)==0x0F);
    }

    //by bits
    if(block<16)
    {
        quint16 flock = firstLockBits(sel);
        return (flock & (1<<block));
    }

    quint16 slock = secondLockBits(sel);

    if(block<40)
    {
        int block_bit = (block<28) ? 1+((block-16)>>2) : 2+((block-16)>>2);
        return (slock & (1<<block_bit));
    }

    //41--47;
    return slock & (1<<(8+4+(qMin(block,44)-41)));
}

bool MifareUltralightCEEPROM::canReadProtect(int block)
{
    return (block>=3);
}

bool MifareUltralightCEEPROM::canWriteProtect(int block)
{
    return (block>=3);
}

bool MifareUltralightCEEPROM::readProtect(int block)
{
    //use special widget
    Q_UNUSED(block);
    return false;
}

bool MifareUltralightCEEPROM::writeProtect(int block)
{
    if(block<=2)
        return false;

    if(block<16)
    {
        quint16 lock = firstLockBits(BD_Current);
        lock |= (1<<block);
        setFirstLockBits(lock);
        return true;
    }
    else if(block<40)
    {

    }
#warning stub!
    return false;
}

#warning finish!
bool MifareUltralightCEEPROM::forceBinDisplay(int block, int byte)
{
    return (((block==2)&&(byte>=2))||(block==3)||((block==40)&&(byte<2)));
}

bool MifareUltralightCEEPROM::isEditable(int block, int byte, BlockData sel)
{
    Q_UNUSED(sel);
    return ((block>2)||((block==2)&&(byte>=2)));
}

bool MifareUltralightCEEPROM::customEdit(int block, int byte)
{
    return (((block==2)&&(byte>=2))||(block==3));
}

QString MifareUltralightCEEPROM::customEditName(int block)
{
    if(block==2)
    {
        return QT_TRANSLATE_NOOP("MifareUltralightCEEPROM","Lock Bits");
    }
    else if(block==3)
    {
        return QT_TRANSLATE_NOOP("MifareUltralightCEEPROM","OTP");
    }
    //else
    return QString();
}

QString MifareUltralightCEEPROM::editHint(int block,int byte)
{
    if(block<=2)
    {
        if((block==2)&&(byte>=2))
        {
            return QT_TRANSLATE_NOOP("MifareUltralightCEEPROM",
                                     "Lock bits. Double click \"Edit\" to manage write access.");
        }
        else
        {
            return QT_TRANSLATE_NOOP("MifareUltralightCEEPROM",
                                     "UID and Manufacturer's Data. Read-only.");
        }
    }
    else if(block==3)
    {
        return QT_TRANSLATE_NOOP("MifareUltralightCEEPROM",
                                 "OTP - one time programmable block. Double click \"Edit\" to modify.");
    }
    return QString();
}

