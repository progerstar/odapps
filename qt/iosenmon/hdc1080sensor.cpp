#include "hdc1080sensor.h"

#include <QFile>
#include <QDebug>
#include <QSettings>

#include <endianrw.h>

#include "iosenmon_global.h"

struct HDC1080_Sensor_FeatureTypeDef {
        uint8_t  report_id;
        uint32_t interval;
        uint8_t  t_resolution;
        uint8_t  h_resolution;
        uint16_t t_change_thresh;
        int16_t  t_normal_min;
        int16_t  t_normal_max;
        int16_t  t_accept_min;
        int16_t  t_accept_max;
        uint16_t h_change_thresh;
        uint8_t  h_normal_min;
        uint8_t  h_normal_max;
        uint8_t  h_accept_min;
        uint8_t  h_accept_max;
} __attribute__((packed));

QDebug operator<<(QDebug debug, const struct HDC1080_Sensor_FeatureTypeDef &c)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << '{'
                    << "id: " << c.report_id
                    <<", interval: " << c.interval
                    <<", t_res: " << c.t_resolution
                    <<", h_res: " << c.h_resolution
                    <<", t_thresh: " << c.t_change_thresh
                    <<", T range: [" << c.t_accept_min <<" - ( "<<c.t_normal_min<<":"<<c.t_normal_max<<") - "<<c.t_accept_max<<"]"
                    <<", h_thresh: " << c.h_change_thresh
                    <<", H range: [" << c.h_accept_min <<" - ( "<<c.h_normal_min<<":"<<c.h_normal_max<<") - "<<c.h_accept_max<<"]"
                    <<'}';

    return debug;
}

HDC1080Sensor::HDC1080Sensor(HDC1080Sensor::Mod mod, HDC1080Sensor::Source src, QObject* parent) :
    HidSensorInterface(parent), m_mod(mod), m_src(src), c_data(0.)
{

}

QString HDC1080Sensor::type() const
{
    return QString((m_mod == HDC1080_2) ? "HDC2080" : "HDC1080") + QString((m_src==HDC1080_Hum) ? "_RH" : "");
}

QString HDC1080Sensor::uiDescr() const
{
    QFile descr(":/ui/hdc1080.json");
    descr.open(QFile::ReadOnly);
    return QString::fromUtf8(descr.readAll());
}

QString HDC1080Sensor::unit() const
{
    return (m_src == HDC1080_Temp) ?
                QString("%1").arg(QChar( (QSettings().value(SETTINGS_UNITS_TEMP,"C").toString()=="F") ? 0x2109 : 0x2103)) :
                QString("%");
}

double HDC1080Sensor::dataMin() const
{
    return (m_src == HDC1080_Temp) ?
                ((QSettings().value(SETTINGS_UNITS_TEMP,"C").toString()=="F") ? (-40.0 * 9/5. + 32) : -40.0) :
                0.0;
}

double HDC1080Sensor::dataMax() const
{
    return (m_src == HDC1080_Temp) ?
                ((QSettings().value(SETTINGS_UNITS_TEMP,"C").toString()=="F") ? (125.0 * 9/5. + 32) : 125.0):
                100.0;
}

#define HDC1080_REPORT_OFFSET       (1)  /*Skip ReportID*/
#define HDC1080_REPORT_IDX_INTERVAL (0)  /*U32LE*/
#define HDC1080_REPORT_IDX_T_RES    (4)  /*U8*/
#define HDC1080_REPORT_IDX_H_RES    (5)  /*U8*/
#define HDC1080_REPORT_IDX_T_THRSH  (6)  /*U16*/
#define HDC1080_REPORT_IDX_T_N_MIN  (8)  /*I16*/
#define HDC1080_REPORT_IDX_T_N_MAX  (10) /*I16*/
#define HDC1080_REPORT_IDX_T_A_MIN  (12) /*I16*/
#define HDC1080_REPORT_IDX_T_A_MAX  (14) /*I16*/
#define HDC1080_REPORT_IDX_H_THRSH  (16) /*U16*/
#define HDC1080_REPORT_IDX_H_N_MIN  (18) /*U8*/
#define HDC1080_REPORT_IDX_H_N_MAX  (19) /*U8*/
#define HDC1080_REPORT_IDX_H_A_MIN  (20) /*U8*/
#define HDC1080_REPORT_IDX_H_A_MAX  (21) /*U8*/

#define HDC1080_CFG_SIZE            (22)

QVariantMap HDC1080Sensor::setupUI()
{
    QVariantMap ret;
    bool toFahrenheit = (QSettings().value(SETTINGS_UNITS_TEMP,"C").toString()=="F");

    if(!get_feature(HID_CMD_REPORT_ID))
    {
        qWarning()<<"Cannot get HDC1080 settings - feature request failed";
        return ret;
    }

    qWarning()<<"HDC1080 settings: "<<QString::fromLatin1(QByteArray((const char*)(hid_report_in+1),15).toHex().toUpper());

    uint8_t* cfg_buffer = hid_report_in + HDC1080_REPORT_OFFSET;
    if(m_src == HDC1080_Temp)
    {
        /*Temperature*/
        double temp = double(static_cast<int16_t>(read_uint16_le(cfg_buffer+HDC1080_REPORT_IDX_T_N_MIN)))/100.;
        ret.insert("normal_min",toFahrenheit ? (temp*9/5.+32) : temp);

        temp = double(static_cast<int16_t>(read_uint16_le(cfg_buffer+HDC1080_REPORT_IDX_T_N_MAX)))/100.;
        ret.insert("normal_max",toFahrenheit ? (temp*9/5.+32) : temp);
        temp = double(static_cast<int16_t>(read_uint16_le(cfg_buffer+HDC1080_REPORT_IDX_T_A_MIN)))/100.;
        ret.insert("accept_min",toFahrenheit ? (temp*9/5.+32) : temp);
        temp = double(static_cast<int16_t>(read_uint16_le(cfg_buffer+HDC1080_REPORT_IDX_T_A_MAX)))/100.;
        ret.insert("accept_max",toFahrenheit ? (temp*9/5.+32) : temp);
        ret.insert("mstepSize",toFahrenheit ? 10 : 5);
    }
    else
    {
        /*Humidity*/
        ret.insert("normal_min", double(cfg_buffer[HDC1080_REPORT_IDX_H_N_MIN]));
        ret.insert("normal_max", double(cfg_buffer[HDC1080_REPORT_IDX_H_N_MAX]));
        ret.insert("accept_min", double(cfg_buffer[HDC1080_REPORT_IDX_H_A_MIN]));
        ret.insert("accept_max", double(cfg_buffer[HDC1080_REPORT_IDX_H_A_MAX]));
        ret.insert("mstepSize", 5);
    }
    return ret;
}

QVariantMap HDC1080Sensor::readSettings()
{
    QVariantMap ret;
    if(!get_feature(HID_CMD_REPORT_ID))
    {
        qWarning()<<"Cannot get HDC1080 settings - feature request failed";
        return ret;
    }

    qWarning()<<"HDC1080 settings: "<<QString::fromLatin1(QByteArray((const char*)(hid_report_in+1),HDC1080_CFG_SIZE).toHex().toUpper());
#ifndef OD_NO_DEBUG
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    {
        HDC1080_Sensor_FeatureTypeDef feature_struct;
        memcpy(&feature_struct, hid_report_in, sizeof(HDC1080_Sensor_FeatureTypeDef));
        qWarning()<<" parsed: "<<feature_struct;
    }
#endif
#endif

    uint8_t* set_buf = hid_report_in+1;
    ret.insert("interval",read_uint32_le(set_buf+HDC1080_REPORT_IDX_INTERVAL)/1000.);
    ret.insert("t_resolution",int(set_buf[HDC1080_REPORT_IDX_T_RES]));
    ret.insert("h_resolution",int(set_buf[HDC1080_REPORT_IDX_H_RES]));
    ret.insert("t_threshold",read_uint16_le(set_buf+HDC1080_REPORT_IDX_T_THRSH)/100.);
    ret.insert("h_threshold",read_uint16_le(set_buf+HDC1080_REPORT_IDX_H_THRSH)/100.);

    ret.insert("t_norm_min",double(static_cast<int16_t>(read_uint16_le(set_buf+HDC1080_REPORT_IDX_T_N_MIN)))/100.);
    ret.insert("t_norm_max",double(static_cast<int16_t>(read_uint16_le(set_buf+HDC1080_REPORT_IDX_T_N_MAX)))/100.);
    ret.insert("t_accept_min",double(static_cast<int16_t>(read_uint16_le(set_buf+HDC1080_REPORT_IDX_T_A_MIN)))/100.);
    ret.insert("t_accept_max",double(static_cast<int16_t>(read_uint16_le(set_buf+HDC1080_REPORT_IDX_T_A_MAX)))/100.);

    ret.insert("h_norm_min",double(set_buf[HDC1080_REPORT_IDX_H_N_MIN]));
    ret.insert("h_norm_max",double(set_buf[HDC1080_REPORT_IDX_H_N_MAX]));
    ret.insert("h_accept_min",double(set_buf[HDC1080_REPORT_IDX_H_A_MIN]));
    ret.insert("h_accept_max",double(set_buf[HDC1080_REPORT_IDX_H_A_MAX]));

    parseSettingsBuffer(set_buf);
    return ret;
}

void HDC1080Sensor::parseSettingsBuffer(const uint8_t* set_buf)
{
    if(m_src == HDC1080_Temp)
    {
        state_descr.normal_min = double(static_cast<int16_t>(read_uint16_le(set_buf+HDC1080_REPORT_IDX_T_N_MIN)))/100.;
        state_descr.normal_max = double(static_cast<int16_t>(read_uint16_le(set_buf+HDC1080_REPORT_IDX_T_N_MAX)))/100.;
        state_descr.accept_min = double(static_cast<int16_t>(read_uint16_le(set_buf+HDC1080_REPORT_IDX_T_A_MIN)))/100.;
        state_descr.accept_max = double(static_cast<int16_t>(read_uint16_le(set_buf+HDC1080_REPORT_IDX_T_A_MAX)))/100.;
    }
    else
    {
        state_descr.normal_min = double(set_buf[HDC1080_REPORT_IDX_H_N_MIN]);
        state_descr.normal_max = double(set_buf[HDC1080_REPORT_IDX_H_N_MAX]);
        state_descr.accept_min = double(set_buf[HDC1080_REPORT_IDX_H_A_MIN]);
        state_descr.accept_max = double(set_buf[HDC1080_REPORT_IDX_H_A_MAX]);
    }

    qWarning()<<"HDCx080 settings ("<<m_src<<"): "<<state_descr;
    if((instance_status != Standalone) && slave && qobject_cast<HDC1080Sensor*>(slave))
    {
        ((HDC1080Sensor*)slave)->parseSettingsBuffer(set_buf);
    }
}

bool HDC1080Sensor::writeSettings(const QVariantMap& feature)
{
    if(!handle)
    {
        if((instance_status == Slave) && master)
        {
            bool res = master->writeSettings(feature);
            if(res)
            {
                emit configChanged();
            }
            return  res;
        }
        qWarning()<<"Cannot write settings - handle is NULL";
        return false;
    }

    qWarning()<<"New HDC1080 settings "<<feature;

    hid_report_out[0] = HID_CMD_REPORT_ID;
    uint8_t* set_buf = hid_report_out+1;
    write_uint32_le(static_cast<uint32_t>(qRound(feature.value("interval").toDouble()*1000.)),set_buf+HDC1080_REPORT_IDX_INTERVAL);
    set_buf[HDC1080_REPORT_IDX_T_RES] = quint8(feature.value("t_resolution").toInt());
    set_buf[HDC1080_REPORT_IDX_H_RES] = quint8(feature.value("h_resolution").toInt());

    int rtemp = qRound(feature.value("t_threshold").toDouble()*100);
    write_uint16_le(quint16(rtemp),set_buf+HDC1080_REPORT_IDX_T_THRSH);
    rtemp = qRound(feature.value("h_threshold").toDouble()*100);
    write_uint16_le(quint16(rtemp),set_buf+HDC1080_REPORT_IDX_H_THRSH);

    rtemp = qRound(feature.value("t_norm_min").toDouble()*100);
    write_uint16_le(static_cast<uint16_t>(int16_t(rtemp)),set_buf+HDC1080_REPORT_IDX_T_N_MIN);
    rtemp = qRound(feature.value("t_norm_max").toDouble()*100);
    write_uint16_le(static_cast<uint16_t>(int16_t(rtemp)),set_buf+HDC1080_REPORT_IDX_T_N_MAX);
    rtemp = qRound(feature.value("t_accept_min").toDouble()*100);
    write_uint16_le(static_cast<uint16_t>(int16_t(rtemp)),set_buf+HDC1080_REPORT_IDX_T_A_MIN);
    rtemp = qRound(feature.value("t_accept_max").toDouble()*100);
    write_uint16_le(static_cast<uint16_t>(int16_t(rtemp)),set_buf+HDC1080_REPORT_IDX_T_A_MAX);

    rtemp = qRound(feature.value("h_norm_min").toDouble());
    set_buf[HDC1080_REPORT_IDX_H_N_MIN] = quint8(rtemp);
    rtemp = qRound(feature.value("h_norm_max").toDouble());
    set_buf[HDC1080_REPORT_IDX_H_N_MAX] = quint8(rtemp);
    rtemp = qRound(feature.value("h_accept_min").toDouble());
    set_buf[HDC1080_REPORT_IDX_H_A_MIN] = quint8(rtemp);
    rtemp = qRound(feature.value("h_accept_max").toDouble());
    set_buf[HDC1080_REPORT_IDX_H_A_MAX] = quint8(rtemp);

    parseSettingsBuffer(set_buf);

    qWarning()<<"Set HDC1080 settings to "<<QString::fromLatin1(QByteArray((const char*)(hid_report_out+1),HDC1080_CFG_SIZE).toHex().toUpper());
#ifndef OD_NO_DEBUG
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    {
        HDC1080_Sensor_FeatureTypeDef feature_struct;
        memcpy(&feature_struct, hid_report_out, sizeof(HDC1080_Sensor_FeatureTypeDef));
        qWarning()<<" parsed: "<<feature_struct;
    }
#endif
#endif

    if(hid_send_feature_report(handle,hid_report_out,(HDC1080_CFG_SIZE+1))<(HDC1080_CFG_SIZE+1))
    {
        qWarning()<<"Failed to send feature report - settings not set";
        return false;
    }
    emit configChanged();
    return true;
}

double HDC1080Sensor::processDataReport(const QByteArray& payload)
{
    /*temperature - I16LE, humidity - I16LE*/
    if(payload.size() >= 4 /*on WINDOWS extra bytes are reported*/)
    {
        /*c_data is always metric (it is logged to the database)*/
        c_data = static_cast<int16_t>(read_uint16_le(payload.constData() + ((m_src == HDC1080_Temp) ? 0 : 2))) / 100.00;
        State new_state = state_descr.state(c_data);
        if((sens_state<=Critical)&&(new_state != sens_state))
        {
            sens_state = new_state;
            emit stateChanged(int(sens_state));
        }
    }
    else
    {
        c_data = 0.;
    }
    return ((m_src == HDC1080_Temp)&&(QSettings().value(SETTINGS_UNITS_TEMP,"C").toString()=="F")) ? (c_data * 9/5. + 32) : c_data;
}

void HDC1080Sensor::processCustomCommand(quint8 cmd, const QByteArray& payload)
{
    Q_UNUSED(cmd);
    Q_UNUSED(payload);
}

