#ifndef EM4100DATA_H
#define EM4100DATA_H

#include <mifaresector.h>
#include <QVector>

class EM4100Data : public MifareSectorInterface
{
    public:
        explicit EM4100Data(const QByteArray& uid, MifareCards cardType = MF_EM_4100) :
            MifareSectorInterface(), _raw(qMax(1, uid.size()), quint8(0)),
            _cardType(cardType) {
            if(!uid.isEmpty()) {
                const quint8* uid_data = (const uint8_t*)uid.constData();
                memcpy(&_raw[0], uid_data, size_t(uid.size()));
            }
        }
        ~EM4100Data(){}

        int type() const override { return ST_EmMARINE;}
        int blockCount() const override { return 1; }
        int size() const override { return _raw.size(); }
        int blockSize() const override { return _raw.size(); }

        bool isReadProtected(int, int, BlockData) override { return false; }
        bool isDeadLocked(int, BlockData) override { return true; }
        bool isWriteProtected(int, int, BlockData) override { return true; }

        bool canReadProtect(int) override { return false; }
        bool canWriteProtect(int) override { return false; }
        bool readProtect(int) override { return false; }
        bool writeProtect(int) override { return false; }

        bool isEditable(int, int, BlockData) override { return false; }
        bool customEdit(int, int) override {return false; }
        QString editHint(int block, int byte) override;
        bool customEdit(int) override { return false; }
        QString customEditName(int) override { return QString(); }
        QString blockTooltip(int, BlockData) const override { return QString(); }

        quint8& byte(int, int byte) override {
            return _raw[byte];
        }
        quint8 value(int block, int byte) const override {
            return !block ? _raw.at(byte) : 0;
        }

        const quint8* raw(int) const override { return &_raw.at(0);}
        const quint8* saved(int) const override { return &_raw.at(0);}
        void write(int, const quint8*) override{}
        bool modified(int) const override { return false;}
        bool modified() const override { return false; }
        void written(int) override {}
    private:
        QVector<quint8> _raw;
        MifareCards _cardType;
};

#endif // EM4100DATA_H
