#include "ds18b20sensor.h"
#include <QFile>
#include <QDebug>
#include <QSettings>

#include <endianrw.h>

#include "iosenmon_global.h"

DS18B20Sensor::DS18B20Sensor(const QString& model, QObject* parent) : HidSensorInterface(parent), c_data(0.), m_model(model)
{

}

QString DS18B20Sensor::uiDescr() const
{
    /*NTC10K "resolution" is the ADC averaging count, so it has its own dialog*/
    QFile descr(m_model == QLatin1String("NTC10K") ? ":/ui/ntc10k.json" : ":/ui/ds18b20.json");
    descr.open(QFile::ReadOnly);
    return QString::fromUtf8(descr.readAll());
}

QString DS18B20Sensor::unit() const
{
    return QString("%1").arg(QChar( (QSettings().value(SETTINGS_UNITS_TEMP,"C").toString()=="F") ? 0x2109 : 0x2103));
}

double DS18B20Sensor::dataMin() const
{
    const double tmin = isNst1001() ? -50.0 : -55.0;
    return (QSettings().value(SETTINGS_UNITS_TEMP,"C").toString()=="F") ? (tmin * 9/5. + 32) : tmin;
}

double DS18B20Sensor::dataMax() const
{
    const double tmax = isNst1001() ? 150.0 : 125.0;
    return (QSettings().value(SETTINGS_UNITS_TEMP,"C").toString()=="F") ? (tmax * 9/5. + 32) : tmax;
}

QVariantMap DS18B20Sensor::setupUI()
{
    QVariantMap ret;
    bool toFahrenheit = (QSettings().value(SETTINGS_UNITS_TEMP,"C").toString()=="F");

    if(!get_feature(HID_CMD_REPORT_ID))
    {
        qWarning()<<"Cannot get DS18B20 settings - feature request failed";
        return ret;
    }

    qWarning()<<"DS18B20 settings: "<<QString::fromLatin1(QByteArray((const char*)(hid_report_in+1),15).toHex().toUpper());

    double temp = double(static_cast<int16_t>(read_uint16_le(hid_report_in+1+7)))/100.;
    ret.insert("normal_min",toFahrenheit ? (temp*9/5.+32) : temp);

    temp = double(static_cast<int16_t>(read_uint16_le(hid_report_in+1+7+2)))/100.;
    ret.insert("normal_max",toFahrenheit ? (temp*9/5.+32) : temp);
    temp = double(static_cast<int16_t>(read_uint16_le(hid_report_in+1+7+2+2)))/100.;
    ret.insert("accept_min",toFahrenheit ? (temp*9/5.+32) : temp);
    temp = double(static_cast<int16_t>(read_uint16_le(hid_report_in+1+7+2+2+2)))/100.;
    ret.insert("accept_max",toFahrenheit ? (temp*9/5.+32) : temp);
    ret.insert("mstepSize",toFahrenheit ? 10 : 5);
    return ret;
}

QVariantMap DS18B20Sensor::readSettings()
{
    QVariantMap ret;
    if(!get_feature(HID_CMD_REPORT_ID))
    {
        qWarning()<<"Cannot get DS18B20 settings - feature request failed";
        return ret;
    }

    qWarning()<<"DS18B20 settings: "<<QString::fromLatin1(QByteArray((const char*)(hid_report_in+1),15).toHex().toUpper());

    uint8_t* set_buf = hid_report_in+1;
    ret.insert("interval",read_uint32_le(set_buf)/1000.);
    set_buf+=4;
    ret.insert("resolution",int(*set_buf++));
    ret.insert("threshold",read_uint16_le(set_buf)/100.);

    set_buf+=2;
    state_descr.normal_min = double(static_cast<int16_t>(read_uint16_le(set_buf)))/100.;
    set_buf+=2;
    state_descr.normal_max = double(static_cast<int16_t>(read_uint16_le(set_buf)))/100.;
    set_buf+=2;
    state_descr.accept_min = double(static_cast<int16_t>(read_uint16_le(set_buf)))/100.;
    set_buf+=2;
    state_descr.accept_max = double(static_cast<int16_t>(read_uint16_le(set_buf)))/100.;

    ret.insert("norm_min",state_descr.normal_min);
    ret.insert("norm_max",state_descr.normal_max);
    ret.insert("accept_min",state_descr.accept_min);
    ret.insert("accept_max",state_descr.accept_max);
    return ret;
}

bool DS18B20Sensor::writeSettings(const QVariantMap& feature)
{
    if(!handle)
    {
        qWarning()<<"Cannot write settings - handle is NULL";
        return false;
    }

    qWarning()<<"New DS18B20 settings "<<feature;

    hid_report_out[0] = HID_CMD_REPORT_ID;
    uint8_t* set_buf = hid_report_out+1;
    write_uint32_le(static_cast<uint32_t>(qRound(feature.value("interval").toDouble()*1000.)),set_buf);
    set_buf+=4;
    *set_buf++ = quint8(feature.value("resolution").toInt());

    int rtemp = qRound(feature.value("threshold").toDouble()*100);
    write_uint16_le(quint16(rtemp),set_buf);
    set_buf+=2;

    state_descr.normal_min = feature.value("norm_min").toDouble();
    rtemp = qRound(state_descr.normal_min*100);
    write_uint16_le(static_cast<uint16_t>(int16_t(rtemp)),set_buf);
    set_buf+=2;

    state_descr.normal_max = feature.value("norm_max").toDouble();
    rtemp = qRound(state_descr.normal_max*100);
    write_uint16_le(static_cast<uint16_t>(int16_t(rtemp)),set_buf);
    set_buf+=2;

    state_descr.accept_min = feature.value("accept_min").toDouble();
    rtemp = qRound(state_descr.accept_min*100);
    write_uint16_le(static_cast<uint16_t>(int16_t(rtemp)),set_buf);
    set_buf+=2;

    state_descr.accept_max = feature.value("accept_max").toDouble();
    rtemp = qRound(state_descr.accept_max*100);
    write_uint16_le(static_cast<uint16_t>(int16_t(rtemp)),set_buf);

    qWarning()<<"Set DS18B20 settings to "<<QString::fromLatin1(QByteArray((const char*)(hid_report_out+1),15).toHex().toUpper());
    if(hid_send_feature_report(handle,hid_report_out,16)<16)
    {
        qWarning()<<"Failed to send feature report - settings not set";
        return false;
    }
    emit configChanged();
    return true;
}

double DS18B20Sensor::processDataReport(const QByteArray& payload)
{
    c_data = (payload.size()>=2) ? (static_cast<int16_t>(read_uint16_le(payload.constData()))/100.00) : 0.;
    State new_state = state_descr.state(c_data);
    if((sens_state<=Critical)&&(new_state != sens_state))
    {
        sens_state = new_state;
        emit stateChanged(int(sens_state));
    }
    return (QSettings().value(SETTINGS_UNITS_TEMP,"C").toString()=="F") ? (c_data * 9/5. + 32) : c_data;
}

void DS18B20Sensor::processCustomCommand(quint8 cmd, const QByteArray& payload)
{
    Q_UNUSED(cmd);
    Q_UNUSED(payload);
}
