#ifndef RFIDKITWINDOW_H
#define RFIDKITWINDOW_H

#include <qprogressdisplay.h>
#include <mifareblockrawtablemodel.h>

#include "rfidkit_global.h"
#include <rfidcardreaderoperator.h>

#include <QMainWindow>
#include <QCloseEvent>
#include <QModelIndex>
#include <QSettings>

#include <QUdpSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QHash>

#include <odhid_global.h>

#define SETTINGS_VIEW_CELLS    "View/Cells"
#define SETTINGS_VIEW_HEADERS  "View/Headers"

class QButtonGroup;
class QActionGroup;

namespace Ui {
class RFIDKitWindow;
}

class RFIDCardReaderInterface;
class MifareKeyDatabase;
class QListWidgetItem;

class ClientID
{
    public:
        ClientID() : address(), port(12345){}
        ClientID(const QHostAddress& a, quint16 p) : address(a.toIPv6Address()),port(p) {}
        ~ClientID(){}

        QHostAddress address;
        quint16 port;

        inline QString toString() const { return address.toString()+"@"+QString::number(port);}
};

inline bool operator==(const ClientID& id1, const ClientID& id2)
{
    return ((memcmp(id1.address.toIPv6Address().c,id2.address.toIPv6Address().c,16)==0)&&
            (id1.port==id2.port));
}

inline uint qHash(const ClientID& id)
{
    return qHash(QByteArray::fromRawData(reinterpret_cast<const char*>(id.address.toIPv6Address().c),16),id.port);
}

class TcpClient : public QObject
{
        Q_OBJECT
    public:
        explicit TcpClient(QTcpSocket* sock, QObject* parent = nullptr) : QObject(parent), fd(sock)
        {
            connect(sock,SIGNAL(readyRead()),this,SLOT(tcpReadyRead()));
            connect(sock,SIGNAL(disconnected()),this,SLOT(tcpDisconnected()));
        }

        ~TcpClient()
        {
            if(fd)
            {
                fd->disconnect(this);
                if(fd->state() != QAbstractSocket::UnconnectedState)
                {
                    fd->abort();
                }
                delete fd;
                fd = nullptr;
            }
        }
        QTcpSocket* fd;
        QByteArray buffer;
    signals:
        void newDatagram(const QByteArray& data);
        void closed();
    private slots:
        void tcpReadyRead();
        void tcpDisconnected();
};

class RFIDKitWindow : public QMainWindow
{
        Q_OBJECT

        enum StackIndex {
            SI_Classic      = 0,
            SI_Ultralight   = 1,
            SI_EmMarine     = 2,
            SI_NotSupported = 3
        };

        enum ULStackIndex {
            USI_None = 0,
            USI_EV1  = 1,
            USI_C    = 2
        };

    public:

        explicit RFIDKitWindow(QWidget *parent = nullptr);
        ~RFIDKitWindow();

        void setDevMode(bool on);
        QString appVersion() const {
            return ODRFIDKIT_VERSION;
        }

        void readSettings(bool init);
        void writeSettings();

        BootMode exitMode(QString& dfuTransport, QStringList &dfuProducts) const;
    protected:
        void closeEvent(QCloseEvent* ev);
    private slots:
        void about();
        void on_actionConnect_triggered();
        void on_actionNext_Tag_triggered();

        void readerValid(bool on);
        void readerVersionChanged(const QString& v, const QVersionNumber &vn);
        void readerPasswordChanged(bool success, quint32 pwd);
        void operatorError(RFIDCardReaderOperator::Level lev, const QString& title, const QString& msg);
        void cardDetected(const QByteArray& uid, int type);
        void cardStateChanged(RFIDCardReaderOperator::CardState state);
        void on_modUIDTool_clicked();
        void on_actionCopy_UID_triggered();

        void on_actionRead_Tag_triggered();
        void on_actionWrite_Tag_triggered();
        void on_actionLF_Memory_triggered();

        //void on_mfcSectorCombo_currentIndexChanged(int i);
        void on_sectorList_currentRowChanged(int currentRow);
        //void on_sectorList_currentItemChanged(QListWidgetItem *current, QListWidgetItem *previous);

        void mfcKeyTypeSwitched(int key);
        void on_mfcValueButton_clicked();
        void on_sectorTable_doubleClicked(const QModelIndex &index);

        void on_ulevPwdEdit_clicked();
        void on_ulSaveButton_clicked();

        void on_emCloneButton_clicked();

        void on_actionEmulator_Open_triggered();
        void on_actionEmulator_New_triggered();
        void on_actionEmulator_Close_triggered();

        void on_actionAuto_Mode_triggered();
        void on_actionFWUpdate_triggered();

        void on_actionPreferences_triggered();

        void formatActionTriggered(QAction* act);
        void headerFormatActionTriggered(QAction* act);

        void on_actionImport_Theme_triggered();

        void led_timeout();

        void cardOpened(bool success, const QString& error);
        void card_uidChanged();
        void card_packReceived(bool success, quint16 pack);
        void card_blockRead(int number, QByteArray data, int result);
        //void card_readFinished();
        void card_blockWritten(int address, int code);
        //void card_writeFinished(bool success);
        void reader_misc(int code, const QVariant& data);

        //net
        void udp_ready_read();

        void tcp_newConnection();
        void tcp_clientClosed();
        void tcp_clientMessage(const QByteArray& data);

        void warnAccessBits();
        void warnKey(const QString& key, const QString& bogusBlocks);

    private:
        Ui::RFIDKitWindow *ui;
        BootMode exit_code;
        quint32 exit_reader_subtype;

        RFIDCardReaderOperator* _op;
        QVersionNumber _reader_version;
        QSettings set;

        MifareBlockRawTableModel* mfc_model;
        MifareSectorInterface*    mfc_sector;

        QActionGroup* fmtActGroup;
        QActionGroup* hdrActGroup;

        QProgressDisplay* progress_display;

        QTimer ledTimer;

        QByteArray last_uid;

        void connectOperator();

        /****Card specific******************/
        ///Classic
        MifareKeyDatabase* key_db;

        QString mifare_classic_skippedBlocks;
        bool mifare_classic_mod_trailer;
        bool ultralight_password_rejected;
        /***********************************/

        void emulateCardCore(MifareCards card);
        void clearModel();
        void currentReaderInvalidated();

        RFIDCardReaderOperator::OpCode warnDeadLock(int block);
        RFIDCardReaderOperator::OpCode warnDeadLockBlocks(const QVector<int>& blocks);

        void mfc_changeSector_classic(int sector);
        void mfc_changeSector_plus(int sector);

        void mfc_classic_read();
        void mfc_plus_read();
        void mfc_write();

        void ul_read();
        void ul_write();

        void mifareclassic_readFinished();
        void mifareul_readFinished();
        void mifareul_ext_readFinished();

        void classic_block_written(int number, bool written);
        void classic_write_finished(bool success);
        void ultralight_write_finished(bool success);

        void card_read_done();
        void card_write_done(bool success);

        void setTagToolsEnabled(bool on);
        /*************UDP Server************/
        QUdpSocket m_udp;
        class UdpClient
        {
            public:
                explicit UdpClient(const QByteArray& dt = QByteArray()) :
                    buffer(dt),init(false) {}
                QByteArray buffer;
                bool init;
        };

        QHash<ClientID,UdpClient> udp_clients;
        void processUdpDatagram(const ClientID& client);
        void closeUDP();
        void udpBroadcast(const QByteArray& data);
        /***********************************/

        /*************TCP Server*************/
        QTcpServer* m_tcp;

        QMap<qintptr,TcpClient*> tcp_clients;

        void tcpStart();
        void tcpStop();
        void tcpBroadcast(const QByteArray& data);
        /************************************/
};

#endif // RFIDKITWINDOW_H
