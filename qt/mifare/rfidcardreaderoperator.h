#ifndef RFIDCARDREADEROPERATOR_H
#define RFIDCARDREADEROPERATOR_H

#include <QObject>
#include <QQueue>
#include <QThread>
#include <QTimer>
#include <QVersionNumber>

#include <mifareblock.h>
#include <lfmemoryprotocol.h>

class RFIDCardReaderInterface;

class RFIDCardReaderOperator : public QObject
{
        Q_OBJECT
        Q_PROPERTY(bool valid READ isValid NOTIFY readerValid)
        Q_PROPERTY(CardState cardState READ cardState NOTIFY cardStateChanged)
        Q_PROPERTY(int pollInterval READ pollInterval WRITE setPollInterval)
    public:
        enum Level {
            Debug,
            Info,
            Warning,
            Critical
        };
        Q_ENUM(Level)

        enum OpCode {
            OP_Continue,
            OP_Cancel,
            OP_Abort
        };
        Q_ENUM(OpCode)

        enum CardState {
            CS_NoCard = 0,
            CS_Idle,
            CS_ReadFailed,
            CS_ReadPend,
            CS_ReadOK,
            CS_Writing,
            CS_Written,
            CS_WriteFailed,
        };
        Q_ENUM(CardState)

        enum Command {
            NOP = 0,
            Poll,
            ReadBlock,
            WriteBlock,
            WriteFinalize,
            SetKey,
            SetAESKey,
            SetPassword,
            RequestPack,
            PlusAuth,
            ChangeUID,
            RemoveCard,
            SelectCard,
            ReadLFMemory,
        };
        Q_ENUM(Command)

        explicit RFIDCardReaderOperator(QObject *parent = nullptr);
        ~RFIDCardReaderOperator();

        inline CardState cardState() const
        {
            return card_state;
        }

        bool isValid() const;
        inline bool supportsLfMemory() const
        {
            return isValid() && _lf_memory_supported;
        }
        inline int type() const
        {
            return _reader_type;
        }
        inline QByteArray uid() const
        {
            return _reader_uid;
        }
        QString uidString() const;
        inline MifareCards card() const
        {
            return _reader_tcard;
        }

        int pollInterval() const {
            return poller.interval();
        }

        void setPollInterval(int intv) {
            poller.setInterval(qMax(int(250), intv));
        }
    signals:
        void cardStateChanged(CardState st);
        void readerValid(bool on);
        void readerVersion(const QString& v, const QVersionNumber& vn);

        void passwordChanged(bool success, quint32 pwd);
        void packReceived(bool success, quint16 pack);

        void cardOpened(bool success, const QString& error);
        void cardDetected(const QByteArray& uid, int type);
        void cardUidChanged();

        void blockRead(int number, QByteArray data, int result);
        void blockWritten(int number, int code);
        void lfMemoryReadFinished(const LFMemoryReadResult& result);

        void error(Level lev, const QString& title, const QString& msg);
        void misc(int code, const QVariant& data);
    public slots:
        void setReader(int type, quint32 subtype, const QVariant& param);
        void close();
        bool dfu();
        bool switchInterface();

        void setPassword(quint32 pwd);
        void setUID(const QByteArray& uid);
        void writeUID(const QByteArray& uid, const QByteArray& key, const QByteArray &pwd, int coding, int speed);
        void setKey(int type, const MifareClassicKey& key);
        void setPlusKey(int type, const MifarePlusKey& key);
        void newCard(MifareCards type, const QString& output);
        void openCard(const QString& id);
        void removeCard();
        void rectifyCard(); //ex: Ultralight EV1 read may fail with the wrong password, but the 'compat' part may still be usable
        void readPack();
        void plusAuth(int block);
        void read(int blk, int count = 1);
        void write(int blk, const QByteArray& data);
        void readLfMemory(bool fullScan, const QByteArray& password = QByteArray());
    private slots:
        void worker_finished();
        void poller_timeout();

        void reader_versionChanged(const QString& v, const QVersionNumber& vn);
        void reader_passwordChanged(bool success, quint32 pwd);
        void reader_keyChanged(bool success);
        void reader_plusKeyChanged(bool success);
        void reader_plusAuthorized(bool success);
        void reader_polled();
        void reader_cardOpened(bool success, const QString& error);
        void reader_cardDetected(const QByteArray& uid, int type);
        void reader_cardRemoved();
        void reader_cardUidChanged(bool success);
        void reader_cardPackReceived(bool success, quint16 pack);
        void reader_blockRead(int number, int left, QByteArray data, int result);
        void reader_blockWritten(int address, int code);
        void reader_lfMemoryReadFinished(const LFMemoryReadResult& result);

        void do_queuePop();
    private:
        class PendingOP {
            public:
                PendingOP(): cmd(NOP) {}
                PendingOP(Command c, const QVariantList& v = QVariantList()) : cmd(c), params(v){}

                Command cmd;
                QVariantList params;

        };

        QTimer poller;
        QThread workerThread;
        RFIDCardReaderInterface* _reader;
        int _reader_type;
        QByteArray _reader_uid;
        MifareCards _reader_tcard;
        bool _lf_memory_supported;

        CardState card_state;
        QQueue<PendingOP> _queue;
        bool reader_busy;
        QList<int> blocks_skipped;

        void queuePop();
        void queueAbort();
};

#endif // RFIDCARDREADEROPERATOR_H
