#include "hidsensorinterface.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QHash>
#include <QVariant>
#include <QFile>
#include <QSettings>
#include <QSet>
#include <QRegExp>
#include <QVector>
#include <cstring>

#include <QDebug>

#include "iosenmon_global.h"
#include <qi18n.h>

#define OD_VID               0x0483
#define OD_IOT_PID           0xA26A


typedef enum {
    HID_CMD_RST_UAPP = 0xF0,
    HID_CMD_RST_DFU  = 0xF1,
    HID_CMD_RST_STM  = 0xFA,
} HID_CMD_ID;

QDebug operator<<(QDebug debug, const HidSensorInterface::SensorStateDescr &descr)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << '[' << descr.accept_min << " - (" << descr.normal_min << ':' << descr.normal_max << ") - " << descr.accept_max << ']';
    (void)saver;
    return debug;
}

HidSensorInterface::State HidSensorInterface::SensorStateDescr::state(double value) const
{
    if((accept_min<normal_min)&&(normal_min<normal_max)&&(normal_max<accept_max))
    {
        if(value<accept_min)
        {
            return Critical;
        }
        else if(value<normal_min)
        {
            return Acceptable;
        }
        else if(value<=normal_max)
        {
            return Normal;
        }
        else if(value<=accept_max)
        {
            return Acceptable;
        }
        else
        {
            return Critical;
        }
    }
    return Normal;
}

HidSensorInterface::HidSensorInterface(QObject *parent) :
    QObject(parent),
    data(0.),sens_state(Normal),
    state_descr{0., 0., 0., 0.},
    instance_status(Standalone),
    handle(0), master(0), slave(0),
    tID(0), reply_timeout(0)
{
    memset(hid_report_out, 0, sizeof(hid_report_out));
    memset(hid_report_in, 0, sizeof(hid_report_in));

}

HidSensorInterface::~HidSensorInterface()
{
    if(tID>0)
    {
        killTimer(tID);
    }
    switch(instance_status)
    {
        case Standalone:
        {
            if(handle)
            {
                hid_close(handle);
                handle = 0;
            }
            break;
        }
        case Master:
        {
            if(slave)
            {
                slave->instance_status = Master;
                slave->handle = handle;
                handle = 0;
                slave = 0;
            }
            else
            {
                if(handle)
                {
                    hid_close(handle);
                    handle = 0;
                }
            }
            break;
        }
        case Slave:
        {
            if(master)
            {
                master->slave = slave;
                slave = 0;
                master = 0;
            }
            break;
        }
    }
}

QSet<QString>& HidSensorInterface::unitxModels()
{
    static QSet<QString> models;
    return models;
}

QStringList HidSensorInterface::availableDevices()
{
    QStringList ret;
    QSet<QString> snRegistry;
    QRegExp iotProduct("IOT ([^\\s]+)(?: HID)?");
    /* ODTEMP-1 variants report themselves as "UNITX ODTEMP-1 <model> [HID]": DS18B20, DHT, NTC10K, NST1001... */
    QRegExp unitxProduct("UNITX ODTEMP-1 (\\S+)(?: HID)?");

    struct hid_device_info *devs, *cur_dev;
    devs = hid_enumerate(OD_VID, OD_IOT_PID);
    cur_dev = devs;
    while (cur_dev)
    {
        /* hidapi may return NULL strings */
        QString vendor  = cur_dev->manufacturer_string ? QString::fromWCharArray(cur_dev->manufacturer_string) : QString();
        QString product = cur_dev->product_string ? QString::fromWCharArray(cur_dev->product_string) : QString();
        QString sn      = cur_dev->serial_number ? QString::fromWCharArray(cur_dev->serial_number) : QString();
        qWarning()<<"OD HID "<<vendor<<" / "<<product<<" @ "<<sn;
        QString model;
        if(vendor=="Open Development")
        {
            if(iotProduct.exactMatch(product))
            {
                model = iotProduct.cap(1);
            }
            else if(unitxProduct.exactMatch(product))
            {
                model = unitxProduct.cap(1);
                unitxModels().insert(model);
            }
        }
        if(!model.isEmpty())
        {
            if(!snRegistry.contains(sn))
            {
                ret.append(model + QStringLiteral(" (") + (!sn.isEmpty() ? sn : QString("S/N") ) + QStringLiteral(")"));
                snRegistry.insert(sn);
            }
        }
        else
        {
            qWarning()<<"Not a OD HID IOT";
        }
        cur_dev = cur_dev->next;
    }
    hid_free_enumeration(devs);
    return ret;
}

bool HidSensorInterface::open(const QString& name, HidSensorInterface::Status status, HidSensorInterface* relative)
{
    if(handle)
    {
        hid_close(handle);
        handle = 0;
    }
    if(tID > 0)
    {
        killTimer(tID);
        tID=0;
    }

    instance_status = status;
    switch(status)
    {
        case Standalone:
        {
            slave = 0;
            //goto Master - make the compiler happy))
            /* fall through */
            FALLTHROUGH;
        }
        case Master:
        {
            master = 0;
            QRegExp devNameRX("(.*)\\s+\\((.*)\\)");
            if(devNameRX.exactMatch(name))
            {
#if 0 /*Assume the API user knows the rules. The following check interferes with the sensor sub-class (RH/T) being reflected in type()*/
                if(devNameRX.cap(1) != type())
                {
                    qWarning()<<"Invalid device type "<<devNameRX.cap(1)<<" vs "<<type();
                    return false;
                }
#endif

                uuid = devNameRX.cap(2);
                if(uuid.isEmpty() || uuid=="S/N")
                {
                    qWarning()<<"Invalid serial number in device description "<<name;
                    return false;
                }

                QVector<wchar_t> wsn(uuid.size()+1, L'\0');
                wsn[uuid.toWCharArray(wsn.data())]=L'\0';

                handle = hid_open(OD_VID,OD_IOT_PID,wsn.constData());
                if(!handle)
                {
                    qWarning()<<"Cannot open HID device "<<uuid;
                    return false;
                }

                hid_set_nonblocking(handle,1);
                slave = relative;
                tID = startTimer(100);

                /*this will init the state_descr*/
                (void)readSettings();

                return true;
            }
            else
            {
                qWarning()<<"Invalid device description "<<name<<": must be \"Type (S/N)\"";
                return false;
            }
        }
        case Slave:
        {
            master = relative;
            if(master)
            {
                uuid = master->uuid;
            }
            slave = 0;
            return true;
        }
    }

    return false;
}

QString HidSensorInterface::serial() const
{
    return uuid;
}

QString HidSensorInterface::firmware()
{
    if(fwver.isEmpty())
    {
        if(get_feature(HID_FW_REPORT_ID))
        {
            fwver = QString::fromLatin1((const char*)hid_report_in+2,qMin<int>(hid_report_in[1],sizeof(hid_report_in)-2));
            HidSensorInterface* distr = slave;
            while(distr)
            {
                distr->fwver = fwver;
                distr = distr->slave;
            }
        }
    }
    return fwver;
}

double HidSensorInterface::getCurrentData()
{
    if(instance_status != Slave)
    {
        int ret = 0;
        if((ret=get_feature(HID_DATA_REPORT_ID))==0)
        {
            qWarning()<<"Cannot get "<<type()<<" data - feature request failed";
        }
        else
        {
            dataReportReceived(QByteArray((const char*)(hid_report_in+1),ret-1));
        }
    }
    return data;
}

/*
 * uiDescr:
 * {
 *     "type": "<device type>",
 *     "version": "<version>",
 *     -- custom KV pairs --,
 *     "settings": [
 *         {
 *             "key": "string_id",
 *             "type": "QML Class",
 *             "label": "<description>",
 *             "hint": "<optional hint>",
 *             "params": {
 *                 -- QML type specific KV
 *             }
 *         },
 *         {
 *             <...>
 *         }
 *     ]
 * }
 */

QString localizedTemplateString(const QJsonValue& v, const QString& lang)
{
    if(v.isString())
    {
        return v.toString();
    }
    QJsonObject obj = v.toObject();

    if(obj.contains(lang))
    {
        return obj.value(lang).toString();
    }
    return obj.value("en").toString();
}

QString HidSensorInterface::settingsDialog() const
{
    QJsonParseError jError;
    QJsonDocument doc = QJsonDocument::fromJson(uiDescr().toUtf8(),&jError);
    QJsonArray settingsObj = doc.object().value("settings").toArray();
    if(settingsObj.isEmpty())
    {
        qWarning()<<"No 'settings' array in uiDescr. Error: "<<jError.errorString()<<" at "<<jError.offset;
        return QString();
    }
    QString lang = QSettings().value(SETTINGS_LANG,"en").toString();

    QString ret;
    QString function_parse = "function parse(setObj){\n";
    QString onAccepted = "onAccepted: {\n"
                         "  var setObj = {\n";
    QString customComponents;
    QSet<QString> componentsRepo;

    for(int i=0;i<settingsObj.count();++i)
    {
        QJsonObject settingsItem = settingsObj.at(i).toObject();
        if(settingsItem.isEmpty() || !settingsItem.contains("key") ||
           !settingsItem.contains("type") || !settingsItem.contains("label"))
        {
            qWarning()<<"Skipping invalid item descr "<<settingsObj.at(i).toString();
            continue;
        }

        ret += QString("Label {\n"
                       "  text: \"%1:\"\n")
               .arg(localizedTemplateString(settingsItem.value("label"),lang));
        ret += QString("}\n");
        bool custom_component = settingsItem.contains("custom") && settingsItem.value("custom").toBool();

        if(custom_component)
        {
            if(!componentsRepo.contains(settingsItem.value("type").toString()))
            {
                QFile componentFile(QString(":/qml/%1.qmlt").arg(settingsItem.value("type").toString()));
                componentFile.open(QFile::ReadOnly);
                customComponents += QString("Component {\n"
                                            "  id: %1\n\n"
                                            "  %2\n"
                                            "}\n")
                                    .arg(settingsItem.value("type").toString().toLower())
                                    .arg(QString::fromUtf8(componentFile.readAll()));
                componentFile.close();
                componentsRepo.insert(settingsItem.value("type").toString());
            }

            ret += QString("Loader {\n"
                           "  sourceComponent: %1\n"
                           "  id: %2\n")
                   .arg(settingsItem.value("type").toString().toLower())
                   .arg(settingsItem.value("key").toString());
        }
        else
        {
            ret += QString("%1 {\n"
                           "  id: %2\n")
                   .arg(settingsItem.value("type").toString())
                   .arg(settingsItem.value("key").toString());
        }

        ret += "  Layout.fillWidth: true\n";

        QVariantHash params = settingsItem.value("params").toObject().toVariantHash();

        if(!custom_component)
        {
            for(QVariantHash::const_iterator it = params.constBegin();it != params.constEnd();++it)
            {
                ret += "  "+it.key()+": ";
                qWarning()<<"typeName of "<<it.key()<<it.value().typeName();
                if(it.value().typeName() && (QString::fromLatin1(it.value().typeName())=="QString"))
                {
                    ret += "\""+it.value().toString()+"\"";
                }
                else
                {
                    ret += it.value().toString();
                }
                ret+="\n";
            }
            if(!settingsItem.value("hint").toString().isEmpty())
            {
                ret += QString("  hoverEnabled: true\n"
                               "  ToolTip.visible: hovered\n"
                               "  ToolTip.text: \"%1\"\n")
                       .arg(localizedTemplateString(settingsItem.value("hint"),lang));
            }

            function_parse += QString("  %1.%2 = setObj.%1;\n")
                              .arg(settingsItem.value("key").toString())
                              .arg(settingsItem.value("field").toString());
            onAccepted+= QString("    %1: %1.%2,\n")
                         .arg(settingsItem.value("key").toString())
                         .arg(settingsItem.value("field").toString());
        }
        else
        {
            ret += "  onLoaded: {\n";
            for(QVariantHash::const_iterator it = params.constBegin();it != params.constEnd();++it)
            {
                if(it.value().typeName() && (QString::fromLatin1(it.value().typeName()) == "QString"))
                {
                    ret += "    item."+it.key()+" = \""+it.value().toString()+"\";\n";
                }
                else
                {
                    ret += "    item."+it.key()+" = "+it.value().toString()+";\n";
                }
            }
            if(!settingsItem.value("hint").toString().isEmpty())
            {
                ret += QString("  item.hint = \"%1\";\n")
                       .arg(localizedTemplateString(settingsItem.value("hint"),lang));
            }
            ret += "  }\n";

            if(settingsItem.contains("onInit"))
            {
                function_parse += QString("  %1.item.%2;\n")
                                  .arg(settingsItem.value("key").toString())
                                  .arg(settingsItem.value("onInit").toString()
                                       .replace("$1",QString("setObj.")+settingsItem.value("key").toString()));
            }
            else
            {
                function_parse += QString("  %1.item.%2 = setObj.%1;\n")
                                  .arg(settingsItem.value("key").toString())
                                  .arg(settingsItem.value("field").toString());
            }
            onAccepted+= QString("    %1: %1.item.%2,\n")
                         .arg(settingsItem.value("key").toString())
                         .arg(settingsItem.value("field").toString());
        }

        ret += QString("}\n"
                       "Label{text:\"%1\"}\n"
                       ).arg(localizedTemplateString(settingsItem.value("unit"),lang));
    }
    function_parse+="}\n";

    onAccepted+=QString("};\n"
                        "if (runner.sensor){\n"
                        "  runner.sensor.writeSettings(setObj);\n"
                        "}\n}\n");

    QString dialogTitle = tr("%1 Config").arg(type());

    return QString("import QtQuick 2.10\n"
                   "import QtQuick.Controls 2.3\n"
                   "import QtQuick.Layouts 1.3\n"
                   "\n\n"
                   "Dialog {\n"
                   "\n\n"
                   "%1"
                   "\n\n"
                   "  id: %2_setupDialog\n"
                   "  title: \"%3\"\n"
                   "  modal: true\n"
                   "  standardButtons: Dialog.Ok | Dialog.Cancel\n"
                   "  Flickable {\n"
                   "  id: flick\n"
                   "  anchors.fill: parent\n"
                   "  clip: true\n"
                   "  contentHeight: glayout.height\n"
                   "  GridLayout {\n"
                   "  id: glayout\n"
                   "  width: flick.width-10\n"
                   "  columns: 3\n").arg(customComponents).arg(type().toLower()).arg(dialogTitle)
            +ret+
            "}\n"+
            "ScrollBar.vertical: ScrollBar {\n"
            "  width: 10\n"
            "  active: true\n"
            "  visible: true\n"
            "}\n"
            "}\n"+
            function_parse+
            "\n"+
            onAccepted+
            "\n"+
            QString("onClosed: {\n"
                    "console.log(\"%1_setupDialog will destroy itself\");\n"
                    "%1_setupDialog.destroy();\n"
                    "}\n").arg(type().toLower())+
            "\n}";
}

void HidSensorInterface::command(int cmd, const QByteArray& params)
{
    if(instance_status == Slave)
    {
        if(master)
        {
            master->command(cmd,params);
        }
        else
        {
            qWarning()<<"Slave with no master - cannot execute command";
        }
        return;
    }

    if(!handle) return;

    qWarning()<<"HID report: "<<int(cmd)<<params;

    hid_report_out[0] = HID_CMD_REPORT_ID;
    hid_report_out[1] = uint8_t(cmd);
    if(params.size())
    {
        memcpy(hid_report_out+2,params.constData(),size_t(qMin<int>(params.size(),HID_CMD_REPORT_SIZE-2)));
    }
    else
    {
        memset(hid_report_out+2,0,HID_CMD_REPORT_SIZE-2);
    }

    reply_timeout = 9;
    if(hid_write(handle, hid_report_out, HID_CMD_REPORT_SIZE)<HID_CMD_REPORT_SIZE)
    {
        QString errorDescr = QString::fromWCharArray(hid_error(handle));

        qWarning()<<"Cannot send CMD report the the device: "<<errorDescr;
        hid_close(handle);
        handle=0;

        emit_error(tr("Device communication error: %1").arg(errorDescr));
    }
}

void HidSensorInterface::reset()
{
    command(HID_CMD_RST_UAPP);
}

void HidSensorInterface::upgrade()
{
    command(HID_CMD_RST_DFU);
}

#ifndef OD_NO_DEVELOPER
void HidSensorInterface::dfu()
{
    command(HID_CMD_RST_STM);
}
#endif

void HidSensorInterface::timerEvent(QTimerEvent* e)
{
    Q_UNUSED(e);
    if(!handle)
    {
        //qWarning()<<"Cannot serve timerEvent - handle is NULL";
        return;
    }

    int ret = hid_read(handle,hid_report_in,64);
    if(ret<0)
    {
        QString errorDescr = QString::fromWCharArray(hid_error(handle));

        qWarning()<<"HID read error "<<errorDescr;
        hid_close(handle);
        handle=0;

        emit_error(tr("Device communication error: %1").arg(errorDescr));
        return;
    }

    if(!ret)
    {
        //qWarning()<<"Device "<<uuid<<" returned zilch";
        if(reply_timeout && (--reply_timeout==0))
        {
            qWarning()<<"Device is not responding - timeout";
            hid_close(handle);
            handle = 0;
            emit_error(tr("Device is not responding"));
        }
        return;
    }

    qWarning()<<"Device "<<uuid<<" reports: "<<QString::fromLatin1(QByteArray((const char*)hid_report_in,ret).toHex().toUpper());
    switch(hid_report_in[0])
    {
        case HID_DATA_REPORT_ID:
        {
            dataReportReceived(QByteArray((const char*)(hid_report_in+1),ret-1));
            break;
        }
        case HID_EVENT_REPORT_ID:
        {
            if(ret>=2)
            {
                sensorStateChanged(hid_report_in[1]);
            }
            break;
        }
        case HID_FW_REPORT_ID:
        {
            if(ret>=2)
            {
                fwver = QString::fromLatin1((const char*)hid_report_in+2,qMin<int>(hid_report_in[1],ret-2));
            }
            break;
        }
        case HID_CMD_REPORT_ID:
        {
            reply_timeout = 0;
            if(ret<2)
            {
                break;
            }
            switch(hid_report_in[1])
            {
                case HID_CMD_RST_DFU:
                case HID_CMD_RST_UAPP:
                case HID_CMD_RST_STM:
                {
                    qWarning()<<"Device will be reset";
                    hid_close(handle);
                    handle = 0;
                    emit_error(tr("Device has been reset"));
                    break;
                }
                default:
                {
                    customCommandReceived(hid_report_in[1],QByteArray((const char*)(hid_report_in+2),ret-2));
                    break;
                }
            }
            break;
        }
        default:
        {
            qWarning()<<"Unknown report id "<<int(hid_report_in[0]);
            break;
        }
    }
}

int HidSensorInterface::get_feature(quint8 id, unsigned char* output)
{
    if(instance_status==Slave)
    {
        if(!output)
        {
            output = hid_report_in;
        }
        return master ? master->get_feature(id, output) : 0;
    }

    //Master & Standalone
    if(!handle)
    {
        qWarning()<<"Cannot get report - device not open";
        return false;
    }
    if(!output)
    {
        output = hid_report_in;
    }

    output[0] = id;
    memset(output+1,0,63);

    int ret = hid_get_feature_report(handle,output,64);
    if(ret>0)
    {
        qWarning()<<"Got "<<ret<<" bytes (incl report) for GET_FEATURE["<<id<<"]";
        return ret;
    }
    qWarning()<<"No reponse for GET_FEATURE["<<id<<"]";
    return 0;
}

void HidSensorInterface::dataReportReceived(const QByteArray& payload)
{
    double newdata = processDataReport(payload);
    qWarning()<<type()<<"-"<<uuid<<" reports current data "<<newdata;
    /* store first: the slots read getData() */
    bool changed = !qFuzzyIsNull(newdata-data);
    data = newdata;
    if(changed) {
        emit dataChanged(newdata);
    }

    if((instance_status!=Standalone)&&slave)
    {
        slave->dataReportReceived(payload);
    }
}

void HidSensorInterface::customCommandReceived(quint8 cmd, const QByteArray& payload)
{
    processCustomCommand(cmd, payload);
    if((instance_status!=Standalone)&&slave)
    {
        slave->processCustomCommand(cmd, payload);
    }
}

void HidSensorInterface::sensorStateChanged(uint8_t state)
{
    if((state > Critical) || (sens_state>Critical))
    {
        switch(state)
        {
            case Normal:
            case Acceptable:
            case Critical:
                sens_state = Normal;
                break;
            case InternalError:
                sens_state = InternalError;
                break;
            case CustomState:
            default:
                sens_state = CustomState;
                break;
        }
        emit stateChanged(int(sens_state));
        if((instance_status != Standalone) && slave)
        {
            slave->sensorStateChanged(state);
        }
    }
}

void HidSensorInterface::emit_error(const QString& descr)
{
    if(slave)
    {
        slave->master_error(descr);
    }
    emit error(descr);
}

void HidSensorInterface::master_error(const QString& descr)
{
    emit error(descr);
    if(slave)
    {
        slave->master_error(descr);
    }
}
