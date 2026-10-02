#include "mifaresector.h"

const QStringList MifareUltralightAbstractEEPROMInterface::staticLockBits_0_15_Labels =
        QStringList()<<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM","OTP Bit")
                     <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM","9-4 Bits")
                     <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM","15-10 Bits")
                     <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM","OTP")
                     <<"4"<<"5"<<"6"<<"7"<<"8"<<"9"<<"10"<<"11"<<"12"<<"13"<<"14"<<"15";


const QStringList MifareUltralightEV1_164_EEPROM::auxLockBits_16_35_Labels =
        QStringList()<<"16-17"<<"18-19"<<"20-21"<<"22-23"<<"24-25"<<"26-27"
                     <<"28-29"<<"30-31"<<"32-33"<<"34-35"
                     <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "RFU")
                     <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "RFU")
                     <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "RFU")
                     <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "RFU")
                     <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "RFU")
                     <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "RFU")
                     <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "16-19 Bits")
                     <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "20-23 Bits")
                     <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "24-27 Bits")
                     <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "28-31 Bits")
                     <<QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM", "32-35 Bits");

void MifareUltralightAbstractEEPROM_EV1Interface::setLastValidBlock(int block)
{
    _last_valid_block = block;
}

bool MifareUltralightEV1_80_EEPROM::forceBinDisplay(int block, int byte)
{
    return (((block==2)&&(byte>=2))||(block==3));
}

bool MifareUltralightEV1_80_EEPROM::customEdit(int block, int byte)
{
    switch (block)
    {
        case 2:
            return (byte>=2); /*Lock*/
        case 3:
            return true; /*OTP*/
        case 16:
            return (byte==0)||(byte==3);
        case 17:
        case 19:
            return (byte<2); /*ACCESS/VCTID | PACK*/
        case 18:
            return true; /*PWD*/
        default:break;
    }
    return false;
}

QString MifareUltralightEV1_80_EEPROM::customEditName(int block)
{
    switch(block)
    {
        case 2:
            return QT_TRANSLATE_NOOP("MifareUltralightEEPROM","Lock Bits");
        case 3:
            return QT_TRANSLATE_NOOP("MifareUltralightEEPROM","OTP");
        case 16:
            return QT_TRANSLATE_NOOP("MifareUltralightEV1_80_EEPROM","MOD & AUTH0");
        case 17:
            return QT_TRANSLATE_NOOP("MifareUltralightEV1_80_EEPROM","ACCESS & VCTID");
        case 18:
            return QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM","PWD");
        case 19:
            return QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM","PACK");
        default:
            break;
    }
    return  QString();
}

QString MifareUltralightEV1_80_EEPROM::editHint(int block,int byte)
{
    switch(block)
    {
        case 0:
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
                                     "OTP - one time programmable block. Double click \"Edit\" to modify.");
        case 16:
            return QT_TRANSLATE_NOOP("MifareUltralightEV1_80_EEPROM",
                                     "Modulation mode and password protected area address");
        case 17:
            return QT_TRANSLATE_NOOP("MifareUltralightEV1_80_EEPROM",
                                     "Access control and Virtual Card Type Identifier");
        case 18:
            return QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM",
                                     "Password");
        case 19:
            return QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM",
                                     "Password Acknowledgement");
        default:
            break;
    }
    return  QString();
}

/* For blocks >=16 */
bool MifareUltralightEV1_80_EEPROM::isWriteProtectedInt(int block, int byte, BlockData sel) const
{
    if(block < 16)
    {
        return isWriteProtected0_15(block, byte, sel);
    }

    /*blocks 16 & 17 are locked permanently if CFGLCK is set*/
    return ((block==16)||(block==17)) && (blocks.at(17).constData(sel)[0] & (1<<6));
}

bool MifareUltralightEV1_80_EEPROM::canWriteProtectExt(int block)
{
    return ((block==16)||(block==17));
}

bool MifareUltralightEV1_80_EEPROM::writeProtectExt(int block)
{
    if((block==16)||(block==17))
    {
        blocks[17][0] |= (1<<6); /*CFGLCK*/
        return true;
    }
    return false;
}

bool MifareUltralightEV1_80_EEPROM::isEditableExt(int block, int byte, BlockData)
{
    //block >=16
    switch(block)
    {
        case 16:
            return (byte==0)||(byte==3);
        case 17:
        case 19:
            return (byte<2);
        case 18:
            return true;
        default:break;
    }
    return false;
}

/*****************************************************/

bool MifareUltralightEV1_164_EEPROM::forceBinDisplay(int block, int byte)
{
    return (((block==2)&&(byte>=2))||(block==3)||((block==36)&&(byte<3)));
}

bool MifareUltralightEV1_164_EEPROM::customEdit(int block, int byte)
{
    switch (block)
    {
        case 2:
            return (byte>=2); /*Lock*/
        case 3:
            return true; /*OTP*/
        case 36:
            return (byte<3); /*Lock Aux*/
        case 37:
            return (byte==0)||(byte==3);
        case 38:
        case 40:
            return (byte<2); /*ACCESS/VCTID | PACK*/
        case 39:
            return true; /*PWD*/
        default:break;
    }
    return false;
}

QString MifareUltralightEV1_164_EEPROM::customEditName(int block)
{
    switch(block)
    {
        case 2:
            return QT_TRANSLATE_NOOP("MifareUltralightEEPROM","Lock Bits");
        case 3:
            return QT_TRANSLATE_NOOP("MifareUltralightEEPROM","OTP");
        case 36:
            return QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM","Lock Bits 2");
        case 37:
            return QT_TRANSLATE_NOOP("MifareUltralightEV1_80_EEPROM","MOD & AUTH0");
        case 38:
            return QT_TRANSLATE_NOOP("MifareUltralightEV1_80_EEPROM","ACCESS & VCTID");
        case 39:
            return QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM","PWD");
        case 40:
            return QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM","PACK");
        default:
            break;
    }
    return  QString();
}

QString MifareUltralightEV1_164_EEPROM::editHint(int block,int byte)
{
    switch(block)
    {
        case 0:
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
                                     "OTP - one time programmable block. Double click \"Edit\" to modify.");
        case 36:
        {
            if(byte<3)
            {
                return QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM",
                                         "Lock bits 2. Double click \"Edit\" to manage write access.");
            }
            break;
        }
        case 37:
            return QT_TRANSLATE_NOOP("MifareUltralightEV1_80_EEPROM",
                                     "Modulation mode and password protected area address");
        case 38:
            return QT_TRANSLATE_NOOP("MifareUltralightEV1_80_EEPROM",
                                     "Access control and Virtual Card Type Identifier");
        case 39:
            return QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM",
                                     "Password");
        case 40:
            return QT_TRANSLATE_NOOP("MifareUltralightAbstractEEPROM",
                                     "Password Acknowledgement");
        default:
            break;
    }
    return  QString();
}

bool MifareUltralightEV1_164_EEPROM::isWriteProtectedInt(int block, int byte, BlockData sel) const
{
    if(block < 16)
    {
        return isWriteProtected0_15(block, byte, sel);
    }
    else if(block<36)
    {
        //lock bytes 2
        quint32 alock = auxLockBits(sel);
        /*
         * bit 0: 16-17
         * ...
         * bit 9: 34-35
         */
        return alock & (quint32(1)<<((block-16)>>1));
    }
    else if(block==36)
    {
        //lock bytes 2 themselves
        quint32 alock = auxLockBits(sel);
        /*
         * bit 16: 16-19 - byte 0
         * bit 17: 20-23 - byte 0
         * bit 18: 24-27 - byte 0
         * bit 19: 28-31 - byte 0
         * bit 20: 32-35 - byte 1
         */
        return ((byte==0)&&(alock&0xF0000UL))||
                ((byte==1)&&(alock&0x100000UL))||
                ((byte==3));
    }
    /*blocks 37 & 38 are locked permanently if CFGLCK is set*/
    return ((block==37)||(block==38)) && (blocks.at(38).constData(sel)[0] & (1<<6));
}

bool MifareUltralightEV1_164_EEPROM::canWriteProtectExt(int block)
{
    return ((block==37)||(block==38));
}

bool MifareUltralightEV1_164_EEPROM::writeProtectExt(int block)
{
    if((block==37)||(block==38))
    {
        blocks[38][0] |= (1<<6); /*CFGLCK*/
        return true;
    }
    return false;
}

bool MifareUltralightEV1_164_EEPROM::isEditableExt(int block, int byte, BlockData)
{
    //block >=16
    switch(block)
    {
        case 36:
            return (byte<3);
        case 37:
            return (byte==0)||(byte==3);
        case 38:
        case 40:
            return (byte<2);
        case 39:
            return true;
        default:break;
    }
    /*user pages are editable*/
    return true;
}
