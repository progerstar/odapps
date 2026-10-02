#ifndef HIDDFU_USB_WORKER_H
#define HIDDFU_USB_WORKER_H

#include <hiddfu_runner.h>

class hiddfu_usb_worker : public hiddfu_worker
{
        Q_OBJECT
    public:
        hiddfu_usb_worker(QObject* parent = nullptr);
        virtual ~hiddfu_usb_worker();
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
        //virtual bool postConnectionRoutine(const QString& sn) override; - no custom actions
        virtual bool getStatus(DFUStatus* status) override;
    private:
        bool flash_bulk(QFile* iofile, uint32_t fw_start, uint32_t fw_size);

#ifndef OD_NO_DEVELOPER
        FlashResult flash_storage_bulk(QFile* iofile, uint32_t size);
        FlashResult flash_storage_read(QFile* iofile, uint32_t size);
        int check_spi_flash();
        bool storage_write_entry(QFile* iofile, const StorageTableEntry& entry, int flash_size);
#endif

    private:
        HIDProxy* _io;

        bool erase_op(Commands op, uint8_t hid_cmd);
};

#endif // HIDDFU_USB_WORKER_H
