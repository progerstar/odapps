#include "mifaresector.h"

#include <endianrw.h>

static int _mfClassicKeyMetaID = qRegisterMetaType<MifareClassicKey>();

QHash<quint8,MifareClassicTrailerAccess> makeTrailerAccessCombinations()
{
    MifareClassicTrailerAccess acc;
    /*keyA_read, keyA_write; bits_read, bits_write; keyB_read, keyB_write*/
    QHash<quint8,MifareClassicTrailerAccess> ret;

    acc = {MC_Permissions_None,
           MC_Permissions_KeyA,
           MC_Permissions_KeyA,
           MC_Permissions_None,
           MC_Permissions_KeyA,
           MC_Permissions_KeyA};
    ret.insert(0,acc);

    acc = {MC_Permissions_None,
           MC_Permissions_None,
           MC_Permissions_KeyA,
           MC_Permissions_None,
           MC_Permissions_KeyA,
           MC_Permissions_None};
    ret.insert(0x02,acc);

    acc = {MC_Permissions_None,
           MC_Permissions_KeyB,
           MifareClassicPermissions(MC_Permissions_KeyA|MC_Permissions_KeyB),
           MC_Permissions_None,
           MC_Permissions_None,
           MC_Permissions_KeyB};
    ret.insert(0x01,acc);

    acc = {MC_Permissions_None,
           MC_Permissions_None,
           MifareClassicPermissions(MC_Permissions_KeyA|MC_Permissions_KeyB),
           MC_Permissions_None,
           MC_Permissions_None,
           MC_Permissions_None};
    ret.insert(0x03,acc);

    acc = {MC_Permissions_None,
           MC_Permissions_KeyA,
           MC_Permissions_KeyA,
           MC_Permissions_KeyA,
           MC_Permissions_KeyA,
           MC_Permissions_KeyA};
    ret.insert(0x04,acc);

    acc = {MC_Permissions_None,
           MC_Permissions_KeyB,
           MifareClassicPermissions(MC_Permissions_KeyA|MC_Permissions_KeyB),
           MC_Permissions_KeyB,
           MC_Permissions_None,
           MC_Permissions_KeyB};
    ret.insert(0x06,acc);

    acc = {MC_Permissions_None,
           MC_Permissions_None,
           MifareClassicPermissions(MC_Permissions_KeyA|MC_Permissions_KeyB),
           MC_Permissions_KeyB,
           MC_Permissions_None,
           MC_Permissions_None};
    ret.insert(0x05,acc);

    acc = {MC_Permissions_None,
           MC_Permissions_None,
           MifareClassicPermissions(MC_Permissions_KeyA|MC_Permissions_KeyB),
           MC_Permissions_None,
           MC_Permissions_None,
           MC_Permissions_None};
    ret.insert(0x07,acc);

    return ret;
}

QHash<quint8,MifareClassicBlockAccess> makeBlockAccessCombinations()
{
    QHash<quint8,MifareClassicBlockAccess> ret;
    /*block_read, block_write; block_incr, block_decr*/
    MifareClassicBlockAccess acc;

    acc = {MifareClassicPermissions(MC_Permissions_KeyA|MC_Permissions_KeyB),
           MifareClassicPermissions(MC_Permissions_KeyA|MC_Permissions_KeyB),
           MifareClassicPermissions(MC_Permissions_KeyA|MC_Permissions_KeyB),
           MifareClassicPermissions(MC_Permissions_KeyA|MC_Permissions_KeyB)};
    ret.insert(0,acc);

    acc = {MifareClassicPermissions(MC_Permissions_KeyA|MC_Permissions_KeyB),
           MC_Permissions_None,
           MC_Permissions_None,
           MC_Permissions_None};
    ret.insert(0x02,acc);

    acc = {MifareClassicPermissions(MC_Permissions_KeyA|MC_Permissions_KeyB),
           MC_Permissions_KeyB,
           MC_Permissions_None,
           MC_Permissions_None};
    ret.insert(0x01,acc);

    acc = {MifareClassicPermissions(MC_Permissions_KeyA|MC_Permissions_KeyB),
           MC_Permissions_KeyB,
           MC_Permissions_KeyB,
           MifareClassicPermissions(MC_Permissions_KeyA|MC_Permissions_KeyB)};
    ret.insert(0x03,acc);

    acc = {MifareClassicPermissions(MC_Permissions_KeyA|MC_Permissions_KeyB),
           MC_Permissions_None,
           MC_Permissions_None,
           MifareClassicPermissions(MC_Permissions_KeyA|MC_Permissions_KeyB)};
    ret.insert(0x04,acc);

    acc = {MC_Permissions_KeyB,
           MC_Permissions_KeyB,
           MC_Permissions_None,
           MC_Permissions_None};
    ret.insert(0x06,acc);

    acc = {MC_Permissions_KeyB,
           MC_Permissions_None,
           MC_Permissions_None,
           MC_Permissions_None};
    ret.insert(0x05,acc);

    acc = {MC_Permissions_None,
           MC_Permissions_None,
           MC_Permissions_None,
           MC_Permissions_None};
    ret.insert(0x07,acc);

    return ret;
}

const QHash<quint8,MifareClassicTrailerAccess> MifareClassicFunctions::trailerAccessHash =
        makeTrailerAccessCombinations();
const QHash<quint8,MifareClassicBlockAccess>   MifareClassicFunctions::blockAccessHash =
        makeBlockAccessCombinations();


int MifareClassicFunctions::classicBlockAddress(int sector)
{
    if(sector>=MifareClassicFirstJumboSector)
    {
        return 4*MifareClassicFirstJumboSector+16*(sector-MifareClassicFirstJumboSector);
    }
    return sector*4;
}

int MifareClassicFunctions::classicBlocksCount(int sector)
{
    return (sector<MifareClassicFirstJumboSector) ? 4 : 16;
}

int MifareClassicFunctions::sectorNumber(int blockAddress)
{
    if(blockAddress > 4*MifareClassicFirstJumboSector)
    {
        return MifareClassicFirstJumboSector + ((blockAddress-4*MifareClassicFirstJumboSector)>>4);
    }
    return (blockAddress>>2);
}

quint8 MifareClassicSector::blockAccessBits(int block, BlockData sel) const
{
    const quint8* data = blocks.at(3).constData(sel);
    quint8 ret = 0;
    ret |= ((data[7]&(0x10<<block))?1:0);
    ret |= ((data[8]&(0x01<<block))?2:0);
    ret |= ((data[8]&(0x10<<block))?4:0);
    return ret;
}

quint8 MifareClassicJumboSector::blockAccessBits(int block, BlockData sel) const
{
    const quint8* data = blocks.at(15).constData(sel);
    quint8 ret = 0;
    if((block>=0)&&(block<=4))
    {
        ret |= ((data[7]&0x10)?1:0);
        ret |= ((data[8]&0x01)?2:0);
        ret |= ((data[8]&0x10)?4:0);
    }
    else if((block>=5)&&(block<=9))
    {
        ret |= ((data[7]&0x20)?1:0);
        ret |= ((data[8]&0x02)?2:0);
        ret |= ((data[8]&0x20)?4:0);
    }
    else if((block>=10)&&(block<=14))
    {
        ret |= ((data[7]&0x40)?1:0);
        ret |= ((data[8]&0x04)?2:0);
        ret |= ((data[8]&0x40)?4:0);
    }
    else if(block==15)
    {
        ret |= ((data[7]&0x80)?1:0);
        ret |= ((data[8]&0x08)?2:0);
        ret |= ((data[8]&0x80)?4:0);
    }
    return ret;
}

template<int N>
bool MifareClassicAbstractSector<N>::bitsValid(BlockData sel) const
{
    const quint8* bits_block = MifareSector<16,N>::at(N-1).constData(sel);
    if ( ((~(bits_block[6]>>4))&0xF) != (bits_block[8]&0xF))
        return false;
    if ( ((~(bits_block[7]>>4))&0xF) != (bits_block[6]&0xF))
        return false;
    if ( ((~(bits_block[8]>>4))&0xF) != (bits_block[7]&0xF))
        return false;

    return true;
}

void intSetBlockAccess(quint8* block, int number, quint8 bits)
{
    block[6] &= ~quint8(0x11 << number);
    quint8 val = (( (bits & 0x02) ? 0 : 1 )<<4) | ( (bits&0x01) ? 0 : 1);
    block[6] |= (val<<number);

    block[7] &= ~quint8(0x11 << number);
    val = (( (bits & 0x01) ? 1 : 0 )<<4) | ( (bits&0x04) ? 0 : 1);
    block[7] |= (val<<number);

    block[8] &= ~quint8(0x11 << number);
    val = (( (bits & 0x04) ? 1 : 0 )<<4) | ( (bits&0x02) ? 1 : 0);
    block[8] |= (val<<number);
}

void MifareClassicSector::setBlockAccess(int block, quint8 bits)
{
    if((block>=0)&&(block<=2))
    {
        intSetBlockAccess(&(blocks[3][0]),block,bits);
    }
    qDebug()<<"New bits set - config "<<(bitsValid(BD_Current) ? "valid" : "invalid");
}

void MifareClassicJumboSector::setBlockAccess(int block, quint8 bits)
{
    if((block>=0)&&(block<=4))
    {
        intSetBlockAccess(&(blocks[15][0]),0,bits);
    }
    else if((block>=5)&&(block<=9))
    {
        intSetBlockAccess(&(blocks[15][0]),1,bits);
    }
    else if((block>=10)&&(block<=14))
    {
        intSetBlockAccess(&(blocks[15][0]),2,bits);
    }
    qDebug()<<"New bits set - config "<<(bitsValid(BD_Current) ? "valid" : "invalid");
}

template<int N>
bool MifareClassicAbstractSector<N>::isReadProtected(int block, int byte, BlockData sel)
{
    if((sec_level == SL3) && (MifareSector<16,N>::blocks[N-1].constData(sel)[5] != 0x0F))
        return true;

#if 0
    /* NO!!! the same rules apply*/
    if(!MifareSectorInterface::sector_start_address && !block)
    {
        //UID & MD
        return false;
    }
#endif

    if(block==(N-1))
    {
        //trailer block
        if(byte<=5)
        {
            //keyA - always blocked
            return true;
        }
        else if(byte>=10)
        {
            //keyB
            return !bool(trailerAccessPermissions(sel).keyB_read & requiredKeyPermission());
        }
        else
        {
            //access bits
            return !bool(trailerAccessPermissions(sel).bits_read & requiredKeyPermission());
        }
    }

    //else - data block
    return !bool(blockAccessPermissions(block, sel).block_read & requiredKeyPermission());
}

template<int N>
bool MifareClassicAbstractSector<N>::isDeadLocked(int block, BlockData sel)
{
    /*
     * byte 5:
     *  When rewriting the access conditions after a switch to SL3 has been made
     *  this byte must be set to 0Fh, otherwise access to the sector will not be possible.
     */
    if((sec_level == SL3) && (MifareSector<16,N>::blocks[N-1].constData(sel)[5] != 0x0F))
        return true;

    return ((trailerAccessPermissions(sel).bits_write == MC_Permissions_None) &&
            (blockAccessPermissions(block, sel).block_write == MC_Permissions_None));
}

template<int N>
bool MifareClassicAbstractSector<N>::isWriteProtected(int block, int byte, BlockData sel)
{
    if((sec_level == SL3) && (MifareSector<16,N>::blocks[N-1].constData(sel)[5] != 0x0F))
        return true;

    if(!MifareSectorInterface::sector_start_address && !block)
    {
        //UID & MD
        return true;
    }

    if(block==(N-1))
    {
        if(byte<=5)
        {
            return ((trailerAccessPermissions(sel).keyA_write & requiredKeyPermission())?false:true);
        }
        else if(byte>=10)
        {
            //keyB
            return ((trailerAccessPermissions(sel).keyB_write & requiredKeyPermission())?false:true);
        }
        else
        {
            //access bits
            return ((trailerAccessPermissions(sel).bits_write & requiredKeyPermission())?false:true);
        }
    }

    //else - data block
    return ((blockAccessPermissions(block, sel).block_write & requiredKeyPermission())?false:true);
}

template<int N>
QString MifareClassicAbstractSector<N>::customEditName(int block)
{
#if 0
    /* We CAN edit the access bits for the 0 block */
    if(!MifareSectorInterface::sector_start_address && !block)
    {
        //UID & MD
        return QString();
    }
    else
#endif
    if(block==(N-1))
    {
        return QT_TRANSLATE_NOOP("MifareClassicSector", "Trailer");
    }
    //else
    return QT_TRANSLATE_NOOP("MifareClassicSector", "Access");
}

template<int N>
QString MifareClassicAbstractSector<N>::blockTooltip(int block, BlockData sel) const
{
    if(block != (N-1))
    {
        const quint8* data = MifareSector<16,N>::blocks[block].constData(sel);
        //if this is a value block, parse and display
#if 0
        qWarning()<<"Testing for value block: "
                  <<QString("0x%1, 0x%2 (0x%3), 0x%4, 0x%5, 0x%6 (0x%7)")
                    .arg(read_uint32_le(data), 8, 16, QLatin1Char('0'))
                    .arg(read_uint32_le(data+4), 8, 16, QLatin1Char('0'))
                    .arg(~read_uint32_le(data+4), 8, 16, QLatin1Char('0'))
                    .arg(read_uint32_le(data+8), 8, 16, QLatin1Char('0'))
                    .arg(data[12], 2, 16, QLatin1Char('0'))
                    .arg(data[13], 2, 16, QLatin1Char('0'))
                    .arg(~data[13], 2, 16, QLatin1Char('0'));
#endif
        uint32_t val = read_uint32_le(data);
        if(
           (val == read_uint32_le(data + 8))&&
           (val == ~read_uint32_le(data + 4))&&
           (data[12]==data[14])&&(data[13]==data[15])&&(data[12]==(~data[13]&0xFF))
           )
        {
            return QT_TRANSLATE_NOOP("MifareClassicSector", "Value block")+QString("\n")+
                   QT_TRANSLATE_NOOP("MifareClassicSector", "value")+QString(": %1").arg(static_cast<int32_t>(val))+
                   QString(", ")+QT_TRANSLATE_NOOP("MifareClassicSector", "address")+
                   QString(": 0x%1").arg(data[12],2,16,QLatin1Char('0'));
        }
    }
    return QString();
}

template<int N>
QString MifareClassicAbstractSector<N>::editHint(int block, int byte)
{
    if(!MifareSectorInterface::sector_start_address && !block)
    {
        //UID & MD
        return QT_TRANSLATE_NOOP("MifareClassicSector","UID and Manufacturer Data block. Read-only.");
    }
    else if(block==(N-1))
    {
        if(byte<=5)
        {
            return (sec_level == SL3) ?
                        ((byte==5) ?  QT_TRANSLATE_NOOP("MifareClassicSector", "Do not modify") :
                                      QT_TRANSLATE_NOOP("MifareClassicSector", "Not used") ):
                        QT_TRANSLATE_NOOP("MifareClassicSector", "Key A. Double click 'Edit' to modify");
        }
        else if(byte>=10)
        {
            return (sec_level == SL3) ?
                        QT_TRANSLATE_NOOP("MifareClassicSector", "Not used") :
                        QT_TRANSLATE_NOOP("MifareClassicSector","Key B. Double click 'Edit' to modify");
        }
        else
        {
            return QT_TRANSLATE_NOOP("MifareClassicSector","Access Bits. Double click 'Edit' to modify");
        }
    }
    return QString();
}

template<int N>
void MifareClassicAbstractSector<N>::setTrailerAccess(quint8 bits)
{
    intSetBlockAccess(&(MifareSector<16,N>::blocks[N-1][0]), 3, bits);
}

template<int N>
void MifareClassicAbstractSector<N>::setValueBlock(int block, qint32 value, quint8 address)
{
    if((block < 0) || (block > (N-2)) ||
       ((MifareSectorInterface::sector_start_address==0) && (block==0)))
        return;

    quint8* data = &((MifareSector<16,N>::blocks[block])[0]);

    uint32_t repr = static_cast<uint32_t>(value);
    write_uint32_le(repr, data);
    write_uint32_le(~repr, data+4);
    write_uint32_le(repr, data+8);
    data[12]=address;
    data[13]=~address;
    data[14]=address;
    data[15]=~address;
}

template class MifareClassicAbstractSector<4>;
template class MifareClassicAbstractSector<16>;
