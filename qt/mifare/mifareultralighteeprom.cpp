#include "mifaresector.h"

template<int Pages>
bool MifarePlainUltralightEEPROM<Pages>::isWriteProtected(int block, int byte, BlockData sel)
{
    return MifareUltralightAbstractEEPROM<Pages>::isWriteProtected0_15(block, byte, sel);
}

template<int Pages>
bool MifarePlainUltralightEEPROM<Pages>::forceBinDisplay(int block, int byte)
{
    return (((block==2)&&(byte>=2))||(block==3));
}

template<int Pages>
bool MifarePlainUltralightEEPROM<Pages>::isEditable(int block, int byte, BlockData)
{
    return ((block>2)||((block==2)&&(byte>=2)));
}

template<int Pages>
bool MifarePlainUltralightEEPROM<Pages>::customEdit(int block, int byte)
{
    return (((block==2)&&(byte>=2))||(block==3));
}

template<int Pages>
QString MifarePlainUltralightEEPROM<Pages>::customEditName(int block)
{
    if(block == 2)
    {
        return QT_TRANSLATE_NOOP("MifareUltralightEEPROM","Lock Bits");
    }
    else if(block==3)
    {
        return QT_TRANSLATE_NOOP("MifareUltralightEEPROM","OTP");
    }
    //else
    return QString();
}

template<int Pages>
QString MifarePlainUltralightEEPROM<Pages>::editHint(int block,int byte)
{
    if(block<=2)
    {
        if((block==2)&&(byte>=2))
        {
            return QT_TRANSLATE_NOOP("MifareUltralightEEPROM",
                                     "Lock bits. Double click \"Edit\" to manage write access.");
        }
        else
        {
            return QT_TRANSLATE_NOOP("MifareUltralightEEPROM",
                                     "UID and Manufacturer's Data. Read-only.");
        }
    }
    else if(block==3)
    {
        return QT_TRANSLATE_NOOP("MifareUltralightEEPROM",
                                 "OTP - one time programmable block. Double click \"Edit\" to modify.");
    }
    return QString();
}

template class MifarePlainUltralightEEPROM<16>;
template class MifarePlainUltralightEEPROM<14>;
