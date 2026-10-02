#ifndef HIDSENSORINTERFACE_H
#define HIDSENSORINTERFACE_H

#include <QObject>
#include <QSet>
#include <QTimerEvent>
#include <QTimer>
#include <QVariantMap>

#include <hidapi.h>

#define HID_DATA_REPORT_ID   (1)
#define HID_EVENT_REPORT_ID  (2)
#define HID_FW_REPORT_ID     (3)
#define HID_CMD_REPORT_ID    (4)
#define HID_CMD_REPORT_SIZE  (7)

class HidSensorInterface : public QObject
{
        Q_OBJECT
        Q_PROPERTY(double minimumValue READ dataMin)
        Q_PROPERTY(double maximumValue READ dataMax)
        Q_PROPERTY(double value READ getData)
        Q_PROPERTY(int state READ getState NOTIFY stateChanged)
    public:
        enum Class {
            Temperature,
            Humidity,
        };
        Q_ENUM(Class)

        enum Display {
            Text,
            LCD,
            Gauge,
            LED,
            Switch,
            CustomDisplay
        };
        Q_ENUM(Display)

        enum State {
            Normal = 0,
            Acceptable = 1,
            Critical = 2,
            InternalError = 3,
            CustomState = 4
        };
        Q_ENUM(State)

        enum Status {
            Standalone,
            Master,
            Slave,
        };
        Q_ENUM(Status)

        struct SensorStateDescr {
            double accept_min;
            double normal_min;
            double normal_max;
            double accept_max;

            HidSensorInterface::State state(double value) const;
        };

        explicit HidSensorInterface(QObject *parent = nullptr);
        virtual ~HidSensorInterface();

        Q_INVOKABLE static QStringList availableDevices();
        /*models seen as "UNITX ODTEMP-1 <model>" (as opposed to legacy "IOT <model>")*/
        static QSet<QString>& unitxModels();
        Q_INVOKABLE virtual bool open(const QString& name, HidSensorInterface::Status status, HidSensorInterface *relative);

        Q_INVOKABLE virtual QString type() const = 0;
        Q_INVOKABLE virtual int sensorClass() const = 0;
        Q_INVOKABLE virtual QString unit() const = 0;
        Q_INVOKABLE virtual QString icon() const = 0;
        Q_INVOKABLE virtual Display uiClass() const = 0;
        Q_INVOKABLE virtual QString uiDescr() const = 0;
        virtual double dataMin() const = 0;
        virtual double dataMax() const = 0;
        virtual double getData() const { return data;}
        virtual double getCurrentData();
        virtual double getMetricData() const {
            return data;
        }
        virtual quint8 getState() const { return sens_state;}
        Q_INVOKABLE virtual QVariantMap setupUI() { return QVariantMap();}

        Q_INVOKABLE virtual QString serial() const;
        Q_INVOKABLE virtual QString firmware();

        Q_INVOKABLE virtual QString settingsDialog() const;
        Q_INVOKABLE virtual QVariantMap readSettings() = 0;
        Q_INVOKABLE virtual bool writeSettings(const QVariantMap& feature) = 0;
    signals:
        void dataChanged(double value);
        void stateChanged(int type);
        void configChanged();
        void error(const QString& descr);
    public slots:
        void command(int cmd, const QByteArray& params = QByteArray());

        void reset();
        void upgrade();
#ifndef OD_NO_DEVELOPER
        void dfu();
#endif
    protected:
        QString uuid;
        QString fwver;
        double  data;
        quint8  sens_state;
        SensorStateDescr state_descr;

        Status instance_status;
        /*
         * Only meaningful for Standalone/Master
         */
        hid_device* handle;
        /*
         * Standalone: meaningless (0,0)
         * Master: master=Null, slave - the first slave device
         * Slave: master - the previous slave or master, slave - the next slave or null
         */
        HidSensorInterface* master;
        HidSensorInterface* slave;
        int tID;
        int reply_timeout;
        unsigned char hid_report_out[64];
        unsigned char hid_report_in[64];

        virtual void timerEvent(QTimerEvent* e) Q_DECL_OVERRIDE;
        virtual int get_feature(quint8 id, unsigned char* output = 0);

        void dataReportReceived(const QByteArray& payload);
        void   customCommandReceived(quint8 cmd, const QByteArray& payload);
        virtual double processDataReport(const QByteArray& payload) = 0;
        virtual void processCustomCommand(quint8 cmd, const QByteArray& payload) = 0;
        virtual void sensorStateChanged(uint8_t state);

        void emit_error(const QString& descr);
        void master_error(const QString& descr);

};

QDebug operator<<(QDebug debug, const HidSensorInterface::SensorStateDescr &descr);

#endif // HIDSENSORINTERFACE_H
