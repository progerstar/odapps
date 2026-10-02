#include "mifaresector.h"

const QStringList MifareNTAG213::dynamicLockBits_Labels =
        QStringList()<<"16-17"<<"18-19"<<"20-21"<<"22-23"<<"24-25"<<"26-27"
                     <<"28-29"<<"30-31"<<"32-33"<<"34-35"<<"36-37"<<"38-39"
                    <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "RFU")
                    <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "RFU")
                    <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "RFU")
                    <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "RFU")
                   <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "16-19 Bits")
                   <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "20-23 Bits")
                   <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "24-27 Bits")
                   <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "28-31 Bits")
                   <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "32-35 Bits")
                   <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "36-39 Bits");


const QStringList MifareNTAG215::dynamicLockBits_Labels =
        QStringList()<<"16-31"<<"32-47"<<"48-63"<<"64-79"<<"80-95"<<"96-111"
                     <<"112-127"<<"128-129"
                     <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "RFU")
                     <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "RFU")
                     <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "RFU")
                     <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "RFU")
                    <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "RFU")
                    <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "RFU")
                    <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "RFU")
                    <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "RFU")
                   <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "16-47 Bits")
                   <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "48-79 Bits")
                   <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "80-111 Bits")
                   <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "112-129 Bits");

const QStringList MifareNTAG216::dynamicLockBits_Labels =
        QStringList()<<"16-31"<<"32-47"<<"48-63"<<"64-79"<<"80-95"<<"96-111"
                     <<"112-127"<<"128-143"<<"144-159"<<"160-175"<<"176-191"
                     <<"192-207"<<"208-223"<<"224-225"
                     <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "RFU")
                     <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "RFU")
                   <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "16-47 Bits")
                   <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "48-79 Bits")
                   <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "80-111 Bits")
                   <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "112-143 Bits")
                   <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "144-175 Bits")
                   <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "176-207 Bits")
                   <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "207-225 Bits");

/*******************************************************/

template<int N>
bool MifareAbstractNTAG<N>::customEdit(int block, int byte)
{
    switch (block)
    {
        case 2: /*Lock*/
            return (byte>=2);
        case 3: /*OTP*/
            return true;
        case (N-5): /*Lock Aux*/
            return (byte<3);
        case (N-4): /*CFG0*/
            return (byte!=1);
        case (N-3): /*CFG1*/
            return (byte==0);
        case (N-2): /*PWD*/
            return true;
        case (N-1): /*PACK*/
            return (byte<2);
        default:break;
    }
    return false;
}

template<int N>
QString MifareAbstractNTAG<N>::customEditName(int block)
{
    switch(block)
    {
        case 2:
            return QT_TRANSLATE_NOOP("MifareUltralightEEPROM","Lock Bits");
        case 3:
            return QT_TRANSLATE_NOOP("MifareNTAGEEPROM","OTP/CC");
        case (N-5):
            return QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM","Lock Bits 2");
        case (N-4):
            return QT_TRANSLATE_NOOP("MifareNTAGEEPROM","MIRROR & AUTH0");
        case (N-3):
            return QT_TRANSLATE_NOOP("MifareNTAGEEPROM","ACCESS");
        case (N-2):
            return QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM","PWD");
        case (N-1):
            return QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM","PACK");
        default:
            break;
    }
    return  QString();
}

template<int N>
QString MifareAbstractNTAG<N>::editHint(int block,int byte)
{
    switch(block)
    {
        case 1:
            return QT_TRANSLATE_NOOP("MifareUltralightEEPROM",
                                     "UID and Manufacturer's Data. Read-only.");
        case 2:
        {
            if(byte>=2)
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
        case 3:
            return QT_TRANSLATE_NOOP("MifareUltralightEEPROM",
                                     "Capability Container is a one time programmable block, describing the TAG. Double click \"Edit\" to modify. Be advised: editing the CC block is strongly discouraged");
        case (N-5):
        {
            if(byte<3)
            {
                return QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM",
                                         "Lock bits 2. Double click \"Edit\" to manage write access.");
            }
            break;
        }
        case (N-4):
            return QT_TRANSLATE_NOOP("MifareNTAGEEPROM",
                                     "ASCII Mirror and password protected area address");
        case (N-3):
            return QT_TRANSLATE_NOOP("MifareNTAGEEPROM",
                                     "Access control");
        case (N-2):
            return QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM",
                                     "Password");
        case (N-1):
            return QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM",
                                     "Password Acknowledgement");
        default:
            break;
    }
    return  QString();
}

template<int N>
bool MifareAbstractNTAG<N>::isWriteProtectedInt(int block, int byte, BlockData sel) const
{
    if(block < 16)
    {
        return MifareUltralightAbstractEEPROM<N>::isWriteProtected0_15(block, byte, sel);
    }
    else if(block<(N-5))
    {
        return isWriteProtectedExtraPages(block, sel);
    }
    else if(block==(N-5))
    {
        //lock bytes 2 themselves
        return isWriteProtectedAuxLockBits(byte, sel);
    }
    /*blocks (N-4) & (N-3) are locked permanently if CFGLCK is set*/
    return ((block==(N-4))||(block==(N-3))) &&
            (MifareAbstractNTAG<N>::blocks.at(N-3).constData(sel)[0] & (1<<6));
}

template<int N>
bool MifareAbstractNTAG<N>::canWriteProtectExt(int block)
{
    return ((block==(N-4))||(block==(N-3)));
}

template<int N>
bool MifareAbstractNTAG<N>::writeProtectExt(int block)
{
    if((block==(N-4))||(block==(N-3)))
    {
        MifareAbstractNTAG<N>::blocks[N-3][0] |= (1<<6); /*CFGLCK*/
        return true;
    }
    return false;
}

template<int N>
bool MifareAbstractNTAG<N>::isEditableExt(int block, int byte, BlockData)
{
    //block >=16
    switch(block)
    {
        case (N-4):
            return (byte!=1);
        case (N-3):
            return (byte==0);
        case (N-2):
            return true;
        case (N-1):
            return (byte<2);
        default:break;
    }
    return true;
}

bool MifareNTAG213::isWriteProtectedExtraPages(int block, BlockData sel) const
{
    //lock bytes 2
    quint32 alock = auxLockBits(sel);
    /*
     * bit 0: 16-17
     * ...
     * bit 11: 38-39
     */
    return alock & (quint32(1)<<((block-16)>>1));
}

bool MifareNTAG213::isWriteProtectedAuxLockBits(int byte, BlockData sel) const
{
    quint32 alock = auxLockBits(sel);
    /*
     * bit 16: 16-19 - byte 0
     * bit 17: 20-23 - byte 0
     * bit 18: 24-27 - byte 0
     * bit 19: 28-31 - byte 0
     * bit 20: 32-35 - byte 1
     * bit 21: 36-39 - byte 1
     */
    return ((byte==0)&&(alock&0x0F0000UL))||
           ((byte==1)&&(alock&0x300000UL))||
           ((byte==3));
}

bool MifareNTAG215::isWriteProtectedExtraPages(int block, BlockData sel) const
{
    //lock bytes 2
    quint32 alock = auxLockBits(sel);
    /*
     * bit 0: 16-31
     * ...
     * bit 7: 128-129
     */
    return alock & (quint32(1)<<((block-16)>>4));
}

bool MifareNTAG215::isWriteProtectedAuxLockBits(int byte, BlockData sel) const
{
    quint32 alock = auxLockBits(sel);
    /*
     * bit 16: 16-47 - byte 0
     * bit 17: 48-79 - byte 0
     * bit 18: 80-111 - byte 0
     * bit 19: 112-129 - byte 0
     */
    return ((byte==0)&&(alock&0x000F0000UL))||
           ((byte==1)||(byte==3));
}

bool MifareNTAG216::isWriteProtectedExtraPages(int block, BlockData sel) const
{
    //lock bytes 2
    quint32 alock = auxLockBits(sel);
    /*
     * bit 0: 16-31
     * ...
     * bit 7: 128-143
     * bit 8: 144-159
     * ...
     * bit 13: 224-225
     */
    return alock & (quint32(1)<<((block-16)>>4));
}

bool MifareNTAG216::isWriteProtectedAuxLockBits(int byte, BlockData sel) const
{
    quint32 alock = auxLockBits(sel);
    /*
     * bit 16: 16-47 - byte 0
     * bit 17: 48-79 - byte 0
     * bit 18: 80-111 - byte 0
     * bit 19: 112-143 - byte 0
     * bit 20: 144-175 - byte 1
     * bit 21: 176-207 - byte 1
     * bit 22: 208-225 - byte 1
     */
    return ((byte==0)&&(alock&0x0F0000UL))||
           ((byte==1)&&(alock&0x700000UL))||
           ((byte==3));
}
