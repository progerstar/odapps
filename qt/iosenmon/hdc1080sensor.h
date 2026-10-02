#ifndef HDC1080SENSOR_H
#define HDC1080SENSOR_H

#include <hidsensorinterface.h>

class HDC1080Sensor : public HidSensorInterface
{
        Q_OBJECT
    public:
        enum Source {
            HDC1080_Temp,
            HDC1080_Hum
        };
        Q_ENUM(Source)

        enum Mod {
            HDC1080_1, /*HDC1080*/
            HDC1080_2  /*HDC2080*/
        };
        Q_ENUM(Mod)

        explicit HDC1080Sensor(Mod mod, Source src, QObject* parent = nullptr);
        virtual ~HDC1080Sensor(){}

        Q_INVOKABLE virtual QString type() const Q_DECL_OVERRIDE;
        Q_INVOKABLE virtual int sensorClass() const Q_DECL_OVERRIDE { return (m_src==HDC1080_Temp) ? Temperature : Humidity;}
        Q_INVOKABLE virtual QString unit() const Q_DECL_OVERRIDE;
        Q_INVOKABLE virtual inline QString icon() const Q_DECL_OVERRIDE { return ":/icons/hdc1080.ico";}
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
        void parseSettingsBuffer(const uint8_t* set_buf);
        Mod m_mod;
        Source m_src;

        double c_data;
};

#endif // HDC1080SENSOR_H
