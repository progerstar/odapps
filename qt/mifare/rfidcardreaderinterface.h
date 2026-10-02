#ifndef RFIDCARDREADERINTERFACE_H
#define RFIDCARDREADERINTERFACE_H

#include <QObject>
#include <QTimer>
#include <QPair>
#include <QVariant>
#include <QVersionNumber>

#include <fstream>

#include "mifareblock.h"
#include "lfmemoryprotocol.h"

class MifareClassicKey;

typedef QPair<QString,QVariant> PortDescriptor;

class RFIDCardReaderInterface : public QObject
{
        Q_OBJECT
        Q_PROPERTY(QString version READ version NOTIFY versionChanged)
    public:
        enum Type
        {
            RFIDReader_Simulator = 0,
            RFIDReader_Hardware  = 1,
            RFIDReader_Invalid   = 255
        };

        enum MiscCode {
            MC_DFUResult = 0,
            MC_User = 1,
        };

        /* for the micro part of the QVersionNumber */
        enum Revision
        {
            REV_FirstGen = 0,
            REV_SecondGen_High = 1, /*high-freq*/
            REV_SecondGen_Low  = 2, /*low-freq*/
            REV_SecondGen_HighLow = 3, /*high-freq & low-freq */
            REV_ThirdGen_High = 4,
            REV_ThirdGen_Low = 5,
            REV_ThirdGen_HighLow = 6,
        };


        static Revision revisionForCode(int maj, int min, const QString& code);
        static QString codeForRevision(Revision rev);
        static bool parseFirmwareVersion(const QString& input, QString& versionText,
                                         QVersionNumber& versionNumber, int* position = nullptr);

        enum Capability {
            HF_Reader = 0x01,
            EM_Reader = 0x02,
            MF_Plus   = 0x04,
            MF_Ulev   = 0x08,
            IF_Switch = 0x10,
            IF_Combo  = 0x20,
            IND_Led   = 0x40,
            IND_Buzz  = 0x80,
            DFU_MS    = 0x100,
            USB_Profile = 0x200,
        };
        Q_ENUM(Capability);
        Q_DECLARE_FLAGS(Capabilities, Capability)

        static bool hasCapability(const QVersionNumber& num, Capability cap);
        /* true if has ALL */
        static bool hasCapabilities(const QVersionNumber& num, Capabilities caps);

        static unsigned int cfglineSize(const QVersionNumber& num);

        enum RW_Result
        {
            RW_OK = 0,
            RW_Retry,
            RW_Denied,
            RW_Fatal,
            RW_Invarg
        };

        static const QStringList readErrorStrings;
        static quint16 plusKeyBNr(quint16 blockAddress, MifareClassicKeyType type);
        static bool isExtCard(quint8 type);

        explicit RFIDCardReaderInterface(QObject *parent = nullptr);
        virtual ~RFIDCardReaderInterface();

        virtual int type() const = 0;
        virtual int subtype() const = 0;
        virtual QString version() const = 0;

        virtual bool isOpen() const = 0;
        virtual bool supportsLfMemory() const { return false; }

        virtual QByteArray getUID() = 0;
        virtual MifareCards getType() const = 0;

        virtual quint8 blockSize() const
        {
            return m_card_present ? current_card_info.block_size : 0;
        }
        virtual quint16 blockCount() const
        {
            return m_card_present ? current_card_info.blocks : 0;
        }

        inline bool haveCard() const { return m_card_present; }

        inline MifareClassicKey currentKey() const
        {
            return mfc_key;
        }

        inline MifareClassicKeyType currentKeyType() const
        {
            return mfc_key_type;
        }

        inline quint32 currentPassword() const
        {
            return ul_pwd;
        }

        inline quint16 currentPack() const
        {
            return ul_pack;
        }

        inline MifareClassicPermission requiredPermission() const
        {
            return (mfc_key_type==MifareClassicKeyA) ? MC_Permissions_KeyA : MC_Permissions_KeyB;
        }

        inline int blocksLeft() const { return block_count; }
    signals:
        void versionChanged(const QString& v);
        void fullVersionChanged(const QString& v, const QVersionNumber& vn);

        void polled();
        void cardPresent(const QByteArray& uid, int type);
        void cardRemoved();
        void uidChanged(bool success);

        void blockRead(int address, int left, QByteArray data, int code);
        void blockWritten(int address, int code);
        void passwordChanged(bool success, quint32 pwd);
        void keyChanged(bool success);
        void plusKeyChanged(bool success);
        void plusAuthorized(bool success);
        void packReceived(bool success, quint16 value);
        void error(const QString& err);
        void misc(int code, const QVariant& data);
        void lfMemoryReadFinished(const LFMemoryReadResult& result);
    public slots:
        virtual void close();
        virtual void setKey(MifareClassicKeyType type, const MifareClassicKey& key) = 0;
        virtual void setPassword(quint32 pwd) = 0;
        virtual void setPlusKey(MifareClassicKeyType type, const MifarePlusKey& key) = 0;

        virtual void plusAuth(quint16 blockAddress);
        virtual void readPack() = 0;
        virtual void readBlock(quint16 blockAddress, int blockCount = 1) = 0;
        virtual void restartRead()
        {
            const int count = qMax(1, block_count);
            block_count=0;
            readBlock(block_address, count);
        }
        virtual void writeBlock(quint16 blockAddress, const QByteArray& blk) = 0;

        virtual void setUID(const QByteArray& uid) = 0;
        virtual void writeEM(const QByteArray& uid, const QByteArray& key, const QByteArray& pwd, int coding, int bits) = 0;

        virtual void removeCard() = 0;

        virtual void readLfMemory(bool fullScan, const QByteArray& password);

        virtual void poll() = 0;
    protected slots:
        virtual void startReplyTimer(int ms = 1000);
        virtual void stopReplyTimer();
    protected:
        QTimer *reply_timer;
        bool   m_card_present;
        qint64 tx_timestamp;

        quint16    block_address;
        int        block_count;

        struct CardInfo {
            QByteArray uid;
            quint8 sak;
            quint16 blocks;
            quint8 block_size;
            quint8 type;

            inline void clear() {
                uid.clear();
                sak = 0;
                type = MF_UNKNOWN;
                blocks = 0;
                block_size = 0;
            }
        } current_card_info;

        /**************Card Specific****************************/
        ///Classic

        MifareClassicKeyType mfc_key_type;
        MifareClassicKey     mfc_key;

        ///Plus

        MifareClassicKeyType mfp_aes_type;
        MifarePlusKey        mfp_aes_key;

        ///Ultralight
        quint32 ul_pwd;
        quint16 ul_pack;

        virtual bool transmit(const QByteArray& data);
        virtual bool plusAuthKey(quint16 keyBrn) = 0;
        virtual bool hal_transmit(const QByteArray& data) = 0;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(RFIDCardReaderInterface::Capabilities)

class RFIDCardHardwareReader : public RFIDCardReaderInterface
{
        Q_OBJECT
    public:
        enum Iface {
            iHID = 1, /* from hal_rfid.h */
            iCDC = 2,
        };

        enum FwResult {
            DFailure,
            DBuiltin,
            DExternal,
        };

        explicit RFIDCardHardwareReader(QObject *parent = nullptr) : RFIDCardReaderInterface(parent) {}
        virtual ~RFIDCardHardwareReader(){}

        virtual QString device() const  = 0;

    signals:
        void disconnected();
    public slots:
        virtual void setDevice(const QString& name) = 0;
        virtual bool selectCard(const QByteArray& uid) = 0;
        virtual bool dfu() = 0;
        virtual bool selectIface(int sel) = 0;
    protected slots:
        virtual void nack() = 0;
        virtual void reboot() = 0;
    protected:
        QString dev_fw;
        QVersionNumber fw_version;
};

#endif // RFIDCARDREADERINTERFACE_H
