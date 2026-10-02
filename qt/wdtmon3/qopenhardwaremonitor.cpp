#include "qopenhardwaremonitor.h"

#include <QDebug>

QOpenHardwareMonitor::QOpenHardwareMonitor(QObject *parent) : QObject(parent)
#ifdef Q_OS_WIN
    ,handle(new QWMIOpenHardwareMonitor(this)), h_valid(false)
#endif
{}

QOpenHardwareMonitor::~QOpenHardwareMonitor()
{
#ifdef Q_OS_WIN
    delete handle;
    handle = 0;
#endif
}

#ifdef Q_OS_WIN
bool QOpenHardwareMonitor::init()
{
    return (h_valid = handle->initService());
}

QStringList QOpenHardwareMonitor::cpus()
{
    return get(QList<int>()<<QOHM::CPU);
}

QStringList QOpenHardwareMonitor::gpus()
{
    return get(QList<int>()<<QOHM::GPU_Ati<<QOHM::GPU_Nvidia);
}

QStringList QOpenHardwareMonitor::get(const QList<int>& htypes)
{
    if(!h_valid)
    {
        qWarning()<<"OHM-WMI is in invalid state";
        return QStringList();
    }

    bool ok;
    QOHM::qohmHardware reply = handle->getState(htypes,QList<int>(),&ok);
    if(!ok)
    {
        qWarning()<<"WMI request failed";
        return QStringList();
    }
    qWarning()<<"ohm-wmi get "<<htypes<<" returned "<<reply.keys();

    QStringList ret;
    QHashIterator<QString,QOHM::qohmSensorValues> it(reply);
    while(it.hasNext())
    {
        it.next();
        ret.append(it.key());
        QOHM::qohmSensorValues sensors = it.value();
        QHashIterator<QString,QOHM::qohmSensorValue> sit(sensors);
        while(sit.hasNext())
        {
            sit.next();
            qWarning()<<it.key()<<" - "<<sit.key()<<": "<<sit.value().toString();
        }
    }
    return ret;
}
#else
bool QOpenHardwareMonitor::init() {return false;}

QStringList QOpenHardwareMonitor::cpus()
{
    return QStringList();
}

QStringList QOpenHardwareMonitor::gpus()
{
    return QStringList();
}

QStringList QOpenHardwareMonitor::get(const QList<int>& htypes)
{
    return QStringList();
}
#endif
