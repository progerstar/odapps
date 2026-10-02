#include "dhtsensor.h"

#include <QFile>
#include <QSettings>

#include "iosenmon_global.h"

DHTSensor::DHTSensor(Source src, QObject* parent) :
    HDC1080Sensor(HDC1080_1, src, parent), fixed_t_res(1), fixed_h_res(1)
{

}

QString DHTSensor::type() const
{
    return QString("DHT") + QString((m_src == HDC1080_Hum) ? "_RH" : "");
}

QString DHTSensor::uiDescr() const
{
    QFile descr(":/ui/dht.json");
    if(!descr.open(QFile::ReadOnly))
    {
        return QString();
    }
    return QString::fromUtf8(descr.readAll());
}

double DHTSensor::dataMin() const
{
    return (m_src == HDC1080_Temp) ?
                ((QSettings().value(SETTINGS_UNITS_TEMP,"C").toString()=="F") ? (-40.0 * 9/5. + 32) : -40.0) :
                0.0;
}

double DHTSensor::dataMax() const
{
    return (m_src == HDC1080_Temp) ?
                ((QSettings().value(SETTINGS_UNITS_TEMP,"C").toString()=="F") ? (125.0 * 9/5. + 32) : 125.0) :
                100.0;
}

QVariantMap DHTSensor::readSettings()
{
    QVariantMap ret = HDC1080Sensor::readSettings();
    /*remember what the device reports: the DHT resolution is not configurable, but we have to send something back*/
    if(ret.contains("t_resolution"))
    {
        fixed_t_res = ret.value("t_resolution").toInt();
        fixed_h_res = ret.value("h_resolution").toInt();
    }
    return ret;
}

bool DHTSensor::writeSettings(const QVariantMap& feature)
{
    QVariantMap fixed = feature;
    fixed.insert("t_resolution", fixed_t_res);
    fixed.insert("h_resolution", fixed_h_res);
    return HDC1080Sensor::writeSettings(fixed);
}
