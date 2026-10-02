#ifndef QCOMPORT_H_
#define QCOMPORT_H_

#include <QSerialPort>
#include <QSerialPortInfo>
#include <QSettings>
#include <QHash>
#include <QDebug>

#define QCOMPORT_SETTING_ADDR(PREFIX)      (QString(PREFIX) + QLatin1String("/Address"))
#define QCOMPORT_SETTING_ADDR_DEFAULT      (0)
#define QCOMPORT_SETTING_BAUD(PREFIX)      (QString(PREFIX) + QLatin1String("/Baud"))
#define QCOMPORT_SETTING_BAUD_DEFAULT      QSerialPort::Baud115200
#define QCOMPORT_SETTING_PARITY(PREFIX)    (QString(PREFIX) + QLatin1String("/Parity"))
#define QCOMPORT_SETTING_PARITY_DEFAULT    QSerialPort::NoParity
#define QCOMPORT_SETTING_BITS(PREFIX)      (QString(PREFIX) + QLatin1String("/Bits"))
#define QCOMPORT_SETTING_BITS_DEFAULT      QSerialPort::Data8
#define QCOMPORT_SETTING_FLOW(PREFIX)      (QString(PREFIX) + QLatin1String("/FlowCtrl"))
#define QCOMPORT_SETTING_FLOW_DEFAULT      QSerialPort::NoFlowControl
#define QCOMPORT_SETTING_STOP(PREFIX)      (QString(PREFIX) + QLatin1String("/StopBits"))
#define QCOMPORT_SETTING_STOP_DEFAULT      QSerialPort::OneStop
#define QCOMPORT_SETTING_BLACKLIST(PREFIX) (QString(PREFIX) + QLatin1String("/BlackList"))

namespace QComPort
{

inline QStringList availableComPorts(const QString& vendor_filter = QString())
{
    QList<QSerialPortInfo> availList = QSerialPortInfo::availablePorts();
    QStringList ret;
    if(!availList.size())
        return ret;

    ret.reserve(availList.size());
    foreach(const QSerialPortInfo& info, availList)
    {
#ifdef Q_OS_WIN
        if(info.portName()=="COM1") {continue;}
#endif
        if(vendor_filter.isEmpty() || info.manufacturer().isEmpty() || (vendor_filter == info.manufacturer()))
        {
            ret.append(info.portName());
        }
        else
        {
            qWarning()<<"Vendor or "<<info.portName()<<" - "<<info.manufacturer()<<" does not match "<<vendor_filter;
        }
    }
    return ret;
}

inline QString comPortDescr(const QSerialPortInfo& info)
{
    return info.portName()+": "+info.description()+", manufacturer: "+info.manufacturer()
#if QT_VERSION >= 0x050300
            +", serial: "+info.serialNumber()
#endif
            ;
}

inline QString comPortShortInfo(const QSerialPort& comport)
{
    QString comport_description = comport.portName() + QLatin1String(" ") + QString::number(comport.dataBits());
    switch(comport.parity())
    {
        case QSerialPort::EvenParity: comport_description += QLatin1String("E");break;
        case QSerialPort::OddParity: comport_description += QLatin1String("O");break;
        case QSerialPort::SpaceParity: comport_description += QLatin1String("S");break;
        case QSerialPort::MarkParity: comport_description += QLatin1String("M");break;
        default:
        case QSerialPort::NoParity: comport_description += QLatin1String("N");break;
    }
    switch(comport.stopBits())
    {
        case QSerialPort::OneAndHalfStop: comport_description += QLatin1String("1.5");break;
        case QSerialPort::TwoStop: comport_description += QLatin1String("2"); break;
        default: comport_description += QLatin1String("1"); break;
    }
    comport_description+=QString("@%1").arg(comport.baudRate());
    switch(comport.flowControl())
    {
        case QSerialPort::HardwareControl: comport_description += " +rts/cts";break;
        case QSerialPort::SoftwareControl: comport_description += " +xon/xoff"; break;
        default: break;
    }
    return comport_description;
}

inline QStringList availableComPortsDescr()
{
    QList<QSerialPortInfo> availList = QSerialPortInfo::availablePorts();
    QStringList ret;
    if(!availList.size())
        return ret;

    ret.reserve(availList.size());
    foreach(const QSerialPortInfo& info, availList)
    {
        ret.append(comPortDescr(info));
    }
    return ret;
}

inline void defaultComSettings(QSettings& set, const QString& prefix)
{
    set.setValue(QCOMPORT_SETTING_BAUD(prefix),   QCOMPORT_SETTING_BAUD_DEFAULT);
    set.setValue(QCOMPORT_SETTING_PARITY(prefix), QCOMPORT_SETTING_PARITY_DEFAULT);
    set.setValue(QCOMPORT_SETTING_BITS(prefix),   QCOMPORT_SETTING_BITS_DEFAULT);
    set.setValue(QCOMPORT_SETTING_FLOW(prefix),   QCOMPORT_SETTING_FLOW_DEFAULT);
    set.setValue(QCOMPORT_SETTING_STOP(prefix),   QCOMPORT_SETTING_STOP_DEFAULT);
}

inline bool setupComPortFromSettings(const QString& portName, QSettings& set, const QString& sprefix, QSerialPort& comport)
{
    QList<QSerialPortInfo> availPortInfos = QSerialPortInfo::availablePorts();
    qDebug()<<"Requested connect to "<<portName<<"; available ports: "<<availableComPorts();

    int pi = -1;
    for(int i=0;i<availPortInfos.size();++i)
    {
        if(availPortInfos.at(i).portName() ==portName)
        {
            pi = i;
            break;
        }
    }
    if(pi<0)
    {
        qDebug()<<"Requested port not found";
        return false;
    }

    if(set.value(QCOMPORT_SETTING_BAUD(sprefix), "").toString().isEmpty())
    {
        defaultComSettings(set, sprefix);
    }

    comport.setPort(availPortInfos.at(pi));

    comport.setBaudRate(set.value(QCOMPORT_SETTING_BAUD(sprefix), QCOMPORT_SETTING_BAUD_DEFAULT).toUInt(), QSerialPort::Input);
    comport.setBaudRate(set.value(QCOMPORT_SETTING_BAUD(sprefix), QCOMPORT_SETTING_BAUD_DEFAULT).toUInt(), QSerialPort::Output);

    switch(set.value(QCOMPORT_SETTING_BITS(sprefix), QCOMPORT_SETTING_BITS_DEFAULT).toInt())
    {
        case QSerialPort::Data5:
        case QSerialPort::Data6:
        case QSerialPort::Data7:
        case QSerialPort::Data8:
        {
            comport.setDataBits(static_cast<QSerialPort::DataBits>(set.value(QCOMPORT_SETTING_BITS(sprefix), QCOMPORT_SETTING_BITS_DEFAULT).toInt()));
            break;
        }
        default:
        {
            comport.setDataBits(QSerialPort::Data8);
            set.setValue(QCOMPORT_SETTING_BITS(sprefix), QCOMPORT_SETTING_BITS_DEFAULT);
            break;
        }
    }

    switch(set.value(QCOMPORT_SETTING_PARITY(sprefix), QCOMPORT_SETTING_PARITY_DEFAULT).toInt())
    {
        case QSerialPort::NoParity:
        case QSerialPort::EvenParity:
        case QSerialPort::OddParity:
        case QSerialPort::SpaceParity:
        case QSerialPort::MarkParity:
        {
            comport.setParity(static_cast<QSerialPort::Parity>(set.value(QCOMPORT_SETTING_PARITY(sprefix), QCOMPORT_SETTING_PARITY_DEFAULT).toInt()));
            break;
        }
        default:
        {
            comport.setParity(QSerialPort::NoParity);
            set.setValue(QCOMPORT_SETTING_PARITY(sprefix), QCOMPORT_SETTING_PARITY_DEFAULT);
            break;
        }
    }

    switch(set.value(QCOMPORT_SETTING_FLOW(sprefix), QCOMPORT_SETTING_FLOW_DEFAULT).toInt())
    {
        case QSerialPort::NoFlowControl:
        case QSerialPort::HardwareControl:
        case QSerialPort::SoftwareControl:
        {
            comport.setFlowControl(static_cast<QSerialPort::FlowControl>(set.value(QCOMPORT_SETTING_FLOW(sprefix), QCOMPORT_SETTING_FLOW_DEFAULT).toInt()));
            break;
        }
        default:
        {
            comport.setFlowControl(QSerialPort::NoFlowControl);
            set.setValue(QCOMPORT_SETTING_FLOW(sprefix), QCOMPORT_SETTING_FLOW_DEFAULT);
            break;
        }
    }

    switch (set.value(QCOMPORT_SETTING_STOP(sprefix), QCOMPORT_SETTING_STOP_DEFAULT).toInt())
    {
        case QSerialPort::OneStop:
        case QSerialPort::OneAndHalfStop:
        case QSerialPort::TwoStop:
        {
            comport.setStopBits(static_cast<QSerialPort::StopBits>(set.value(QCOMPORT_SETTING_STOP(sprefix), QCOMPORT_SETTING_STOP_DEFAULT).toInt()));
            break;
        }
        default:
        {
            comport.setStopBits(QSerialPort::OneStop);
            set.setValue(QCOMPORT_SETTING_STOP(sprefix), QCOMPORT_SETTING_STOP_DEFAULT);
            break;
        }
    }

    qWarning()<<comPortShortInfo(comport);
    return comport.open(QIODevice::ReadWrite);
}

inline QHash<QString,QVariant> comPortConfig(const QSerialPort& comport, const QString& sprefix)
{
    QHash<QString,QVariant> set;
    set.insert(QCOMPORT_SETTING_BAUD(sprefix),   (int)comport.baudRate());
    set.insert(QCOMPORT_SETTING_PARITY(sprefix), (int)comport.parity());
    set.insert(QCOMPORT_SETTING_BITS(sprefix),   (int)comport.dataBits());
    set.insert(QCOMPORT_SETTING_FLOW(sprefix),   (int)comport.flowControl());
    set.insert(QCOMPORT_SETTING_STOP(sprefix),   (int)comport.stopBits());
    return set;
}

inline bool setupComPortFromHash(const QString& portName, const QHash<QString,QVariant>& set, const QString& sprefix, QSerialPort& comport)
{
    QList<QSerialPortInfo> availPortInfos = QSerialPortInfo::availablePorts();
    qDebug()<<"Requested connect to "<<portName<<"; available ports: "<<availableComPorts();

    int pi = -1;
    for(int i=0;i<availPortInfos.size();++i)
    {
        if(availPortInfos.at(i).portName() ==portName)
        {
            pi = i;
            break;
        }
    }
    if(pi<0)
    {
        qDebug()<<"Requested port not found";
        return false;
    }
    comport.setPort(QSerialPortInfo::availablePorts().at(pi));

    comport.setBaudRate(set.value(QCOMPORT_SETTING_BAUD(sprefix), QCOMPORT_SETTING_BAUD_DEFAULT).toUInt(),QSerialPort::Input);
    comport.setBaudRate(set.value(QCOMPORT_SETTING_BAUD(sprefix), QCOMPORT_SETTING_BAUD_DEFAULT).toUInt(),QSerialPort::Output);

    switch(set.value(QCOMPORT_SETTING_PARITY(sprefix), QCOMPORT_SETTING_PARITY_DEFAULT).toInt())
    {
        case QSerialPort::NoParity:
        case QSerialPort::EvenParity:
        case QSerialPort::OddParity:
        case QSerialPort::SpaceParity:
        case QSerialPort::MarkParity:
        {
            comport.setParity(static_cast<QSerialPort::Parity>(set.value(QCOMPORT_SETTING_PARITY(sprefix), QCOMPORT_SETTING_PARITY_DEFAULT).toInt()));
            break;
        }
        default:
        {
            comport.setParity(QCOMPORT_SETTING_PARITY_DEFAULT);
            break;
        }
    }

    switch(set.value(QCOMPORT_SETTING_BITS(sprefix), QCOMPORT_SETTING_BITS_DEFAULT).toInt())
    {
        case QSerialPort::Data5:
        case QSerialPort::Data6:
        case QSerialPort::Data7:
        case QSerialPort::Data8:
        {
            comport.setDataBits(static_cast<QSerialPort::DataBits>(set.value(QCOMPORT_SETTING_BITS(sprefix), QCOMPORT_SETTING_BITS_DEFAULT).toInt()));
            break;
        }
        default:
        {
            comport.setDataBits(QCOMPORT_SETTING_BITS_DEFAULT);
            break;
        }
    }

    switch(set.value(QCOMPORT_SETTING_FLOW(sprefix), QCOMPORT_SETTING_FLOW_DEFAULT).toInt())
    {
        case QSerialPort::NoFlowControl:
        case QSerialPort::HardwareControl:
        case QSerialPort::SoftwareControl:
        {
            comport.setFlowControl(static_cast<QSerialPort::FlowControl>(set.value(QCOMPORT_SETTING_FLOW(sprefix), QCOMPORT_SETTING_FLOW_DEFAULT).toInt()));
            break;
        }
        default:
        {
            comport.setFlowControl(QCOMPORT_SETTING_FLOW_DEFAULT);
            break;
        }
    }

    switch (set.value(QCOMPORT_SETTING_STOP(sprefix), QCOMPORT_SETTING_STOP_DEFAULT).toInt())
    {
        case QSerialPort::OneStop:
        case QSerialPort::OneAndHalfStop:
        case QSerialPort::TwoStop:
        {
            comport.setStopBits(static_cast<QSerialPort::StopBits>(set.value(QCOMPORT_SETTING_STOP(sprefix), QCOMPORT_SETTING_STOP_DEFAULT).toInt()));
            break;
        }
        default:
        {
            comport.setStopBits(QCOMPORT_SETTING_STOP_DEFAULT);
            break;
        }
    }

    if(!comport.open(QIODevice::ReadWrite))
    {
        qWarning()<<"Cannot open serial port "<<comport.portName()<<" - "<<comport.errorString();
        return false;
    }

    qWarning()<<comPortShortInfo(comport);
    return true;
}

}

#endif  // QCOMPORT_H_
