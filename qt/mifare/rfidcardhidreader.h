#ifndef RFIDCARDHIDREADER_H
#define RFIDCARDHIDREADER_H

#include <rfidcardreaderinterface.h>
#include <hidproxy.h>

#include <QQueue>

#define RFIDCARDHIDREADER_ID (2)

class RFIDCardHIDReader : public RFIDCardHardwareReader
{
        Q_OBJECT
        Q_PROPERTY(QString device READ device WRITE setDevice)
    public:
        explicit RFIDCardHIDReader(QObject* parent = nullptr);
        virtual ~RFIDCardHIDReader();

        static QStringList availableDevices();
        static QList<PortDescriptor> availableDeviceList();
        virtual QString device() const override;

        virtual bool isOpen() const override;

        inline virtual int type() const override
        {
            return RFIDReader_Hardware;
        }

        inline virtual int subtype() const override
        {
            return RFIDCARDHIDREADER_ID;
        }

        inline virtual QString version() const override
        {
            return  dev_fw;
        }

        virtual QByteArray getUID() override;
        virtual MifareCards getType() const override;

    public slots:
        virtual void setDevice(const QString& name) override;
        virtual void close() override;
        virtual void setUID(const QByteArray& uid) override;
        virtual void writeEM(const QByteArray& uid, const QByteArray& key, const QByteArray& pwd, int coding, int bits) override;
        virtual void setKey(MifareClassicKeyType type, const MifareClassicKey& key) override;
        virtual void setPassword(quint32 pwd) override;
        virtual void setPlusKey(MifareClassicKeyType type, const MifarePlusKey& key) override;

        virtual void readPack() override;
        virtual void readBlock(quint16 blockAddress, int blockCount = 1) override;
        virtual void writeBlock(quint16 blockAddress, const QByteArray& blk) override;

        virtual bool selectCard(const QByteArray& uid) override;
        virtual void removeCard() override;
        virtual void poll() override;

        virtual bool dfu() override;
        virtual bool selectIface(int sel) override;
    protected:
        virtual bool plusAuthKey(quint16 keyBrn) override;
        virtual bool hal_transmit(const QByteArray& data) override;
    protected slots:
        virtual void nack() override;
        virtual void reboot() override;
    private slots:
        void hidQuery(const QByteArray& data);
        void handleDisconnected();
    private:
        HIDProxy* handle;

        bool legacy_cmdset;
        bool new_card_requested;
        QQueue<QByteArray> commandQueue;

        void clearCard();

        bool sendPacket(quint8 cmd, const QByteArray& param = QByteArray());
        inline bool sendCmd(quint8 cmd, quint8 value)
        {
            return sendPacket(cmd, QByteArray(reinterpret_cast<const char*>(&value), 1));
        }

        void checkStatus(uint32_t code, bool write, QString &errorDescr);

        //void processDataLine(const QByteArray &line);
        //void processDataPacket(bool success);

        void readBlockInt(uint16_t blockAddress);

};

#endif // RFIDCARDHIDREADER_H
