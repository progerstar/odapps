#ifndef HIDDFU_RUNNER_H
#define HIDDFU_RUNNER_H

#include <QObject>
#include <QVariant>
#include <QFile>
#include <QThread>
#include <hidproxy.h>

#include <stdint.h>
#include <atomic>

#define SB_Erasing    (uint8_t(0x01))
#define SB_Writing    (uint8_t(0x02))
#define SB_LauchSDFU  (uint8_t(0x04))
#define SB_LauchDFU   (uint8_t(0x08))
#define SB_Reading    (uint8_t(0x10))
#define SB_SelStorage (uint8_t(0x20))
#define SB_WaitLaunch (uint8_t(0x40))
#define SB_Launched   (uint8_t(0x80))

class UARTCommunicator;

struct DFUStatus {
    uint8_t state;
    uint8_t error;
    uint32_t address;
    uint32_t maxsize;
    uint32_t id;
};

Q_DECLARE_METATYPE(DFUStatus);

enum OD_DFU_ERRORS {
    OD_DFU_ERROR_OK       = 0, /*No Error*/
    OD_DFU_ERROR_FLASHOP  = 1, /*Flash operation failed / timed out etc*/
    OD_DFU_ERROR_BUSY     = 2, /*Requested the next operation before the previous one has completed (should not happen)*/
    OD_DFU_ERROR_ADDRESS  = 3, /*Requested read/write from a wrong address*/
    OD_DFU_ERROR_ALIGN    = 4, /*Requested unaligned flash write*/
    OD_DFU_ERROR_NOERASE  = 5, /*Requested flash write without previous erase*/
    OD_DFU_ERROR_PROTECT  = 6, /*Flash is read/write protected (aka Level2 protection - the chip is DEAD, long live the chip*/
    OD_DFU_ERROR_WCRC     = 7, /*Flash write verification failed*/
    OD_DFU_ERROR_OVERRUN  = 8, /*New operation requested while previous is not finished*/
};

void dumpStatus(const DFUStatus *status);
QString dfu_error_string(uint8_t err);

enum WDT_HID_COMMANDS {
    HID_SET_ADDR        = 1,
    HID_ERASE           = 2,
    HID_BOOT_SDFU       = 3,
    HID_BOOT_UDFU       = 4,
    HID_BOOT_APP        = 5,
    /* for ~E bootloaders */
    HID_ERASE_STORAGE   = 6,
    HID_STORAGE_ADDRESS = 7,
    HID_READ_STORAGE    = 8,
    HID_WRITE_STORAGE   = 9,
    /* for ~S bootloaders */
    HID_ERASE_EEPROM    = 0x10,
    /* for UART bootloaders */
    HID_SET_PARAMS      = 0x80
};

enum HID_REPORT_IDS {
    HID_ID_CMD  = 1,
    HID_ID_DATA = 2,
    HID_ID_STAT = 3,
    HID_ID_INFO = 4,
};

/*~E bootloaders*/
#define  HID_ID_SPI   (5)

/* UART bootloaders*/
#define HID_ID_CFG    (5)
#define HID_ID_SERIAL (6)


#ifdef Q_OS_ANDROID
#define HIDDFU_DEFAULT_TIMEOUT (2000)
#else
#define HIDDFU_DEFAULT_TIMEOUT (1000)
#endif

typedef QPair<uint32_t, uint32_t> LocationTypeDef;

class hiddfu_worker : public QObject
{
        Q_OBJECT
    public:
        enum Errors {
            ERR_ERASE = 1,
            ERR_COMM_NONF,
            ERR_COMM,
            ERR_READ,
            ERR_FLASH,
            ERR_DFU,
            /*ERR_NOTIFY, - use INotify*/
        };
        Q_ENUM(Errors)

        enum Commands {
            Setup,
            Status,
            Info,
            CheckFW,
            Flash,
            FlashStor,
            ReadStor,
            EraseStor,
            EraseEEPROM,
            Reboot,
        };
        Q_ENUM(Commands)

        enum IType {
            ISerial,
            INotify,
            IStatus,
            IDescr,
            IDfuDescr,
        };
        Q_ENUM(IType)

        enum FlashResult {
            FSuccess,
            FError,
            FFallback,
        };

        struct StorageTableEntry {
            uint32_t address;
            uint32_t offset;
            uint32_t size;
        };

        virtual ~hiddfu_worker(){}
    signals:
        void progress(int val);
        void error(int code, const QVariant& param);
        void info(int type, const QVariant& param);
        void finished(int cmd, bool status);
    public slots:
        virtual void setup(const QVariantMap& params) = 0;
        virtual void getstatus() = 0;
        virtual void getinfo() = 0;
        virtual void checkfw(const QString& file);

        virtual void flash(const QString& source) = 0;
        virtual void reboot(int kind) = 0;
#ifndef OD_NO_DEVELOPER
        virtual void storage_erase() = 0;
        virtual void storage_write(const QString& source, int index = -1) = 0;
        virtual void storage_read(const QString& source, const QList<LocationTypeDef>& locations = QList<LocationTypeDef>(), const QString& info = QString()) = 0;
#endif
        virtual void eeprom_erase() = 0;
        inline virtual void abort() {
            force_stop.store(true, std::memory_order_relaxed);
        }
    protected:
        hiddfu_worker(QObject* parent = nullptr) : QObject(parent), force_stop(false){}
        std::atomic_bool force_stop;

        virtual bool parseDfuHeader(QFile* dfu_file, QString& info, QString* errorString);
        virtual bool parseStorageHeader(QFile* dfu_file, QString& info, QList<StorageTableEntry>& vtable, QString* errorString = nullptr);
        virtual bool createStorageHeader(QFile* dfu_file, const QString& info, const QList<LocationTypeDef>& locations);
        virtual bool getStatus(DFUStatus* status) = 0;
        virtual bool postConnectionRoutine(const QString& sn);
        virtual bool preOperationRoutine(Commands cmd);
        virtual bool preFlashRoutine(QFile* dfu_file);
        virtual bool verifyAddressSet(DFUStatus& status, uint32_t address, int timeout);
};

class hiddfu_runner : public QObject
{
        Q_OBJECT
        Q_PROPERTY(Mode mode READ mode)
        Q_PROPERTY(bool connected READ isConnected)
        Q_PROPERTY(bool busy READ isBusy)
        QThread workerThread;
    public:
        enum Mode {
            USB,
            UART,
        };
        Q_ENUM(Mode)

        explicit hiddfu_runner(Mode m, QObject* parent = nullptr);
        ~hiddfu_runner();

        Mode mode() const {
            return dfumode;
        }

        bool isConnected() const {
            return connected;
        }

        bool isBusy() const {
            return busy;
        }

    signals:
        void progress(int val);
        void info(int type, const QVariant& param);
        void error(int code, const QVariant& param);
        void result(int cmd, bool success);
    public slots:
        void exec(int command, const QVariantMap &params = QVariantMap());
        void abort();
    private slots:
        void done(int cmd, bool status);
        void threadFinished();
    private:
        Mode dfumode;
        bool busy;
        bool connected;

        hiddfu_worker *worker;
};

#endif // HIDDFU_RUNNER_H
