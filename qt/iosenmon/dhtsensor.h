#ifndef DHTSENSOR_H
#define DHTSENSOR_H

#include "hdc1080sensor.h"

/*
 * UNITX ODTEMP-1 DHT: temperature/humidity sensor that uses the same HID
 * reports as HDC1080, except the measurement resolution is fixed.
 */
class DHTSensor : public HDC1080Sensor
{
        Q_OBJECT
    public:
        explicit DHTSensor(Source src, QObject* parent = nullptr);
        virtual ~DHTSensor(){}

        Q_INVOKABLE virtual QString type() const Q_DECL_OVERRIDE;
        Q_INVOKABLE virtual inline QString icon() const Q_DECL_OVERRIDE { return ":/icons/hdc1080.ico";}
        Q_INVOKABLE virtual QString uiDescr() const Q_DECL_OVERRIDE;

        virtual double dataMin() const Q_DECL_OVERRIDE;
        virtual double dataMax() const Q_DECL_OVERRIDE;

        Q_INVOKABLE virtual QVariantMap readSettings() Q_DECL_OVERRIDE;
        Q_INVOKABLE virtual bool writeSettings(const QVariantMap& feature) Q_DECL_OVERRIDE;

    private:
        int fixed_t_res;
        int fixed_h_res;
};

#endif // DHTSENSOR_H
