#ifndef ODRFIDCONFIG_H
#define ODRFIDCONFIG_H

#include <QMainWindow>
#include <QTimer>
#include <QTimerEvent>
#include <QModbusDevice>
#include <QModbusRtuSerialMaster>

#include "commandconsoledialog.h"

#define ODRFIDCFG_VERSION "1.4.0"

namespace Ui {
class ODRFIDConfig;
}
class LogWindow;

class ODRFIDConfig : public QMainWindow
{
        Q_OBJECT

        enum OP {
            OP_READ,
            OP_WRITE,
        };
    public:
        explicit ODRFIDConfig(QWidget *parent = nullptr);
        ~ODRFIDConfig();

        QStringList availableDevices() const;
        bool connect(const QString& name, bool from_settings);

#ifndef OD_NO_DEVELOPER
        int holdingOffset() const {
            return hold_offset;
        }
        void setHoldingOffset(int o) {
            hold_offset = o;
        }
#endif

#ifndef OD_NO_DEVELOPER
    public slots:
#else
    private slots:
#endif
        void on_fwTool_clicked();

    private slots:
        void on_readButton_clicked();
        void on_writeButton_clicked();
        void on_consoleTool_clicked();
        void about();

        void console_commit(const QString& data);

        void connParamChanged();

        void hnd_errorOccurred(QModbusDevice::Error error);
    private:
        Ui::ODRFIDConfig *ui;
        CommandConsoleDialog* console;
        LogWindow* log;
        QModbusRtuSerialMaster* comm;
        OP cop;
        bool conn_params_changed;
        int selected_address;
#ifndef OD_NO_DEVELOPER
        int hold_offset;
#endif

        bool processMessage(const QString& data);

        void reply_failure();
        bool modbusReplyWaitFinished(QModbusReply* r);
        bool dropBuffers(int address);
        bool getPendingBufferSize(int address, int* output);
        bool sendCommand(int address, const QByteArray& data, QByteArray* reply = 0);

        QModbusReply* modbusWrite(const QModbusDataUnit& write, int address);
        QModbusReply* modbusRead(const QModbusDataUnit& read, int address);
        QModbusReply* modbusRaw(const QModbusRequest& request, int address);
};

#endif // ODRFIDCONFIG_H
