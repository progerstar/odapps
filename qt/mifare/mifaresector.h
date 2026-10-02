#ifndef MIFARESECTOR_H
#define MIFARESECTOR_H

#include "mifareblock.h"
#include <QVector>
#include <QHash>

#include <endianrw.h>

class MifareSectorInterface
{
    public:
        enum Types
        {
            ST_MiClassic,
            ST_MiClassicJumbo,
            ST_MiUlEEPROM,
            ST_MiUlCEEPROM,
            ST_MiULExtEEPROM,
            ST_MiNTAGEEPROM,
            ST_EmMARINE,
            ST_USER
        };

        MifareSectorInterface() : sector_start_address(0){}
        virtual ~MifareSectorInterface(){}

        virtual int type() const = 0;

        virtual int blockCount() const  = 0;
        virtual int size() const = 0;
        virtual int blockSize() const = 0;

        /*in 'blocks'*/
        quint8 startAddress() const { return sector_start_address; }

        void setStartAddress(quint8 addr)
        {
            sector_start_address = addr;
        }

        virtual bool isReadProtected(int block, int byte, BlockData sel =  BD_Current) = 0;
        virtual bool isDeadLocked(int block, BlockData sel =  BD_Current) = 0;
        virtual bool isWriteProtected(int block, int byte, BlockData sel =  BD_Current) = 0;

        virtual bool canReadProtect(int block) = 0;
        virtual bool canWriteProtect(int block) = 0;

        virtual bool readProtect(int block) = 0;
        virtual bool writeProtect(int block) = 0;

        virtual bool forceBinDisplay(int /*block*/, int /*byte*/)
        {
            return false;
        }

        virtual bool isEditable(int block, int byte, BlockData sel =  BD_Current) = 0;

        virtual bool customEdit(int block, int byte) = 0;
        virtual QString editHint(int block,int byte) = 0;

        inline virtual bool customEdit(int block)
        {
            for(int i=0;i<blockSize();++i)
            {
                if(customEdit(block,i))
                    return true;
            }
            return false;
        }

        virtual QString customEditName(int block) = 0;
        virtual QString blockTooltip(int block, BlockData sel =  BD_Current) const {
            Q_UNUSED(block)
            Q_UNUSED(sel)
            return QString();
        }

        virtual quint8& byte(int block, int byte) = 0;
        virtual quint8 value(int block, int byte) const = 0;
        virtual const quint8* raw(int block) const = 0;
        virtual const quint8* saved(int block) const = 0;
        virtual void write(int block, const quint8* raw) = 0;
        virtual bool modified(int block) const = 0;
        inline virtual bool modified() const
        {
            for(int i=0;i<blockCount();++i)
            {
                if(modified(i))
                    return true;
            }
            return false;
        }

        virtual void written(int block) = 0;

    protected:
        quint8 sector_start_address;
    private:
        Q_DISABLE_COPY(MifareSectorInterface)
};

template<int N, int S>
class MifareSector : public MifareSectorInterface
{
    public:
        MifareSector() : MifareSectorInterface()
        {
            blocks.resize(S);
        }
        virtual ~MifareSector(){}

        virtual inline int blockCount() const Q_DECL_OVERRIDE { return S;}
        virtual inline int size() const Q_DECL_OVERRIDE { return S*N;}
        virtual inline int blockSize() const Q_DECL_OVERRIDE
        {
            return N;
            //return blockCount() ? blocks.at(0).size() : 0;
        }

        virtual inline void setBlock(int num, const MifareBlock<N>& block)
        {
            blocks[num] = block;
        }

        MifareBlock<N>& operator[](int i)
        {
            return blocks[i];
        }

        inline const MifareBlock<N>& at(int i) const
        {
            return blocks.at(i);
        }

        inline virtual quint8& byte(int block, int byte) Q_DECL_OVERRIDE
        {
            return blocks[block][byte];
        }

        inline virtual quint8 value(int block, int byte) const Q_DECL_OVERRIDE
        {
            return blocks.at(block).at(byte);
        }

        inline virtual const quint8* raw(int block) const Q_DECL_OVERRIDE
        {
            return blocks.at(block).raw();
        }

        inline virtual const quint8* saved(int block) const Q_DECL_OVERRIDE
        {
            return blocks.at(block).saved();
        }

        inline virtual void write(int block, const quint8* raw) Q_DECL_OVERRIDE
        {
            if(raw != blocks.at(block).raw())
            {
                blocks[block].fromRaw(raw);
            }
        }

        inline virtual bool modified(int block) const Q_DECL_OVERRIDE
        {
            return blocks.at(block).modified();
        }

        inline virtual void written(int block) Q_DECL_OVERRIDE
        {
            blocks[block].written();
        }

    protected:
        QVector<MifareBlock<N> > blocks;
    private:
        Q_DISABLE_COPY(MifareSector)
};

class MifareClassicFunctions
{
    public:
        static const QHash<quint8,MifareClassicTrailerAccess> trailerAccessHash;
        static const QHash<quint8,MifareClassicBlockAccess> blockAccessHash;

        static int classicBlockAddress(int sector);
        static int classicBlocksCount(int sector);
        static int sectorNumber(int blockAddress);
};

class MifareClassicSectorInterface
{
    public:
        enum SecurityLevel {
            SL0 = 0,
            SL1 = 1,
            SL2 = 2,
            SL3 = 3
        };

        MifareClassicSectorInterface() {}
        virtual ~MifareClassicSectorInterface(){}

        virtual MifareClassicKeyType keyType() const = 0;
        virtual void setKeyType(MifareClassicKeyType used_key) = 0;

        virtual MifareClassicSectorInterface::SecurityLevel securityLevel() const = 0;
        virtual void setSecurityLevel(MifareClassicSectorInterface::SecurityLevel lev) = 0;

        virtual quint8 trailerAccessBits(BlockData sel =  BD_Current) const = 0;
        virtual quint8 blockAccessBits(int block, BlockData sel =  BD_Current) const = 0;

        virtual bool bitsValid(BlockData sel =  BD_Current) const = 0;

        /*always saved*/
        virtual bool canWriteKey(MifareClassicKeyType type) const = 0;
        virtual bool canWriteTrailerBits() const = 0;
        virtual bool canWriteBlock(int block) const = 0;

        virtual MifareClassicTrailerAccess trailerAccessPermissions(BlockData sel =  BD_Current) const = 0;
        virtual MifareClassicBlockAccess blockAccessPermissions(int block, BlockData sel =  BD_Current) const = 0;

        virtual MifareClassicPermission requiredKeyPermission() const = 0;

        virtual void setTrailerAccess(quint8 bits) = 0;
        virtual void setBlockAccess(int block, quint8 bits) = 0;
        virtual void setValueBlock(int block, qint32 value, quint8 address) = 0;

        virtual MifareClassicKey key(MifareClassicKeyType type, BlockData sel =  BD_Current) const = 0;
        virtual void writeKey(MifareClassicKeyType type, const MifareClassicKey& key) = 0;
};

template<int N>
class MifareClassicAbstractSector : public MifareSector<16,N>, public MifareClassicSectorInterface
{
    public:

        MifareClassicAbstractSector() : MifareSector<16,N>(), MifareClassicSectorInterface(),
            key_type(MifareClassicKeyA), sec_level(MifareClassicSectorInterface::SL1)
        {
        }

        virtual ~MifareClassicAbstractSector(){}

        inline virtual MifareClassicKeyType keyType() const Q_DECL_OVERRIDE
        {
            return key_type;
        }

        inline virtual void setKeyType(MifareClassicKeyType used_key) Q_DECL_OVERRIDE
        {
            key_type = used_key;
        }

        inline virtual MifareClassicSectorInterface::SecurityLevel securityLevel() const Q_DECL_OVERRIDE
        {
            return sec_level;
        }

        inline virtual void setSecurityLevel(MifareClassicSectorInterface::SecurityLevel lev) Q_DECL_OVERRIDE
        {
            sec_level = lev;
        }

        virtual bool bitsValid(BlockData sel =  BD_Current) const Q_DECL_OVERRIDE;

        virtual bool isReadProtected(int block, int byte, BlockData sel =  BD_Current) Q_DECL_OVERRIDE;
        virtual bool isDeadLocked(int block, BlockData sel =  BD_Current) Q_DECL_OVERRIDE;
        virtual bool isWriteProtected(int block, int byte, BlockData sel =  BD_Current) Q_DECL_OVERRIDE;

        inline virtual bool canReadProtect(int block) Q_DECL_OVERRIDE
        {
            if(!MifareSectorInterface::sector_start_address && !block)
            {
                //UID & MD
                return false;
            }
            return true;
        }

        inline virtual bool canWriteProtect(int block) Q_DECL_OVERRIDE
        {
            if(!MifareSectorInterface::sector_start_address && !block)
            {
                //UID & MD
                return false;
            }
            return true;
        }

        inline virtual bool readProtect(int /*block*/) Q_DECL_OVERRIDE
        {
            //use special widget for it!
            return false;
        }

        inline virtual bool writeProtect(int /*block*/) Q_DECL_OVERRIDE
        {
            //use special widget for it!
            return false;
        }

        inline virtual bool isEditable(int block, int /*byte*/, BlockData sel =  BD_Current) Q_DECL_OVERRIDE
        {
            Q_UNUSED(sel)
            if(!MifareSectorInterface::sector_start_address && !block)
            {
                //UID & MD
                return false;
            }
            return true;
        }

        inline virtual bool customEdit(int block, int byte) Q_DECL_OVERRIDE
        {
            //trailer only, byte 9 is for USER DATA
            return ((block==(N-1)) && (byte != 9) && ((byte<10) || (sec_level != SL3)));
        }

        virtual QString customEditName(int block) Q_DECL_OVERRIDE;
        virtual QString blockTooltip(int block, BlockData sel =  BD_Current) const Q_DECL_OVERRIDE;
        virtual QString editHint(int block,int byte) Q_DECL_OVERRIDE;

        /*
         * returns a byte where:
         *  bit0 == C13
         *  bit1 == C23
         *  bit2 == C33
         */
        inline virtual quint8 trailerAccessBits(BlockData sel =  BD_Current) const Q_DECL_OVERRIDE
        {
            return blockAccessBits(N-1, sel);
        }

        inline virtual MifareClassicTrailerAccess trailerAccessPermissions(BlockData sel =  BD_Current) const Q_DECL_OVERRIDE
        {
            return MifareClassicFunctions::trailerAccessHash.value(trailerAccessBits(sel));
        }
        inline virtual MifareClassicBlockAccess blockAccessPermissions(int block, BlockData sel =  BD_Current) const Q_DECL_OVERRIDE
        {
            return MifareClassicFunctions::blockAccessHash.value(blockAccessBits(block, sel));
        }

        inline virtual MifareClassicPermission requiredKeyPermission() const Q_DECL_OVERRIDE
        {
            return (key_type==MifareClassicKeyA) ? MC_Permissions_KeyA : MC_Permissions_KeyB;
        }

        inline virtual bool canWriteKey(MifareClassicKeyType type) const Q_DECL_OVERRIDE
        {
            return (sec_level != SL3) &&
                   ((type==MifareClassicKeyA) ? (trailerAccessPermissions(BD_Saved).keyA_write & requiredKeyPermission()) :
                                              (trailerAccessPermissions(BD_Saved).keyB_write & requiredKeyPermission()));
        }

        inline virtual bool canWriteTrailerBits() const Q_DECL_OVERRIDE
        {
            return (trailerAccessPermissions(BD_Saved).bits_write & requiredKeyPermission());
        }

        inline virtual bool canWriteBlock(int block) const Q_DECL_OVERRIDE
        {
            return (blockAccessPermissions(block,BD_Saved).block_write & requiredKeyPermission());
        }

        virtual void setTrailerAccess(quint8 bits) Q_DECL_OVERRIDE;
        virtual void setValueBlock(int block, qint32 value, quint8 address) Q_DECL_OVERRIDE;

        inline virtual MifareClassicKey key(MifareClassicKeyType type, BlockData sel =  BD_Current) const Q_DECL_OVERRIDE
        {
            const quint8* addr = MifareSector<16,N>::at(N-1).constData(sel);
            if(type==MifareClassicKeyB)
                addr+=10;
            return MifareClassicKey(addr);
        }

        inline virtual void writeKey(MifareClassicKeyType type, const MifareClassicKey& key) Q_DECL_OVERRIDE
        {
            /*Not used in SL3*/
            if(sec_level == SL3)
                return;

            switch(type)
            {
                case MifareClassicKeyA:
                    MifareSector<16,N>::blocks[N-1].setRaw(key.raw(), 0, 6);
                    //memcpy(&(MifareSector<16,N>::blocks[N-1][0]),key.raw(),6);
                    break;
                case MifareClassicKeyB:
                    MifareSector<16,N>::blocks[N-1].setRaw(key.raw(), 10, 6);
                    //memcpy((&(MifareSector<16,N>::blocks[N-1][0]))+10,key.raw(),6);
                    break;
            }
        }

    protected:
        MifareClassicKeyType key_type;
        SecurityLevel sec_level;
};

class MifareClassicSector : public MifareClassicAbstractSector<4>
{
    public:
        MifareClassicSector() : MifareClassicAbstractSector<4>() {}
        virtual ~MifareClassicSector(){}

        virtual int type() const Q_DECL_OVERRIDE { return ST_MiClassic;}

        virtual quint8 blockAccessBits(int block, BlockData sel =  BD_Current) const Q_DECL_OVERRIDE;
        virtual void setBlockAccess(int block, quint8 bits) Q_DECL_OVERRIDE;
};

class MifareClassicJumboSector : public MifareClassicAbstractSector<16>
{
    public:
        MifareClassicJumboSector() : MifareClassicAbstractSector<16>(){}
        virtual ~MifareClassicJumboSector(){}

        virtual int type() const Q_DECL_OVERRIDE { return ST_MiClassicJumbo;}

        virtual quint8 blockAccessBits(int block, BlockData sel =  BD_Current) const Q_DECL_OVERRIDE;
        virtual void setBlockAccess(int block, quint8 bits) Q_DECL_OVERRIDE;
};

class MifareUltralightAbstractEEPROMInterface
{
    public:
        MifareUltralightAbstractEEPROMInterface(){}
        virtual ~MifareUltralightAbstractEEPROMInterface(){}
        static const QStringList staticLockBits_0_15_Labels;

        virtual quint16 lockBits(BlockData sel =  BD_Current) const = 0;
        virtual void setLockBits(quint16 bits) = 0;

        virtual quint32 otp(BlockData sel =  BD_Current) const = 0;
        virtual void setOTP(quint32 value) = 0;

        virtual QVector<int> newLockedBlocks() const = 0;
};

template<int N>
class MifareUltralightAbstractEEPROM : public MifareSector<4,N>, public MifareUltralightAbstractEEPROMInterface
{
    public:
        MifareUltralightAbstractEEPROM() : MifareSector<4,N>(){}
        virtual ~MifareUltralightAbstractEEPROM(){}

        virtual bool canWriteProtect(int block) Q_DECL_OVERRIDE
        {
            if(block<2)
                return false;
            else if(block<16)
                return true;
            return canWriteProtectExt(block);
        }

        virtual bool writeProtect(int block) Q_DECL_OVERRIDE
        {
            if(block<2)
                return false;
            else
            {
                quint16 lock = lockBits(BD_Current);
                if(block == 2)
                {
                    //lock bits themselves
                    lock |= 0x07;
                }
                else if(block<16)
                {
                    lock |= (1<<block);
                }
                setLockBits(lock);
                return true;
            }
            return writeProtectExt(block);
        }

        virtual bool canReadProtect(int) Q_DECL_OVERRIDE {return false;}
        virtual bool readProtect(int) Q_DECL_OVERRIDE {return false;}

        virtual inline quint16 lockBits(BlockData sel =  BD_Current) const Q_DECL_OVERRIDE
        {
            return read_uint16_le(MifareSector<4,N>::at(2).constData(sel) + 2);
        }

        virtual inline void setLockBits(quint16 bits) Q_DECL_OVERRIDE
        {
            quint16 lb = lockBits(BD_Saved);
            write_uint16_le(lb | bits, &(MifareSector<4,N>::blocks[2][2]));
        }

        virtual inline quint32 otp(BlockData sel =  BD_Current) const Q_DECL_OVERRIDE
        {
            return read_uint32_le(MifareSector<4,N>::at(3).constData(sel));
        }

        virtual inline void setOTP(quint32 value) Q_DECL_OVERRIDE
        {
            quint32 new_otp = otp(BD_Saved) | value;
            quint8 data[4];
            write_uint32_le(new_otp, data);
            MifareSector<4,N>::write(3, data);
            //write_uint32_le(new_otp,&(MifareSector<4,N>::blocks[3][0]));
            //MifareSector<4,N>::blocks[3].touch();
        }

        virtual QVector<int> newLockedBlocks() const Q_DECL_OVERRIDE
        {
            QVector<int> ret;
            for(int i=2;i<qMin(N, 16);++i)
            {
                if(isWriteProtected0_15(i, -1, BD_Current) && !isWriteProtected0_15(i, -1, BD_Saved))
                {
                    ret.append(i);
                }
            }
            return ret;
        }
    protected:
        virtual bool isWriteProtected0_15(int block, int byte, BlockData sel) const
        {
            if(block<2)
            {
                //UID
                return true;
            }
            else if(block==2)
            {
                if((byte>=0)&&(byte<2))
                {

                }
                //the locking bits themselves
                switch(byte)
                {
                    case 0:
                    case 1:
                        // internal data
                        return true;
                    case 2:
                        return ((MifareSector<4,N>::blocks.at(2).constData(sel)[2] & 0x3)==0x3); /*OTP and 9-4 locked*/
                    case 3:
                        return ((MifareSector<4,N>::blocks.at(2).constData(sel)[2] & 0x6)==0x6); /*15-10 and 9-4*/
                    default: /*-1*/
                        return (MifareSector<4,N>::blocks.at(2).constData(sel)[2] & 0x7); /*Any of Block Locking bits set*/
                }
            }

            quint16 lock = MifareUltralightAbstractEEPROM<N>::lockBits(sel);
            return (lock & (1<<block));
        }

        /* For blocks >=16 */
        //virtual bool isDeadLockedExt(int block, BlockData sel =  BD_Current) = 0;
        virtual bool canWriteProtectExt(int block) = 0;
        virtual bool writeProtectExt(int block) = 0;
};

class MifareUltralightAbstractEEPROM_EV1Interface
{
    public:
        MifareUltralightAbstractEEPROM_EV1Interface():
            _last_valid_block(-1){}
        virtual ~MifareUltralightAbstractEEPROM_EV1Interface(){}

        /*
         * -1 - all valid
         */
        virtual int lastValidBlock() const { return _last_valid_block; }
        virtual void setLastValidBlock(int block);

        virtual uint32_t password(BlockData sel) const = 0;
        virtual uint16_t pack(BlockData sel) const = 0;

        virtual MifareUltralightEV1Security getCredentials() const = 0;
        virtual void setCredentials(uint32_t pwd, uint16_t ack) = 0;
        virtual void setCredentials(const MifareUltralightEV1Security& sec) = 0;
        virtual void storeCredentials(uint32_t pwd, uint16_t ack) = 0;
        virtual void restoreCredentials() = 0;

        virtual quint32 auxLockBits(BlockData sel =  BD_Current) const = 0;
        virtual void setAuxLockBits(quint32 bits) = 0;
        virtual QSet<int> auxLockRFU() const = 0;

    protected:
        int _last_valid_block;
};

/*Pages total, Last user page, Password page, Lock bytes page (-1 - not supported)*/
template<int N, int U, int P, int L>
class MifareUltralightAbstractEEPROM_EV1 : public MifareUltralightAbstractEEPROM<N>, public MifareUltralightAbstractEEPROM_EV1Interface
{
    public:
        MifareUltralightAbstractEEPROM_EV1() : MifareUltralightAbstractEEPROM<N>(), MifareUltralightAbstractEEPROM_EV1Interface () {}
        virtual ~MifareUltralightAbstractEEPROM_EV1() {}

        virtual bool isDeadLocked(int block, BlockData sel =  BD_Current) Q_DECL_OVERRIDE
        {
            if(block<2)
                return false;
            return isWriteProtected(block, -1, sel);
        }

        virtual bool isEditable(int block, int byte, BlockData sel =  BD_Current) Q_DECL_OVERRIDE
        {
            if((_last_valid_block>=0)&&(block>_last_valid_block))
               return false;
            if(block <=2)
            {
                return (block==2)&&(byte>=2);
            }

            return (block<=U)||isEditableExt(block, byte, sel);
        }

        virtual bool isReadProtected(int block, int byte, BlockData sel =  BD_Current) Q_DECL_OVERRIDE
        {
            Q_UNUSED(sel)

            if((_last_valid_block>=0)&&(block>_last_valid_block))
               return true;

            /*Password and PACK*/
            return (block==P) || ((block==(P+1)) && (byte<2));
        }

        virtual bool isWriteProtected(int block, int byte, BlockData sel =  BD_Current) Q_DECL_OVERRIDE
        {
            if((_last_valid_block>=0)&&(block>_last_valid_block))
               return true;
            return isWriteProtectedInt(block, byte, sel);
        }

        virtual uint32_t password(BlockData sel) const Q_DECL_OVERRIDE
        {
            return read_uint32_le(MifareSector<4,N>::at(P).constData(sel));
        }

        virtual uint16_t pack(BlockData sel) const Q_DECL_OVERRIDE
        {
            return read_uint16_le(MifareSector<4,N>::at(P+1).constData(sel));
        }

        virtual MifareUltralightEV1Security getCredentials() const Q_DECL_OVERRIDE
        {
            return MifareUltralightEV1Security(_pwd, _pack);
        }

        virtual void setCredentials(uint32_t pwd, uint16_t ack) Q_DECL_OVERRIDE
        {
            write_uint32_le(pwd, &(MifareSector<4,N>::blocks[P][0]));
            write_uint16_le(ack, &(MifareSector<4,N>::blocks[P+1][0]));
        }

        virtual void setCredentials(const MifareUltralightEV1Security& sec) Q_DECL_OVERRIDE
        {
            setCredentials(sec.password, sec.pack);
        }

        virtual void storeCredentials(uint32_t pwd, uint16_t ack) Q_DECL_OVERRIDE
        {
            _pwd = pwd; _pack = ack;
        }

        virtual void restoreCredentials() Q_DECL_OVERRIDE
        {
            setCredentials(_pwd, _pack);
            MifareSector<4,N>::blocks[P].written();
            MifareSector<4,N>::blocks[P+1].written();
        }

        virtual inline quint32 auxLockBits(BlockData sel =  BD_Current) const Q_DECL_OVERRIDE
        {
            if(L>0)
            {
                return read_uint32_le(MifareSector<4,N>::blocks.at(L).constData(sel)) & 0x00FFFFFFUL;
            }
            return 0;
        }

        virtual inline void setAuxLockBits(quint32 bits) Q_DECL_OVERRIDE
        {
            if(L>0)
            {
                write_uint32_le((read_uint32_le(MifareSector<4,N>::blocks.at(L).constData(BD_Saved)) & 0xFF000000UL) | (bits & 0x00FFFFFFUL),
                                &(MifareSector<4,N>::blocks[L][0]));
            }
        }

        virtual QVector<int> newLockedBlocks() const Q_DECL_OVERRIDE
        {
            QVector<int> ret;
            int last_check = (_last_valid_block>=0) ? _last_valid_block : N;
            for(int i=2; i<last_check; ++i)
            {
                if(isWriteProtectedInt(i, -1, BD_Current) && !isWriteProtectedInt(i, -1, BD_Saved))
                {
                    ret.append(i);
                }
            }
            return ret;
        }

    protected:
        virtual bool isWriteProtectedInt(int block, int byte, BlockData sel =  BD_Current) const = 0;
        virtual bool isEditableExt(int block, int byte, BlockData sel =  BD_Current) = 0;

        quint32 _pwd;
        quint16 _pack;
};

template<int Pages>
class MifarePlainUltralightEEPROM : public MifareUltralightAbstractEEPROM<Pages>
{
    public:
        MifarePlainUltralightEEPROM() : MifareUltralightAbstractEEPROM<Pages>() {}
        virtual ~MifarePlainUltralightEEPROM(){}

        virtual int type() const Q_DECL_OVERRIDE { return MifareSectorInterface::ST_MiUlEEPROM;}

        virtual bool isDeadLocked(int block, BlockData sel =  BD_Current) Q_DECL_OVERRIDE
        {
            if(block<2)
                return false;
            return isWriteProtected(block, -1, sel);
        }

        virtual bool isReadProtected(int, int, BlockData sel =  BD_Current) Q_DECL_OVERRIDE {
            Q_UNUSED(sel);
            return false;
        }
        virtual bool isWriteProtected(int block, int byte, BlockData sel =  BD_Current) Q_DECL_OVERRIDE;

        virtual bool forceBinDisplay(int block, int byte) Q_DECL_OVERRIDE;

        virtual bool isEditable(int block, int byte, BlockData sel =  BD_Current) Q_DECL_OVERRIDE;
        virtual bool customEdit(int block, int byte) Q_DECL_OVERRIDE;
        virtual QString customEditName(int block) Q_DECL_OVERRIDE;
        virtual QString editHint(int block,int byte) Q_DECL_OVERRIDE;

    protected:
        virtual bool canWriteProtectExt(int) Q_DECL_OVERRIDE {
            return false;
        }
        virtual bool writeProtectExt(int) Q_DECL_OVERRIDE {
            return false;
        }
};

typedef MifarePlainUltralightEEPROM<16> MifareUltralightEEPROM;
typedef MifarePlainUltralightEEPROM<14> MifareUltralightNanoEEPROM;

class MifareUltralightEV1_80_EEPROM : public MifareUltralightAbstractEEPROM_EV1<20,15,18,-1>
{
    public:
        MifareUltralightEV1_80_EEPROM() : MifareUltralightAbstractEEPROM_EV1<20,15,18,-1>() {}
        virtual ~MifareUltralightEV1_80_EEPROM() {}

        virtual int type() const Q_DECL_OVERRIDE { return ST_MiULExtEEPROM;}

        virtual bool forceBinDisplay(int block, int byte) Q_DECL_OVERRIDE;

        virtual bool customEdit(int block, int byte) Q_DECL_OVERRIDE;
        virtual QString customEditName(int block) Q_DECL_OVERRIDE;
        virtual QString editHint(int block,int byte) Q_DECL_OVERRIDE;

        inline virtual QSet<int> auxLockRFU() const Q_DECL_OVERRIDE
        {
            return QSet<int>();
        }
    protected:
        /* For blocks >=16 */
        virtual bool isWriteProtectedInt(int block, int byte, BlockData sel =  BD_Current) const Q_DECL_OVERRIDE;
        virtual bool canWriteProtectExt(int) Q_DECL_OVERRIDE;
        virtual bool writeProtectExt(int) Q_DECL_OVERRIDE;
        virtual bool isEditableExt(int block, int byte, BlockData sel =  BD_Current) Q_DECL_OVERRIDE;
};

class MifareUltralightEV1_164_EEPROM : public MifareUltralightAbstractEEPROM_EV1<41,35,39,36>
{
    public:
        static const QStringList auxLockBits_16_35_Labels;

        MifareUltralightEV1_164_EEPROM() : MifareUltralightAbstractEEPROM_EV1<41,35,39,36>() {}
        virtual ~MifareUltralightEV1_164_EEPROM() {}

        virtual int type() const Q_DECL_OVERRIDE { return ST_MiULExtEEPROM;}

        virtual bool forceBinDisplay(int block, int byte) Q_DECL_OVERRIDE;

        virtual bool customEdit(int block, int byte) Q_DECL_OVERRIDE;
        virtual QString customEditName(int block) Q_DECL_OVERRIDE;
        virtual QString editHint(int block,int byte) Q_DECL_OVERRIDE;

        inline virtual QSet<int> auxLockRFU() const Q_DECL_OVERRIDE
        {
            return QSet<int>()<<10<<11<<12<<13<<14<<15<<21<<22<<23<<24<<25<<26<<27<<28<<29<<30<<31;
        }
    protected:
        /* For blocks >=16 */
        virtual bool isWriteProtectedInt(int block, int byte, BlockData sel =  BD_Current) const Q_DECL_OVERRIDE;
        virtual bool canWriteProtectExt(int) Q_DECL_OVERRIDE;
        virtual bool writeProtectExt(int) Q_DECL_OVERRIDE;
        virtual bool isEditableExt(int block, int byte, BlockData sel =  BD_Current) Q_DECL_OVERRIDE;
};

template<int N>
class MifareAbstractNTAG : public MifareUltralightAbstractEEPROM_EV1<N,(N-6),(N-2),(N-5)>
{
    public:
        MifareAbstractNTAG(): MifareUltralightAbstractEEPROM_EV1<N,(N-6),(N-2),(N-5)>(){}
        virtual ~MifareAbstractNTAG() {}

        virtual int type() const Q_DECL_OVERRIDE { return MifareSectorInterface::ST_MiNTAGEEPROM;}

        virtual bool forceBinDisplay(int block, int byte) Q_DECL_OVERRIDE
        {
            /*
             * UL lock bits
             * CC (ref. NFC forum tag p20)
             * Dynamic lock bits
             * MIRROR && ACCESS bytes
             */
            return (
                        ((block==2)&&(byte>=2))||
                        (block==3)||
                        ((block==(N-5))&&(byte<3))||
                        (((block==(N-4))||(block==(N-3)))&&(byte==0))
                   );
        }

        virtual bool customEdit(int block, int byte) Q_DECL_OVERRIDE;
        virtual QString customEditName(int block) Q_DECL_OVERRIDE;
        virtual QString editHint(int block,int byte) Q_DECL_OVERRIDE;

    protected:
        /* For blocks >=16 */
        virtual bool isWriteProtectedInt(int block, int byte, BlockData sel =  BD_Current) const Q_DECL_OVERRIDE;
        virtual bool canWriteProtectExt(int) Q_DECL_OVERRIDE;
        virtual bool writeProtectExt(int) Q_DECL_OVERRIDE;
        virtual bool isEditableExt(int block, int byte, BlockData sel =  BD_Current) Q_DECL_OVERRIDE;

        virtual bool isWriteProtectedExtraPages(int block, BlockData sel =  BD_Current) const = 0;
        virtual bool isWriteProtectedAuxLockBits(int byte, BlockData sel =  BD_Current) const = 0;

};

class MifareNTAG213 : public MifareAbstractNTAG<45>
{
    public:
        static const QStringList dynamicLockBits_Labels;

        MifareNTAG213() : MifareAbstractNTAG<45> (){}
        virtual ~MifareNTAG213() {}

        inline virtual QSet<int> auxLockRFU() const Q_DECL_OVERRIDE
        {
            return QSet<int>()<<12<<13<<14<<15<<22<<23<<24<<25<<26<<27<<28<<29<<30<<31;
        }
    protected:
        virtual bool isWriteProtectedExtraPages(int block, BlockData sel =  BD_Current) const Q_DECL_OVERRIDE;
        virtual bool isWriteProtectedAuxLockBits(int byte, BlockData sel =  BD_Current) const Q_DECL_OVERRIDE;
};

class MifareNTAG215 : public MifareAbstractNTAG<135>
{
    public:
        static const QStringList dynamicLockBits_Labels;

        MifareNTAG215() : MifareAbstractNTAG<135> (){}
        virtual ~MifareNTAG215() {}

        inline virtual QSet<int> auxLockRFU() const Q_DECL_OVERRIDE
        {
            return QSet<int>()<<8<<9<<10<<11<<12<<13<<14<<15<<20<<21<<22<<23<<24<<25<<26<<27<<28<<29<<30<<31;
        }
    protected:
        virtual bool isWriteProtectedExtraPages(int block, BlockData sel =  BD_Current) const Q_DECL_OVERRIDE;
        virtual bool isWriteProtectedAuxLockBits(int byte, BlockData sel =  BD_Current) const Q_DECL_OVERRIDE;
};

class MifareNTAG216 : public MifareAbstractNTAG<231>
{
    public:
        static const QStringList dynamicLockBits_Labels;

        MifareNTAG216() : MifareAbstractNTAG<231> (){}
        virtual ~MifareNTAG216() {}

        inline virtual QSet<int> auxLockRFU() const Q_DECL_OVERRIDE
        {
            return QSet<int>()<<14<<15<<23<<24<<25<<26<<27<<28<<29<<30<<31;
        }
    protected:
        virtual bool isWriteProtectedExtraPages(int block, BlockData sel =  BD_Current) const Q_DECL_OVERRIDE;
        virtual bool isWriteProtectedAuxLockBits(int byte, BlockData sel =  BD_Current) const Q_DECL_OVERRIDE;
};

class MifareUltralightCEEPROM : public MifareSector<4,48>
{
    public:
        MifareUltralightCEEPROM() : MifareSector<4,48>() {}
        virtual ~MifareUltralightCEEPROM(){}

        static const QStringList secondLockBitsLabels;

        virtual int type() const Q_DECL_OVERRIDE { return ST_MiUlCEEPROM;}

        virtual bool isReadProtected(int block, int byte, BlockData sel =  BD_Current) Q_DECL_OVERRIDE;
        virtual bool isDeadLocked(int block, BlockData sel =  BD_Current) Q_DECL_OVERRIDE;
        virtual bool isWriteProtected(int block, int byte, BlockData sel =  BD_Current) Q_DECL_OVERRIDE;

        virtual bool canReadProtect(int block) Q_DECL_OVERRIDE;
        virtual bool canWriteProtect(int block) Q_DECL_OVERRIDE;

        virtual bool readProtect(int block) Q_DECL_OVERRIDE;
        virtual bool writeProtect(int block) Q_DECL_OVERRIDE;

        virtual bool forceBinDisplay(int block, int byte) Q_DECL_OVERRIDE;

        virtual bool isEditable(int block, int byte, BlockData sel =  BD_Current) Q_DECL_OVERRIDE;
        virtual bool customEdit(int block, int byte) Q_DECL_OVERRIDE;
        virtual QString customEditName(int block) Q_DECL_OVERRIDE;
        virtual QString editHint(int block,int byte) Q_DECL_OVERRIDE;

        quint16 firstLockBits(BlockData sel =  BD_Current) const;
        quint16 secondLockBits(BlockData sel =  BD_Current) const;
        void setFirstLockBits(quint16 bits);
        void setSecondLockBits(quint16 bits);
        quint32 otp(BlockData sel =  BD_Current) const;
        void setOTP(quint32 value);

        MifareUltralightKey getKey(MifareUltralightCKeyType type, BlockData sel =  BD_Current) const;
        bool setKey(MifareUltralightCKeyType type, const MifareUltralightKey& key);
};

#endif // MIFARESECTOR_H
