#ifndef HIDDFU_H
#define HIDDFU_H

#include <QWidget>
#include <QFile>
#include <QFutureWatcher>
#include <QProgressDialog>
#include <QTimer>

#include <hidapi.h>
#include <qpathparser.h>

#include "odhid_global.h"
#include "hiddfu_runner.h"
#include "uartcommunicator.h"

namespace Ui {
class HIDDfu;
}

class HIDDfu : public QWidget
{
        Q_OBJECT

        Q_PROPERTY(QString transport READ transport WRITE setTransport)
        Q_PROPERTY(QString medium READ medium WRITE setMedium)
        Q_PROPERTY(quint32 baudrate READ baudrate WRITE setBaudrate)
        Q_PROPERTY(int address READ address WRITE setAddress)

        Q_PROPERTY(QString file READ file WRITE setFile)
    public:
        enum AutoAction {
            Action_None = 0,
            Action_STM = 1
        };
        enum RunMode {
            Mode_IAPP = 0, /* qApp quit on close */
            Mode_DFU  = 1, /* no qApp->quit, emit finished() & autoclose */
            Mode_EMB  = 2 /*no qApp->quit & close, just emit finished() */
        };

        enum Capability {
            NoCapabilities = 0x00,
            SpiStorage     = 0x01,
            EepromStorage  = 0x02,
            MassStorage    = 0x04,
        };
        Q_DECLARE_FLAGS(Capabilities, Capability)

        explicit HIDDfu(QWidget *parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());
        ~HIDDfu();

        void setDevMode(bool on);
        inline BootMode exitMode() { return mode;}

        bool init();

        inline QString transport() const {
            return m_transport;
        }

        inline void setTransport(const QString& value) {
            m_transport = value;
        }

        inline void setProduct(const QString& val) {
            m_products = QStringList()<<val;
        }

        inline void setProducts(const QStringList& alist) {
            m_products = alist;
        }

        inline QString medium() const {
            return m_medium;
        }

        inline void setMedium(const QString& val) {
            m_medium = val;
        }

        inline QString file() const {
            return dfu_file;
        }

        inline void setFile(const QString& fw_file)
        {
            dfu_file = QPathParser::canonicalFilePath(fw_file);
        }

        inline void setAutoAccept(bool on)
        {
            auto_accept = on;
        }

#ifndef OD_NO_DEVELOPER
        inline void setAutoAction(AutoAction act)
        {
            aact = act;
        }
#endif

        inline quint32 baudrate() const {
            return m_baudrate;
        }

        inline void setBaudrate(quint32 baud) {
            m_baudrate = baud;
        }

        inline int address() const {
            return m_address;
        }

        inline void setAddress(int addr) {
             m_address = addr;
        }

#if 0
        inline bool handleValid()
        {
            return ((dfumode==hiddfu_runner::USB) ? (uhandle && uhandle->isOpen()) : (shandle && shandle->isOpen()));
        }

        inline void* handlePtr()
        {
            return (dfumode == hiddfu_runner::USB) ? ((void*)uhandle) : ((void*)shandle);
        }
#endif

    signals:
        void finished();

    signals:
        void thread_exec(int cmd, const QVariantMap& params, QPrivateSignal);
    private slots:
        void connectDfu();

        void on_dfuDropArea_changed(const QString& file);

        void on_launchTool_clicked();
        void on_restartTool_clicked();
        void on_sdfuTool_clicked();
#ifndef OD_NO_DEVELOPER
        void on_eStorTool_clicked();
#endif
        void on_eepromTool_clicked();

        void thread_done(int cmd, bool success);
        void thread_info(int typ, const QVariant& data);
        void thread_error(int code, const QVariant& param);
        void thread_progress(int prog);
    private:
        Ui::HIDDfu *ui;
        RunMode run_mode;
        bool dev_mode;

        Capabilities caps;
        AutoAction aact;
        BootMode mode;

        QString m_transport;
        QStringList m_products;
        QString m_medium;
        quint32 m_baudrate;
        int m_address;
        int reboot_mode;

        hiddfu_runner *runner;
        QString dfu_file;
        QString dfu_file_info;
        bool auto_accept;

        QTimer connectTimer;
        QProgressDialog* connectProgress;
        int connectRetries;

        void connectAccept();
        void connectBail();
        void postConnected();

        bool flash(const QString& file);
        void checkfw_complete();
#ifndef OD_NO_DEVELOPER
        bool erase_storage();
        bool storage_write();
        bool storage_read();
#endif
        bool eeprom_erase();
        void file_restart();
        void flash_finished(bool ok);

        QWidget* msgParent();
};

Q_DECLARE_OPERATORS_FOR_FLAGS(HIDDfu::Capabilities)

#endif // HIDDFU_H
