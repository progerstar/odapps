#ifndef RFIDCARDCDCATREADER_H
#define RFIDCARDCDCATREADER_H

#include <rfidcardreaderinterface.h>
#include <qcomport.h>
#include <uartcommunicator.h>

#include <QQueue>

#define RFIDCARDCDCATREADER_ID  (3)
#define RFIDCARDUARTREADER_ID   (4)
#define RFIDCARDRS485READER_ID  (5)

class RFIDCardCdcAtReader : public RFIDCardHardwareReader
{
        Q_OBJECT
        Q_PROPERTY(QString device READ device WRITE setDevice)
        Q_PROPERTY(PROTOCOL protocol READ getProtocol WRITE setProtocol)
    public:
        enum PROTOCOL {
            CDC = 0,
            UART = 1,
            RS485 = 2
        };
        Q_ENUM(PROTOCOL)

        explicit RFIDCardCdcAtReader(QObject* parent = nullptr);
        virtual ~RFIDCardCdcAtReader();

        static QStringList availableDevices(PROTOCOL proto = RFIDCardCdcAtReader::CDC);
        static QList<PortDescriptor> availableDeviceList(PROTOCOL proto = RFIDCardCdcAtReader::CDC);
        inline virtual QString device() const override
        {
            return (_proto == RS485) ? (rs485->isOpen() ? rs485->portName() : "") : (handle->isOpen() ? handle->portName() : "");
        }

        inline virtual bool isOpen() const override
        {
            return (_proto == RS485) ? rs485->isOpen() : handle->isOpen();
        }

        inline virtual bool supportsLfMemory() const override
        {
            return true;
        }

        inline virtual int type() const override
        {
            return RFIDReader_Hardware;
        }

        inline virtual int subtype() const override
        {
            switch(_proto) {
                case CDC: return RFIDCARDCDCATREADER_ID;
                case UART: return RFIDCARDUARTREADER_ID;
                case RS485: return RFIDCARDRS485READER_ID;
            }
            return 0;
        }

        inline virtual QString version() const override
        {
            return dev_fw;
        }

        virtual QByteArray getUID() override;
        virtual MifareCards getType() const override;

        inline RFIDCardCdcAtReader::PROTOCOL getProtocol() const {
            return _proto;
        }

        inline void setProtocol(RFIDCardCdcAtReader::PROTOCOL proto) {
            _proto = proto;
        }

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
        virtual void readLfMemory(bool fullScan, const QByteArray& password) override;
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
        void serialError(QSerialPort::SerialPortError error);
        void aboutToClose();
        void msg_ready();
        void rs485_ready();
    private:
        QSerialPort* handle;
        UARTCommunicator* rs485;
        PROTOCOL _proto;

        QByteArray port_data;
        QList<QByteArray> cmd_reply;
        QString repl_cmd;
        bool new_card_requested;

        struct Command
        {
                QString cmd;
                QByteArray param;
                int timeout;
        };

        QQueue<Command> commandQueue;

        void clearCard();
        bool sendPacket(const QString& cmd, const QByteArray& param = QByteArray(),
                        int timeout = 900);

        enum LFReadState
        {
            LFRead_Idle,
            LFRead_Classify,
            LFRead_LegacyInfo,
            LFRead_T55xx,
            LFRead_Trace,
            LFRead_Em4x05,
            LFRead_Stream
        };

        LFReadState lf_read_state;
        LFMemoryReadResult lf_read_result;
        QByteArray lf_password;
        int lf_address;
        int repl_timeout;

        bool processLfMemoryPacket(bool success);
        bool prepareLfMemoryRead(const LFMemoryClassification& classification);
        bool sendLfMemoryPacket(const QString& cmd,
                                const QByteArray& param = QByteArray());
        void readNextLfMemoryBlock();
        void finishLfMemoryRead(const QString& error = QString());
        QString lfPacketError(const QString& fallback) const;
        void setLfBlockError(int block, const QString& error);

        void checkStatus(uint32_t code, bool write, QString &errorDescr);

        void processDataLine(const QByteArray &line);
        void processDataPacket(bool success);

        void closeInt();
};

#endif // RFIDCARDCDCATREADER_H
