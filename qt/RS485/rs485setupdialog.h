#ifndef RS485SETUPDIALOG_H
#define RS485SETUPDIALOG_H

#include <QDialog>
#include <QHash>

namespace Ui {
class RS485SetupDialog;
}

class RS485SetupDialog : public QDialog
{
        Q_OBJECT
        Q_PROPERTY(bool scanning READ scanning WRITE setScanning)
    public:
        enum AddressingMode {
            Addressing_RS458,
            Addressing_MODBUS,
        };
        Q_ENUM(AddressingMode)

        enum StandardBaudrates {
            BAUD_300 = 0,
            BAUD_600,
            BAUD_1200,
            BAUD_2400,
            BAUD_4800,
            BAUD_9600,
            BAUD_14400,
            BAUD_19200,
            BAUD_38400,
            BAUD_57600,
            BAUD_74880,
            BAUD_115200,
            BAUD_128000,
            BAUD_230400,
            BAUD_256000,
            BAUD_460800,
            BAUD_921600,
            BAUD_LAST = BAUD_921600,
        };
        Q_ENUM(StandardBaudrates)

        static quint32 standardBaudrate(quint16 rate);

        explicit RS485SetupDialog(QWidget *parent = nullptr);
        ~RS485SetupDialog();

        void readSettings(const QString& prefix = "RS485");
        void writeSettings(const QString& prefix = "RS485");

        bool scanning() const;
        void setScanning(bool on);

        AddressingMode addressingMode() const
        {
            return amode;
        }
        void setAddressingMode(RS485SetupDialog::AddressingMode mode);

        QHash<QString,QVariant> get() const;
        void set(const QHash<QString, QVariant>& cfg);
    private:
        Ui::RS485SetupDialog *ui;
        AddressingMode amode;
};

#endif // RS485SETUPDIALOG_H
