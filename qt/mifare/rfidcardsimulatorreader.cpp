#include "rfidcardsimulatorreader.h"
#include "mifaresector.h"

#include <endianrw.h>

#include <QRandomGenerator>


/*File header:
 * 0..3 0xFEBDAC17 (LE)
 * 4    SAK,
 * 5    UID size
 * 6    PICC Type
 * 7    0
 */

#define SIM_CARD_OFFSET (8)

inline quint8 mifareSak(MifareCards type)
{
    switch(type)
    {
        case MF_CLASSIC_1K:
        case MF_PLUS_S_2K_SL1:
        case MF_PLUS_X_2K_SL1:
            return 0x08;
        case MF_PLUS_S_4K_SL1:
        case MF_PLUS_X_4K_SL1:
        case MF_CLASSIC_4K:
            return 0x18;
        case MF_CLASSIC_Mini:       return 0x09;
        case MF_ULTRALIGHT:
        case MF_ULTRALIGHT_NANO:
        case MF_ULTRALIGHT_C:
        case MF_ULTRALIGHT_EV1_80:
        case MF_ULTRALIGHT_EV1_164:
        case MF_NTAG213:
        case MF_NTAG215:
        case MF_NTAG216:            return 0;
        case MF_PLUS_X_2K_SL2:      return 0x10;
        case MF_PLUS_X_4K_SL2:      return 0x11;
        case MF_DESFIRE:
        case MF_DESFIRE_C1:
        case MF_DESFIRE_C2:         return 0x20;
        case MF_CLASSIC_2K:         return 0x19;
        case MF_UNKNOWN:
        default:
            break;
    }
    return 0xFF;
}


RFIDCardSimulatorReader::RFIDCardSimulatorReader(QObject *parent) : RFIDCardReaderInterface(parent),
    file_size(0), mfc_sector(0)
{
    QTimer::singleShot(200, this, [=]{
        emit this->versionChanged(this->version());
        emit this->fullVersionChanged(this->version(), QVersionNumber(1, 4, REV_FirstGen));
    });
}

RFIDCardSimulatorReader::~RFIDCardSimulatorReader()
{
    card_file.close();
}

bool checkSize(quint64 expect, quint64 get, MifareCards type)
{
    if(get != (expect+SIM_CARD_OFFSET))
    {
        qWarning()<<"Invalid "<< mifareCardName(type) <<" card file: expected "<<expect
                 <<" bytes, got "<<(get-SIM_CARD_OFFSET);
        return false;
    }
    return true;
}

void RFIDCardSimulatorReader::open(const QString& cardfile)
{
    QString err;
    bool res = open_int(cardfile, &err);
    emit cardOpened(res, err);
}

bool RFIDCardSimulatorReader::open_int(const QString& cardfile, QString* error)
{
    close();
    card_file.open(cardfile.toLocal8Bit().constData(),
                   std::ios_base::in|std::ios_base::out|std::ios_base::binary);
    if(!card_file.is_open())
    {
        qWarning()<<"Cannot open card file "<<cardfile<<": access denied";
        *error = tr("Access denied");
        return false;
    }

    card_file.seekg(0,std::ios_base::end);
    file_size = card_file.tellg();
    card_file.seekg(0);

    if(file_size <= SIM_CARD_OFFSET)
    {
        //definitely invalid file
        qWarning()<<"Invalid card file "<<cardfile<<" - too small";
        *error = tr("Tag storage is corrupted");
        card_file.close();
        return false;
    }

    quint32 head = read_uint32_le(card_file);
    if(head != 0xFEBDAC17)
    {
        qWarning()<<"Not a card file: magic header "<<QString::number(head,16)<<" vs FEBDAC17";
        *error = tr("File is not a tag storage");
        card_file.close();
        return false;
    }
    quint8 csak = sak();
    //quint8 usize = uidSize();
    MifareCards card = getType();

    if(card == MF_UNKNOWN)
    {
        qWarning()<<"Unknown card type "<<int(csak)<<" in file "<<cardfile;
        *error = tr("Tag class is not supported");
        card_file.close();
        return false;
    }

    if(isUnsupportedCard(card))
    {
        qWarning()<<"Unimplemented card type "<<mifareCardName(card);
        *error = tr("Tag class is not supported");
        card_file.close();
        return false;
    }

    current_card_info.sak = csak;
    current_card_info.type = card;
    current_card_info.block_size = 16;

    bool chk_res = false;
    switch(card)
    {
        case MF_CLASSIC_1K:
            chk_res = checkSize(1024, file_size, card);
            current_card_info.blocks = 64;
            break;
        case MF_CLASSIC_2K:
        case MF_PLUS_S_2K_SL1:
        case MF_PLUS_X_2K_SL1:
            chk_res = checkSize(2048, file_size, card);
            current_card_info.blocks = 128;
            break;
        case MF_CLASSIC_4K:
        case MF_PLUS_S_4K_SL1:
        case MF_PLUS_X_4K_SL1:
            chk_res = checkSize(4096, file_size, card);
            current_card_info.blocks = 256;
            break;
        case MF_CLASSIC_Mini:
            chk_res = checkSize(320, file_size, card);
            current_card_info.blocks = 20;
            break;
        case MF_ULTRALIGHT:
            current_card_info.block_size = 4;
            current_card_info.blocks = 16;
            chk_res = checkSize(64, file_size, card);
            break;
        case MF_ULTRALIGHT_NANO:
            current_card_info.block_size = 4;
            current_card_info.blocks = 14;
            chk_res = checkSize(56, file_size, card);
            break;
        case MF_ULTRALIGHT_C:
            current_card_info.block_size = 4;
            current_card_info.blocks = 48;
            chk_res = checkSize(192 /*48*4*/, file_size, card);
            break;
        case MF_ULTRALIGHT_EV1_80:
            current_card_info.block_size = 4;
            current_card_info.blocks = 20;
            chk_res = checkSize(80 /*20*4*/, file_size, card);
            break;
        case MF_ULTRALIGHT_EV1_164:
            current_card_info.block_size = 4;
            current_card_info.blocks = 41;
            chk_res = checkSize(164 /*41*4*/, file_size, card);
            break;
        case MF_NTAG213:
            current_card_info.block_size = 4;
            current_card_info.blocks = 45;
            chk_res = checkSize(180 /*45*4*/, file_size, card);
            break;
        case MF_NTAG215:
            current_card_info.block_size = 4;
            current_card_info.blocks = 135;
            chk_res = checkSize(540 /*135*4*/, file_size, card);
            break;
        case MF_NTAG216:
            current_card_info.block_size = 4;
            current_card_info.blocks = 231;
            chk_res = checkSize(924 /*231*4*/, file_size, card);
            break;
        default:
            qWarning()<<"Unimplemented card type "<<mifareCardName(card);
            break;
    }

    if(!chk_res)
    {
        card_file.close();
        *error = tr("Tag class is not supported");
        return false;
    }

    if(isUltralightEV1Card(getType()))
    {
        int pack_block_num = 0;
        char pack_block[4];
        switch(getType())
        {
            case MF_ULTRALIGHT_EV1_80:
                pack_block_num = 19;
                break;
            case MF_ULTRALIGHT_EV1_164:
                pack_block_num = 40;
                break;
            case MF_NTAG213:
                pack_block_num = 44;
                break;
            case MF_NTAG215:
                pack_block_num = 134;
                break;
            case MF_NTAG216:
                pack_block_num = 230;
                break;
            default:
                break;
        }

        if(pack_block_num)
        {
            card_file.seekg(SIM_CARD_OFFSET + pack_block_num*4);
            card_file.read(pack_block, 2);
            ul_pack = read_uint16_le(pack_block);
        }
    }

    //ok, have valid card file;
    m_card_present = true;
    emit cardPresent(getUID(), getType());
    *error = tr("Success");
    return true;
}

void RFIDCardSimulatorReader::create(int type, const QString& output)
{
    QString err;
    bool res = newCard(type, output, 4, &err);
    emit cardOpened(res, err);
}

bool RFIDCardSimulatorReader::newCard(int type, const QString& cardfile, quint8 uid_size, QString* error)
{
    if(isUnsupportedCard(type))
    {
        qWarning()<<"Unimplemented card type "<<mifareCardName(type);
        *error = tr("Tag class is not supported");
        return false;
    }

    close();
    card_file.open(cardfile.toLocal8Bit().constData(),
                   std::ios_base::out|std::ios_base::binary);
    if(!card_file.is_open())
    {
        qWarning()<<"Cannot open card file "<<cardfile<<": access denied";
        *error = tr("Access denied");
        return false;
    }

    write_uint32_le(0xFEBDAC17UL,card_file);
    /*it's safe after isUnsupported... check*/
    write_uint8(mifareSak(static_cast<MifareCards>(type)), card_file);
    if(isClassicCard(type))
    {
        write_uint8((uid_size==7)?7:4,card_file);
    }
    else
    {
        uid_size = 7;
        write_uint8(7,card_file);
    }

    write_uint8(quint8(type), card_file);
    write_padding(1, card_file);
    //////head done//////////////////////

    QRandomGenerator randg = QRandomGenerator::securelySeeded();
    if(isClassicCard(type))
    {
        int normal_sectors = 16;
        switch(type)
        {
            case MF_CLASSIC_Mini:
                normal_sectors = 5;break;
            case MF_CLASSIC_2K:
            case MF_PLUS_S_2K_SL1:
            case MF_PLUS_X_2K_SL1:
            case MF_CLASSIC_4K:
            case MF_PLUS_S_4K_SL1:
            case MF_PLUS_X_4K_SL1:
                normal_sectors = 32;
                break;
            default:
                break;
        }

        //block 0 sector 0 - UID & MD
        quint8 uid = ((uid_size == 7) ? 0x04 : quint8(randg.bounded(0, 255))), bcc=0;
        for(int j=0; j < 4; ++j)
        {
            write_uint8(uid,card_file);
            bcc ^= uid;
            uid = quint8(randg.bounded(0, 255));
        }
        if(uid_size==4)
        {
            write_uint8(bcc, card_file);
        }
        else
        {
            for(int j=4;j<7;++j)
            {
                uid = quint8(randg.bounded(0, 255));
                write_uint8(uid,card_file);
            }
        }

        //sak
        write_uint8(mifareSak(static_cast<MifareCards>(type)), card_file);
        //atqa
        write_uint8( ((uid_size==7)?0x40:0)|((type==MF_CLASSIC_4K)?0x02:0x04), card_file);
        write_uint8(0x00, card_file);

        write_padding((uid_size==4)?8:6,card_file);

        quint8 zero_block[16];
        memset(zero_block,0x00,16);
        quint8 dummy_block[16];
        memset(dummy_block,0xFF,16);
        for(int j=0;j<2;++j)
        {
            write_arbitrary(zero_block,16,card_file);
        }
        //keyA
        write_arbitrary(dummy_block,6,card_file);
        //access bits
        write_uint32_be(0xFF0780FFUL,card_file);
        //keyB
        write_arbitrary(dummy_block,6,card_file);

        for(int i=1;i<normal_sectors;++i)
        {
            for(int j=0;j<3;++j)
            {
                write_arbitrary(zero_block,16,card_file);
            }
            //keyA
            write_arbitrary(dummy_block,6,card_file);
            //access bits
            write_uint32_be(0xFF0780FFUL,card_file);
            //keyB
            write_arbitrary(dummy_block,6,card_file);
        }

        if((type == MF_CLASSIC_4K)||(type==MF_PLUS_S_4K_SL1)||(type==MF_PLUS_X_4K_SL1))
        {
            for(int i=0;i<8;++i)
            {
                for(int j=0;j<15;++j)
                {
                    write_arbitrary(zero_block,16,card_file);
                }
                //keyA
                write_arbitrary(dummy_block,6,card_file);
                //access bits
                write_uint32_be(0xFF0780FF,card_file);
                //keyB
                write_arbitrary(dummy_block,6,card_file);
            }
        }
    }
    else if(isUltralightCard(type))
    {
        //ultralight
        quint8 uid;
        quint8 bcc = 0x88;

        uid = 0x04;
        bcc ^= uid;
        write_uint8(uid,card_file);

        for(int i=0;i<2;++i)
        {
            uid = quint8(randg.bounded(0, 255));
            bcc ^= uid;
            write_uint8(uid,card_file);
        }
        write_uint8(bcc,card_file);

        bcc = uid = quint8(randg.bounded(0, 255));
        write_uint8(uid,card_file);

        for(int i=0;i<3;++i)
        {
            uid = quint8(randg.bounded(0, 255));
            bcc ^= uid;
            write_uint8(uid,card_file);
        }
        write_uint8(bcc,card_file);

        write_padding(3,card_file); //internal + 2*lock;

        switch(type)
        {
            case MF_ULTRALIGHT:
                write_padding(4, card_file);                     /*OTP*/
                write_padding(12*4,card_file);
                break;
            case MF_ULTRALIGHT_NANO:
                write_padding(4, card_file);                     /*OTP*/
                write_padding(10*4,card_file);
                break;
            case MF_ULTRALIGHT_EV1_80:
                write_padding(4, card_file);                     /*OTP*/
                write_padding(12*4,card_file);
                write_arbitrary("\x00\x00\x00\xFF",4,card_file); /*MOD RFU RFU AUTH0*/
                write_arbitrary("\x00\x05\x00\x00",4,card_file); /*ACCESS VCTID RFU AUTHLIM*/
                write_uint32_be(0xFFFFFFFFUL,card_file);         /*PWD*/
                write_padding(4,card_file);                      /*PACK RFU RFU*/
                break;
            case MF_ULTRALIGHT_EV1_164:
                write_padding(4, card_file);                     /*OTP*/
                write_padding(32*4,card_file);
                write_padding(4, card_file);                     /*Lock bytes 2*/
                write_arbitrary("\x00\x00\x00\xFF",4,card_file); /*MOD RFU RFU AUTH0*/
                write_arbitrary("\x00\x05\x00\x00",4,card_file); /*ACCESS VCTID RFU AUTHLIM*/
                write_uint32_be(0xFFFFFFFFUL,card_file);         /*PWD*/
                write_padding(4,card_file);                      /*PACK RFU RFU*/
                break;
            case MF_ULTRALIGHT_C:
                write_padding(4, card_file);             /*OTP*/
                write_padding(36*4,card_file);
                write_padding(3,card_file);              //lock bytes 2&3;
                write_uint8(0xBD,card_file);
                write_padding(4,card_file);              //counter && rfu
                write_uint32_be(0x30000000UL,card_file); //AUTH0
                write_padding(4,card_file);              //AUTH1
                write_uint32_be(0x42524541UL,card_file); //KEY1
                write_uint32_be(0x4B4D4549UL,card_file); //KEY1
                write_uint32_be(0x46594F55UL,card_file); //KEY2
                write_uint32_be(0x43414E21UL,card_file); //KEY2
                break;
            case MF_NTAG213:
                write_arbitrary("\xE1\x10\x12\x00",4,card_file); /*Capability Container*/
                write_padding(36*4, card_file);
                write_padding(4, card_file);                     /*Dynamic Lock bytes*/
                write_arbitrary("\x02\x00\x00\xFF",4,card_file); /*MIRROR FDU MIRROR_PAGE AUTH0*/
                write_padding(4, card_file);                     /*Access*/
                write_uint32_be(0xFFFFFFFFUL,card_file);         /*PWD*/
                write_padding(4,card_file);                      /*PACK RFU RFU*/
                break;
            case MF_NTAG215:
                write_arbitrary("\xE1\x10\x12\x00",4,card_file); /*Capability Container*/
                write_padding(126*4, card_file);
                write_padding(4, card_file);                     /*Dynamic lock bytes*/
                write_arbitrary("\x02\x00\x00\xFF",4,card_file); /*MIRROR FDU MIRROR_PAGE AUTH0*/
                write_padding(4, card_file);                     /*Access*/
                write_uint32_be(0xFFFFFFFFUL,card_file);         /*PWD*/
                write_padding(4,card_file);                      /*PACK RFU RFU*/
                break;
            case MF_NTAG216:
                write_arbitrary("\xE1\x10\x12\x00",4,card_file); /*Capability Container*/
                write_padding(222*4, card_file);
                write_padding(4, card_file);                     /*Dynamic lock bytes*/
                write_arbitrary("\x02\x00\x00\xFF",4,card_file); /*MIRROR FDU MIRROR_PAGE AUTH0*/
                write_padding(4, card_file);                     /*Access*/
                write_uint32_be(0xFFFFFFFFUL,card_file);         /*PWD*/
                write_padding(4,card_file);                      /*PACK RFU RFU*/
                break;
            default:
                break;
        }
    }

    card_file.flush();
    card_file.close();

    QString chk_err;
    if(open_int(cardfile, &chk_err))
    {
        m_card_present = true;
        emit cardPresent(getUID(), getType());
        return true;
    }

    return false;
}

void RFIDCardSimulatorReader::close()
{
    card_file.close();
    RFIDCardReaderInterface::close();
}

quint8 RFIDCardSimulatorReader::sak() const
{
    if(!card_file.is_open())
    {
        return 0xFF;
    }
    card_file.seekg(4);
    return static_cast<quint8>(card_file.get());
}

quint8 RFIDCardSimulatorReader::uidSize() const
{
    if(!card_file.is_open())
    {
        return 0;
    }
    card_file.seekg(5);
    return static_cast<quint8>(card_file.get());
}

QByteArray RFIDCardSimulatorReader::getUID()
{
    if(!m_card_present)
        return QByteArray();

    QByteArray ret;
    switch(uidSize())
    {
        case 4:
        {
            card_file.seekg(SIM_CARD_OFFSET);
            ret = QByteArray(4,char(0));
            card_file.read(ret.data(),4);
            break;
        }
        case 7:
        {
            if(isClassicCard(getType()))
            {
                card_file.seekg(SIM_CARD_OFFSET);
                ret = QByteArray(7, char(0));
                card_file.read(ret.data(),7);
            }
            else if(isUltralightCard(getType()))
            {
                card_file.seekg(SIM_CARD_OFFSET);
                ret = QByteArray(9,char(0));
                card_file.read(ret.data(),9);
                ret.remove(7,1);
                ret.remove(3,1);
            }
            break;
        }
        case 10:
        {
#if 0
            ret = QByteArray(12,char(0));
            card_file.read(ret.data(),11);
            ret.remove(11,1);
            ret.remove(7,1);
            ret.remove(3,1);
#endif
            break;
        }
        default:break;
    }

    return ret;
}

void RFIDCardSimulatorReader::setUID(const QByteArray& uid)
{
    if(!card_file.is_open() || (uid.size() != getUID().size()))
    {
        emit uidChanged(false);
        return;
    }

    if(isClassicCard(getType()))
    {
        card_file.seekp(SIM_CARD_OFFSET);
        card_file.write(uid.constData(),uid.size());
        if(uid.size()==4)
        {
            //bcc
            uint8_t bcc = 0;
            const uint8_t* uuid_data = (const uint8_t*)uid.constData();
            for(int i=0;i<uid.size();++i)
            {
                bcc ^= *uuid_data++;
            }
            card_file.write((const char*)&bcc,1);
        }
    }
    else if(isUltralightCard(getType()))
    {
        card_file.seekp(SIM_CARD_OFFSET);
        uint8_t bcc = 0x88;
        const uint8_t* uuid_data = (const uint8_t*)uid.constData();
        for(int i=0;i<3;++i)
        {
            bcc ^= uuid_data[i];
        }
        card_file.write((const char*)uuid_data, 3);
        card_file.write((const char*)&bcc, 1);
        bcc = 0;
        for(int i=3;i<7;++i)
        {
            bcc ^= uuid_data[i];
        }
        card_file.write((const char*)(uuid_data+3), 4);
        card_file.write((const char*)&bcc, 1);
    }
    else
    {
        qWarning()<<"No procedure to change UID of "<<getType();
        emit uidChanged(false);
    }

    removeCard();
    emit uidChanged(true);
}

void RFIDCardSimulatorReader::writeEM(const QByteArray& uid, const QByteArray& key, const QByteArray& pwd, int coding, int bits)
{
    emit error("Not supported");
}

MifareCards RFIDCardSimulatorReader::getType() const
{
    if(!card_file.is_open())
    {
        return MF_UNKNOWN;
    }

    card_file.seekg(6);
    quint8 type = static_cast<quint8>(card_file.get());
    if(isUnsupportedCard(type))
    {
        return MF_UNKNOWN;
    }
    return (MifareCards)type;
}

MifareClassicBlock RFIDCardSimulatorReader::currentClassicSectorTrailer()
{
    MifareClassicBlock trailer_block;
    if(mfc_sector>=MifareClassicFirstJumboSector)
    {
        //jumbo
        card_file.seekg(SIM_CARD_OFFSET + 16*(MifareClassicFunctions::classicBlockAddress(mfc_sector)+15));
    }
    else
    {
        //classic-normal
        card_file.seekg(SIM_CARD_OFFSET + 16*(MifareClassicFunctions::classicBlockAddress(mfc_sector)+3));
    }
    read_arbitrary(&trailer_block[0],16,card_file);
    trailer_block.written();
    qWarning()<<"Current classic sector trailer: "<<trailer_block;
    return trailer_block;
}

MifareClassicKey RFIDCardSimulatorReader::currentClassicKey()
{
    return (mfc_key_type==MifareClassicKeyA) ? MifareClassicKey(currentClassicSectorTrailer().raw()) :
                                               MifareClassicKey(currentClassicSectorTrailer().raw()+10);
}

MifareClassicBlockAccess RFIDCardSimulatorReader::currentBlockAccess(int block)
{
    if(mfc_sector>=MifareClassicFirstJumboSector)
    {
        MifareClassicJumboSector jsector;
        jsector.setBlock(15,currentClassicSectorTrailer());
        return jsector.blockAccessPermissions(block, BD_Saved);
    }

    MifareClassicSector jsector;
    jsector.setBlock(3,currentClassicSectorTrailer());

    qWarning()<<"currentBlockAccess("<<block<<") bits "<<jsector.blockAccessBits(block, BD_Saved);
    return jsector.blockAccessPermissions(block, BD_Saved);
}

MifareClassicTrailerAccess RFIDCardSimulatorReader::currentTrailerAccess()
{
    if(mfc_sector>=MifareClassicFirstJumboSector)
    {
        MifareClassicJumboSector jsector;
        jsector.setBlock(15,currentClassicSectorTrailer());
        return jsector.trailerAccessPermissions(BD_Saved);
    }

    MifareClassicSector jsector;
    jsector.setBlock(3,currentClassicSectorTrailer());
    return jsector.trailerAccessPermissions(BD_Saved);
}

void RFIDCardSimulatorReader::setPassword(quint32 pwd)
{
    qWarning()<<"Simulator password set to "<<QString("0x%1").arg(pwd, 8, 16, QLatin1Char('0')).toUpper();
    ul_pwd = pwd;
    emit passwordChanged(true, ul_pwd);
}

void RFIDCardSimulatorReader::setKey(MifareClassicKeyType type, const MifareClassicKey& key)
{
    mfc_key_type = type;
    mfc_key = key;
    qWarning()<<"Simulator classic key set to "<<mfc_key<<", type "<<mfc_key_type;
    emit keyChanged(true);
}

void RFIDCardSimulatorReader::setPlusKey(MifareClassicKeyType type, const MifarePlusKey& key)
{
    mfp_aes_key = key;
    mfp_aes_type = type;
    qWarning()<<"Simulator plus ket set to "<<mfp_aes_key<<", type "<<mfp_aes_type;
    emit plusKeyChanged(true);
}

#define INVOKE_READFAIL(result) \
    QMetaObject::invokeMethod(this, "blockRead", Qt::QueuedConnection, Q_ARG(int, blockAddress), Q_ARG(int, 0), Q_ARG(QByteArray, QByteArray()), Q_ARG(int, result))
#define INVOKE_WRITEFAIL(result) \
    QMetaObject::invokeMethod(this, "blockWritten", Qt::QueuedConnection, Q_ARG(int, blockAddress), Q_ARG(int, result))

void RFIDCardSimulatorReader::readPack()
{
    if(haveCard() && isUltralightEV1Card(getType()))
    {
        this->card_file.seekg(-8, std::ios_base::end);
        if(ul_pwd == read_uint32_le(this->card_file))
        {
            emit this->packReceived(true, read_uint16_le(this->card_file));
        }
        else
        {
            emit this->packReceived(false, 0x0000);
        }
    }
    else
    {
        emit packReceived(false, ul_pack);
    }
}

void RFIDCardSimulatorReader::readBlock(quint16 blockAddress, int blockCount)
{
    qWarning()<<"Simreader: requested read "<<blockAddress<<" to "<<(blockAddress+blockCount-1);
    if(!m_card_present)
    {
        INVOKE_READFAIL(RW_Fatal);
        return;
    }

    if(block_count)
    {
        qWarning()<<"Already reading block(s): left "<<block_count;
        INVOKE_READFAIL(RW_Retry);
        return;
    }

    qWarning()<<"Requested read of blocks ["<<blockAddress<<" - "<<(blockAddress+blockCount-1)<<"]";
    block_address = blockAddress;
    block_count   = blockCount;

    QMetaObject::invokeMethod(this,"read_finished",Qt::QueuedConnection);
}

void RFIDCardSimulatorReader::writeBlock(quint16 blockAddress, const QByteArray& blk)
{
    const quint8 *data = (const quint8*)blk.constData();
    int blockSize = blk.size();


    if(!m_card_present)
    {
        INVOKE_WRITEFAIL(RW_Fatal);
        return;
    }

    if(quint64(blockAddress*blockSize+blockSize+SIM_CARD_OFFSET) > file_size)
    {
        INVOKE_WRITEFAIL(RW_Invarg);
        return;
    }

    if(isClassicCard(getType()))

    {
        int blockNumber = (blockAddress - MifareClassicFunctions::classicBlockAddress(mfc_sector));
        if((blockAddress < MifareClassicFunctions::classicBlockAddress(mfc_sector)) ||
           (blockAddress >= (MifareClassicFunctions::classicBlockAddress(mfc_sector) + MifareClassicFunctions::classicBlocksCount(mfc_sector))))
        {
            qWarning()<<"Requested block write from wrong sector - requested block "<<blockAddress
                     <<" from range ["<<MifareClassicFunctions::classicBlockAddress(mfc_sector)<<";"
                    <<(MifareClassicFunctions::classicBlockAddress(mfc_sector)+MifareClassicFunctions::classicBlocksCount(mfc_sector))<<"]";
            INVOKE_WRITEFAIL(RW_Invarg);
            return;
        }

        bool success = true;
        if( ((mfc_sector>=MifareClassicFirstJumboSector)&&(blockNumber==15))||
            ((mfc_sector<MifareClassicFirstJumboSector)&&(blockNumber==3))
            )
        {
            //need to check trailerAccess, not blockAccess
            MifareClassicTrailerAccess cperm = currentTrailerAccess();
            if(!(cperm.bits_write & currentClassicPermission()))
            {
                success = false;
                qWarning()<<"Cannot write trailer: bits are locked";
            }
            else if(!(cperm.keyA_write & currentClassicPermission()))
            {
                success = false;
                qWarning()<<"Cannot write trailer: keyA locked";
            }
            else if(!(cperm.keyB_write & currentClassicPermission()))
            {
                success = false;
                qWarning()<<"Cannot write trailed: keyB locked";
            }
        }
        else if (!(currentBlockAccess(blockNumber).block_write & currentClassicPermission()))
        {
            success = false;
            qWarning()<<"Cannot write block: access denied - needed premissions: "
                     <<currentBlockAccess(blockNumber).block_write<<", have "<<currentClassicPermission();
        }

        if(!success)
        {
            qWarning()<<"Cannot write block - access denied";
            INVOKE_WRITEFAIL(RW_Denied);
            return;
        }
    }

    block_address = blockAddress;
    card_file.seekp(SIM_CARD_OFFSET + blockAddress*blockSize);
    card_file.write((const char*)data,blockSize);
    QMetaObject::invokeMethod(this,"write_finished",Qt::QueuedConnection);
}

bool RFIDCardSimulatorReader::plusAuthKey(quint16 keyBrn)
{
    /*Not supported for now*/
    Q_UNUSED(keyBrn);
    return false;
}

void RFIDCardSimulatorReader::read_finished()
{
    if(!block_count)
    {
        emit blockRead(block_address, 0, QByteArray(), RW_Invarg);
        return;
    }

    int block_size = 0;
    if(isClassicCard(getType()))
    {
        //process special blocks/bytes here
        mfc_sector = MifareClassicFunctions::sectorNumber(block_address);
        int blockNumber = (block_address - MifareClassicFunctions::classicBlockAddress(mfc_sector));

        if((blockNumber+block_count) > MifareClassicFunctions::classicBlocksCount(mfc_sector))
        {
            qWarning()<<"Requested block read from wrong sector";
            block_count = 0;
            emit blockRead(block_address, 0, QByteArray(), RW_Invarg);
            return;
        }

        if(!(currentClassicKey() == mfc_key))
        {
            qWarning()<<"Simulator tried read block "<<blockNumber<<" with wrong key: "<<mfc_key<<" vs "<<currentClassicKey();
            block_count = 0;
            emit blockRead(block_address, 0, QByteArray(), RW_Denied);
            return;
        }

        block_size = 16;
    }
    else if(isUltralightCard(getType()))
    {
        block_size = 4;
        //process special blocks/bytes here
    }

    if(!block_size)
    {
        qWarning()<<"Block size for card "<<mifareCardName(getType())<<" is unknown";
        block_count = 0;
        emit blockRead(block_address, 0, QByteArray(), RW_Invarg);
        return;
    }

    QByteArray block_data(block_size,char(0));
    card_file.seekg(SIM_CARD_OFFSET + block_address*block_size);
    card_file.read(block_data.data(),block_size);

    if(isClassicCard(getType()))
    {
        //check - if insufficient permissions -> reset to default values
    }

    --block_count;
    qWarning()<<"Simulator: blocks left: "<<block_count;

    emit blockRead(block_address, block_count, block_data, RW_OK);

    if(block_count)
    {
        ++block_address;
        QMetaObject::invokeMethod(this,"read_finished",Qt::QueuedConnection);
    }
}

void RFIDCardSimulatorReader::write_finished()
{
    emit blockWritten(block_address,RW_OK);
}
