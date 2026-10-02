#ifndef DS18B20SENSOR_H
#define DS18B20SENSOR_H

#include <hidsensorinterface.h>

class DS18B20Sensor : public HidSensorInterface
{
        Q_OBJECT
    public:
        explicit DS18B20Sensor(QObject* parent = nullptr);
        virtual ~DS18B20Sensor(){}

        Q_INVOKABLE virtual inline QString type() const Q_DECL_OVERRIDE { return "DS18B20";}
        Q_INVOKABLE virtual int sensorClass() const Q_DECL_OVERRIDE { return Temperature;}
        Q_INVOKABLE virtual QString unit() const Q_DECL_OVERRIDE;
        Q_INVOKABLE virtual inline QString icon() const Q_DECL_OVERRIDE { return ":/icons/ds18b20.ico";}
        Q_INVOKABLE virtual inline Display uiClass() const Q_DECL_OVERRIDE { return HidSensorInterface::Gauge; }
        Q_INVOKABLE virtual QString uiDescr() const Q_DECL_OVERRIDE;

        virtual double dataMin() const Q_DECL_OVERRIDE;
        virtual double dataMax() const Q_DECL_OVERRIDE;
        virtual double getMetricData() const Q_DECL_OVERRIDE{
            return c_data;
        }
        Q_INVOKABLE virtual QVariantMap setupUI() Q_DECL_OVERRIDE;

        Q_INVOKABLE virtual QVariantMap readSettings() Q_DECL_OVERRIDE;
        Q_INVOKABLE virtual bool writeSettings(const QVariantMap& feature) Q_DECL_OVERRIDE;

    protected:
        virtual double processDataReport(const QByteArray& payload) Q_DECL_OVERRIDE;
        virtual void processCustomCommand(quint8 cmd, const QByteArray& payload) Q_DECL_OVERRIDE;
        double c_data;
};

#endif // DS18B20SENSOR_H
