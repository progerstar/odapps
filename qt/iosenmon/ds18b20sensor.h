#ifndef DS18B20SENSOR_H
#define DS18B20SENSOR_H

#include <hidsensorinterface.h>

class DS18B20Sensor : public HidSensorInterface
{
        Q_OBJECT
    public:
        explicit DS18B20Sensor(const QString& model = QStringLiteral("DS18B20"), QObject* parent = nullptr);
        virtual ~DS18B20Sensor(){}

        Q_INVOKABLE virtual inline QString type() const Q_DECL_OVERRIDE { return m_model;}
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
        bool isNst1001() const { return m_model == QLatin1String("NST1001") || m_model == QLatin1String("NTS1001"); }
        virtual double processDataReport(const QByteArray& payload) Q_DECL_OVERRIDE;
        virtual void processCustomCommand(quint8 cmd, const QByteArray& payload) Q_DECL_OVERRIDE;
        double c_data;
        QString m_model; /*DS18B20, NST1001, NTC10K...: all use the same HID reports*/
};

#endif // DS18B20SENSOR_H
