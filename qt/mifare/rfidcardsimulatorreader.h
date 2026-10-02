#ifndef RFIDCARDSIMULATORREADER_H
#define RFIDCARDSIMULATORREADER_H

#include <rfidcardreaderinterface.h>

class RFIDCardSimulatorReader : public RFIDCardReaderInterface
{
        Q_OBJECT
    public:
        explicit RFIDCardSimulatorReader(QObject* parent = 0);
        virtual ~RFIDCardSimulatorReader();

        inline virtual int type() const Q_DECL_OVERRIDE
        {
            return RFIDReader_Simulator;
        }

        inline virtual int subtype() const Q_DECL_OVERRIDE
        {
            return 0;
        }

        inline virtual QString version() const Q_DECL_OVERRIDE
        {
            return "1.4F";
        }

        inline virtual bool isOpen() const Q_DECL_OVERRIDE
        {
            return true;
        }
        virtual void close() Q_DECL_OVERRIDE;

        quint8 sak() const;
        quint8 uidSize() const;

        virtual QByteArray getUID() Q_DECL_OVERRIDE;
        virtual MifareCards getType() const Q_DECL_OVERRIDE;

        bool newCard(int type, const QString& cardfile, quint8 uid_size, QString* error);
    signals:
        void cardOpened(bool success, const QString& error);
        void cardCreated(bool success, const QString& error);
    public slots:
        virtual void setUID(const QByteArray& uid) Q_DECL_OVERRIDE;
        virtual void writeEM(const QByteArray& uid, const QByteArray& key, const QByteArray& pwd, int coding, int bits) Q_DECL_OVERRIDE;

        virtual void setPassword(quint32 pwd) Q_DECL_OVERRIDE;
        virtual void setKey(MifareClassicKeyType type, const MifareClassicKey& key) Q_DECL_OVERRIDE;
        virtual void setPlusKey(MifareClassicKeyType type, const MifarePlusKey& key) Q_DECL_OVERRIDE;

        virtual void readPack() Q_DECL_OVERRIDE;
        virtual void readBlock(quint16 blockAddress, int blockCount = 1) Q_DECL_OVERRIDE;
        virtual void writeBlock(quint16 blockAddress, const QByteArray& blk) Q_DECL_OVERRIDE;
        virtual void poll() Q_DECL_OVERRIDE {
            emit polled();
        }

        inline virtual void removeCard() Q_DECL_OVERRIDE
        {
            close();
        }

        void open(const QString& cardfile);
        void create(int type, const QString& output);
    protected:
        virtual bool plusAuthKey(quint16 keyBrn) Q_DECL_OVERRIDE;
        virtual bool hal_transmit(const QByteArray&) Q_DECL_OVERRIDE { /*NOP*/ return true;}
    private slots:
        void read_finished();
        void write_finished();
    private:
        mutable std::fstream card_file;
        quint64 file_size;

        /**************Card Specific****************************/
        ///Classic

        int mfc_sector;
        inline MifareClassicPermission currentClassicPermission()
        {
            return (mfc_key_type==MifareClassicKeyA) ? MC_Permissions_KeyA : MC_Permissions_KeyB;
        }

        MifareClassicBlock   currentClassicSectorTrailer();
        MifareClassicKey     currentClassicKey();
        MifareClassicBlockAccess currentBlockAccess(int block);
        MifareClassicTrailerAccess currentTrailerAccess();
        /*******************************************************/

        bool open_int(const QString& cardfile, QString* error);
};

#endif // RFIDCARDSIMULATORREADER_H
