#ifndef MIFAREBLOCK_H
#define MIFAREBLOCK_H

#include "mifare_global.h"

#include <endianrw.h>

#include <QByteArray>

enum BlockData {
    BD_Current,
    BD_Saved
};

template<int N>
class MifareBlock
{
    public:

        MifareBlock()
        {
            memset(orig,0,N);
            memset(data,0,N);
        }
        virtual ~MifareBlock(){}

        MifareBlock(const MifareBlock<N>& other)
        {
            memcpy(orig,other.orig,N);
            memcpy(data,other.data,N);
        }

        explicit MifareBlock(const void* raw)
        {
            memcpy(orig,raw,N);
            memcpy(data,raw,N);
        }

        MifareBlock& operator=(const MifareBlock<N>& other)
        {
            if(this != &other)
            {
                memcpy(orig,other.orig,N);
                memcpy(data,other.data,N);
            }
            return *this;
        }

        inline virtual bool operator==(const MifareBlock<N>& other) const
        {
            return (memcmp(orig,other.orig,N)==0) && ((memcmp(data,other.data,N)==0));
        }

        inline virtual bool operator!=(const MifareBlock<N>& other) const
        {
            return !(other == *this);
        }

        inline int size() const { return N; }
        quint8& operator[](int i) { return data[i]; }
        quint8 at(int i) const { return data[i]; }
        const quint8* raw() const { return data; }
        const quint8* saved() const { return orig; }
        const quint8* constData(BlockData sel =  BD_Current) const
        {
            return (sel==BD_Current) ? data : orig;
        }
        inline bool modified() const {
            return (memcmp(orig,data,N)!=0);
        }

        inline QString get(int i, DataFormat fmt = DR_Hex) const
        {
            return numberToString(data[i],fmt);
        }

        inline void fromRaw(const quint8* buf)
        {
            memcpy(data,buf,N);
        }

        inline void written()
        {
            memcpy(orig,data,N);
        }

        inline bool set(int i, const QString& number, DataFormat fmt = DR_Hex)
        {
            bool ok;
            int n = stringToNumber(number,fmt,&ok);
            if(ok && (n>=0) && (n<=255))
            {
                data[i] = n;
                return true;
            }
            return false;
        }

        inline void setRaw(const void* src, int offset=0, int size = N)
        {
            memcpy(data+offset, src, size);
        }

        inline QString toString(BlockData sel = BD_Current) const
        {
            return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(constData(sel)),N).toHex().toUpper());
        }

    protected:
        quint8 data[N];
        quint8 orig[N];
};

typedef MifareBlock<16> MifareClassicBlock;
typedef MifareBlock<4>  MifareUltralightBlock;

class MifareKeyInterface
{
    public:
        MifareKeyInterface(){}
        virtual ~MifareKeyInterface(){}

        /*Interface*/
        virtual bool fromString(const QString& text) = 0;
        virtual QString toString() const = 0;
        virtual QByteArray toHex() const = 0;
};

template<int N>
class MifareAbstractKey : public MifareBlock<N>, public MifareKeyInterface
{
    public:
        MifareAbstractKey() : MifareBlock<N>()
        {
            memset(MifareBlock<N>::data, 0xFF, N);
        }

        MifareAbstractKey<N>& operator=(const MifareAbstractKey<N>& other)
        {
            if(this != &other)
            {
                memcpy(MifareBlock<N>::orig, other.orig, N);
                memcpy(MifareBlock<N>::data, other.data, N);
            }
            return *this;
        }

        virtual ~MifareAbstractKey(){}

        MifareAbstractKey(const QString& textKey) : MifareBlock<N>()
        {
            fromString(textKey);
        }

        explicit MifareAbstractKey(const void* raw) : MifareBlock<N>(raw)
        {

        }

        virtual bool compare(const MifareAbstractKey<N>* other) const
        {
            return (memcmp(MifareBlock<N>::data, other->data, N) == 0);
        }

        /*Implementation*/
        virtual bool fromString(const QString& text) Q_DECL_OVERRIDE
        {
            QRegExp keyRX = QRegExp(QString("[A-F0-9]{%1,%1}").arg(N*2), Qt::CaseInsensitive);
            if(!keyRX.exactMatch(text))
            {
                memset(MifareBlock<N>::data, 0xFF, N);
                return false;
            }
            QByteArray tdata = QByteArray::fromHex(text.toLatin1());
            memcpy(MifareBlock<N>::data, tdata.constData(), N);
            return true;
        }

        virtual QString toString() const Q_DECL_OVERRIDE
        {
            QByteArray tdata(reinterpret_cast<const char*>(MifareBlock<N>::data), N);
            return QString::fromLatin1(tdata.toHex().toUpper());
        }

        virtual QByteArray toHex() const Q_DECL_OVERRIDE
        {
            QByteArray tdata(reinterpret_cast<const char*>(MifareBlock<N>::data), N);
            return tdata.toHex().toUpper();
        }
};

class MifareClassicKey : public MifareAbstractKey<6>
{
    public:
        using MifareAbstractKey<6>::operator==;

        MifareClassicKey() : MifareAbstractKey<6> (){}
        MifareClassicKey(const QString& textKey) : MifareAbstractKey<6>(textKey){}
        explicit MifareClassicKey(const void* raw) : MifareAbstractKey<6>(raw) {}
        inline MifareClassicKey& operator=(const MifareClassicKey& other) {
            (void)MifareAbstractKey<6>::operator=(other);
            return *this;
        }
        MifareClassicKey(const MifareClassicKey& other) : MifareAbstractKey<6>(other.data){}
        virtual ~MifareClassicKey(){}

        virtual bool operator==(const MifareClassicKey& other) const {
            return this->compare(&other);
        }
};

Q_DECLARE_METATYPE(MifareClassicKey)

class MifareUltralightKey : public MifareAbstractKey<8>
{
    public:
        using MifareAbstractKey<8>::operator==;

        explicit MifareUltralightKey(MifareUltralightCKeyType key = MifareUltralightCKey1) : MifareAbstractKey<8>()
        {
            write_uint64_le(key==MifareUltralightCKey1? 0x49454D4B41455242ULL :
                                                       0x214E4143554F5946ULL, data);
        }

        explicit MifareUltralightKey(quint64 val) : MifareAbstractKey<8>()
        {
            write_uint64_le(val, data);
        }

        MifareUltralightKey(const QString& textKey) : MifareAbstractKey<8>(textKey){}
        explicit MifareUltralightKey(const void* raw) : MifareAbstractKey<8>(raw) {}
        MifareUltralightKey(const MifareUltralightKey& other) : MifareAbstractKey<8>(other.data){}
        virtual ~MifareUltralightKey(){}

        virtual bool operator==(const MifareUltralightKey& other) const {
            return this->compare(&other);
        }
};

Q_DECLARE_METATYPE(MifareUltralightKey)

class MifarePlusKey : public MifareAbstractKey<16>
{
    public:
        using MifareAbstractKey<16>::operator==;

        MifarePlusKey() : MifareAbstractKey<16>(){}
        MifarePlusKey(const QString& textKey) : MifareAbstractKey<16>(textKey){}
        explicit MifarePlusKey(const void* raw) : MifareAbstractKey<16>(raw) {}
        MifarePlusKey(const MifarePlusKey& other) : MifareAbstractKey<16>(other.data) {}
        MifarePlusKey& operator=(const MifarePlusKey& other) {
            (void)MifareAbstractKey<16>::operator=(other);
            return *this;
        }
        virtual ~MifarePlusKey(){}

        virtual bool operator==(const MifarePlusKey& other) const {
            return this->compare(&other);
        }
};

Q_DECLARE_METATYPE(MifarePlusKey)

template<int N>
QDebug operator<<(QDebug debug, const MifareBlock<N> &b)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "MifareBlock(" << N << ", " << b.toString(BD_Saved) << " / " << b.toString(BD_Current) << ")";

    return debug;
}

#endif // MIFAREBLOCK_H
