#ifndef ODRFIDCONFIG_H
#define ODRFIDCONFIG_H

#include <QMainWindow>
#include <QVersionNumber>
#include <QTimer>
#include <QTimerEvent>
#include <QtSerialPort>
#include <QQueue>

#include <hidapi.h>

#define ODRFIDCFG_VERSION "1.6.0"

namespace Ui {
class ODRFIDConfig;
}

class ODRFIDConfig : public QMainWindow
{
        Q_OBJECT

        enum OP {
            OP_READ,
            OP_WRITE,
            OP_SWITCH,
        };

        enum IFACE {
            I_INV = -1,
            I_HID = 0,
            I_CDC = 1,
        };

        enum CDC_OP {
            CDC_OP_IDLE,
            CDC_OPREAD_INFO,
            CDC_OPREAD_PARAMS,
            CDC_OPWRITE_PARAMS,
            CDC_OPWRITE_FIN,
        };

        enum SCAN_MODE {
            SCAN_MODE_AUTO = 0,
            SCAN_MODE_MANUAL = 1,
        };

    public:
        explicit ODRFIDConfig(QWidget *parent = 0);
        ~ODRFIDConfig();

        QStringList availableDevices() const;
        bool connect(const QString& name);

#ifndef OD_NO_DEVELOPER
    public slots:
#else
    private slots:
#endif
        void on_actionUpgrade_triggered();

    protected:
        void timerEvent(QTimerEvent* e);
    private slots:
        void uart_readyRead();
        void uart_error(QSerialPort::SerialPortError error);
        void uart_nack();

        void on_actionRead_triggered();
        void on_actionWrite_triggered();

        void on_actionSystemLanguage_triggered();
        void on_actionEnglish_triggered();
        void on_actionRussian_triggered();
        void about();
    private:
        Ui::ODRFIDConfig *ui;
        hid_device* handle;
        QSerialPort uhnd;
        IFACE devtype;

        QString dev_fw;
        QVersionNumber dev_fw_num;

        int hTim;
        int reply_timeout;
        unsigned char hid_report_out[64];
        unsigned char hid_report_in[64];

        QTimer uart_watcher;
        QByteArray uart_read_buffer;
        QQueue<QString> cdc_command_queue;

        OP cop;
        CDC_OP cdc_iter;
        bool legacy_block;
        int cmd_iteration;

        int usbmode_cfg;     // USB profile stored in the reader, -1 if unknown
        int usbmode_active;  // profile the reader is running now
        bool usbmode_changed;
        QString cdc_last_cmd;

        bool sendCmdParam(quint8 cmd, quint8 param);
        bool sendPacket(quint8 cmd, const QByteArray& param = QByteArray());
        bool sendAtCommand(const QString& cmd, const QByteArray& param = QByteArray());

        void processCdcPacket(const QByteArray& data);
        void processHidPacket(uint8_t cmd, uint32_t ret_code, const uint8_t* data);

        //////
        void processVersionInfo();
        void prepareCdcReadCommands();
        void queryIndicatorsHid();
        void queryIndicatorsCdc();
        void startParamsQueryHid();
        void startParamsQueryCdc();
        void paramsReadFinalize();
        void offerRestartForUsbMode();
};


#endif // ODRFIDCONFIG_H
