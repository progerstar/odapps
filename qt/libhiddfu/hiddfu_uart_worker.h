#ifndef HIDDFU_UART_WORKER_H
#define HIDDFU_UART_WORKER_H

#include <hiddfu_runner.h>

class hiddfu_uart_worker : public hiddfu_worker
{
        Q_OBJECT
    public:
        hiddfu_uart_worker(QObject* parent = nullptr);
        virtual ~hiddfu_uart_worker();
    public slots:
        virtual void setup(const QVariantMap& params) override;
        virtual void getstatus() override;
        virtual void getinfo() override;
        virtual void flash(const QString& source) override;
        virtual void reboot(int kind) override;
#ifndef OD_NO_DEVELOPER
        virtual void storage_erase() override;
        virtual void storage_write(const QString& source, int index = -1) override;
        virtual void storage_read(const QString& source, const QList<LocationTypeDef>& locations = QList<LocationTypeDef>(), const QString& info = QString()) override;
#endif
        virtual void eeprom_erase() override;
    protected:
        virtual bool postConnectionRoutine(const QString& sn) override;
        virtual bool getStatus(DFUStatus* status) override;
    private:
        UARTCommunicator* _io;
        bool busy;

        bool busyWait();
        bool execCmd(uint8_t cmd);
        bool erase_op(Commands op, uint8_t hid_cmd);
};

#endif // HIDDFU_UART_WORKER_H
