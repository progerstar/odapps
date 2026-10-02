#ifndef RFIDCARDUSBREADER_H
#define RFIDCARDUSBREADER_H

#include <rfidcardreaderinterface.h>

class RFIDCardUSBReader : public RFIDCardReaderInterface
{
        Q_OBJECT
    public:
        enum State {
            Wait,
            Idle,
            Read,
            Write
        };

        explicit RFIDCardUSBReader(QObject* parent = 0);
        virtual ~RFIDCardUSBReader();

        void setManager(OdHalAbstractMessenger* handler);

        inline virtual int type() const Q_DECL_OVERRIDE{ return RFIDReader_Hardware+1; }

        virtual QByteArray getUID() Q_DECL_OVERRIDE;
        virtual MifareCards getType() Q_DECL_OVERRIDE;

        virtual bool setKey(MifareClassicKeyType type, const MifareClassicKey& key) Q_DECL_OVERRIDE;

        virtual void readBlock(quint16 blockAddress, int blockCount = 1) Q_DECL_OVERRIDE;
        virtual void writeBlock(quint16 blockAddress, const quint8* data, int blockSize) Q_DECL_OVERRIDE;

        inline virtual void removeCard() Q_DECL_OVERRIDE
        {
            if(m_card_present)
            {
                emit cardRemoved();
            }
            m_card_present = false;
        }

        virtual void startPolling() Q_DECL_OVERRIDE;

    public slots:
        void reboot();
    protected:
        virtual void poll() Q_DECL_OVERRIDE;
    private slots:
        void msg_ready();
        void nack();
    private:
        OdHalAbstractMessenger* handle;
        State state;
        QTimer reply_timer;

        QByteArray current_card_uid;
        quint8     current_card_sak;

        bool sendPacket(const QByteArray& data);

        void checkStatus(uint32_t code, bool write);

        void processDataPacket(const OdHalPacket& pkt);
};

#endif // RFIDCARDUSBREADER_H
