#include "ql-channel-serial.hpp"
#include <QDebug>
#include <QSettings>

QlChannelSerial::QlChannelSerial(QObject* parent) :
    QlChannel(parent), port_(new QSerialPort(this))
{
    connect(port_, &QSerialPort::readyRead, this, &QlChannelSerial::portReadyRead);
    connect(port_, &QSerialPort::errorOccurred, this, &QlChannelSerial::portError);
    params_ << "baud" << "bauds" << "bits" << "parity" << "stops" << "rts" << "cts" << "dtr" << "dsr" << "dcd" << "ri";
}

QlChannelSerial::~QlChannelSerial()
{
    if(port_->isOpen())
    {
        port_->close();
    }
}

QStringList QlChannelSerial::channels() {
    QList<QSerialPortInfo> pil = QSerialPortInfo::availablePorts();
    QStringList psl = QStringList();

    for (int i=0; i<pil.size(); i++)
    {
#ifdef Q_OS_WIN
        if(pil.at(i).portName() == "COM1")
        {
            continue;
        }
#endif
        /*
         * manufacturer:
         * 1. 2018 - Open Development
         * 2. 2017 - STMicroelectronics
         */
        //qWarning()<<"Found serial port with manufacturer "<<pil.at(i).manufacturer();
#ifndef OD_NO_DEVELOPER

#ifdef Q_OS_WIN
        psl.append( pil[i].portName() );
#else
        psl.append( pil[i].systemLocation() );
#endif

#else

#error "Originality check?"
        if(pil.at(i).hasVendorIdentifier() && (pil.at(i).vendorIdentifier()==0x0483))
        {
#ifdef Q_OS_WIN
            psl.append( pil[i].portName() );
#else
            psl.append( pil[i].systemLocation() );
#endif
        }
#endif
    }

    return psl;
}

bool QlChannelSerial::open(const QString &name) {
    close();
    if(name.isEmpty()) {
        return false;
    }
    port_->setPortName(name);

    if (port_->open(QIODevice::ReadWrite)) {
        //qWarning()<<"Port "<<name<<" opened";
        name_ = name;
        emit nameChanged(name_);
        return true;
    }

    //qWarning()<<"Cannot open serial port "<<name<<": "<<port_->error()<<"("<<port_->errorString()<<")";
    //qWarning()<<"System ports: "<<channels();
    name_ = QString::fromLatin1("");
    emit nameChanged(name_);
    return false;
}

void QlChannelSerial::close() {
    if (isOpen()) {
        qWarning()<<"Request "<<name_<<"close (normal)";
        port_->close();
    }
    if(!name_.isEmpty()) {
        name_.clear();
        emit nameChanged(name_);
    }
}

bool QlChannelSerial::isOpen()
{
    return port_->isOpen();
}

QString QlChannelSerial::name()
{
    return name_;
}

QString QlChannelSerial::param(const QString &name)
{
    if (!isOpen())
    {
        return QString::fromLatin1("");
    }

    if (name == "baud")
    {
        return QString::number(port_->baudRate());
    }
    else if (name == "bits")
    {
        return QString::number(port_->dataBits());
    }
    else if (name == "parity")
    {
        switch(port_->parity()) {
            case QSerialPort::NoParity: return QLatin1String("n");
            case QSerialPort::EvenParity: return QLatin1String("e");
            case QSerialPort::OddParity: return QLatin1String("o");
            case QSerialPort::SpaceParity: return QLatin1String("s");
            case QSerialPort::MarkParity: return QLatin1String("m");
            default:break;
        }
        return QLatin1String("u");
    }
    else if (name == "stops")
    {
        return QString::number(port_->stopBits());
    }
    else if (name == "rts")
    {
        return QString::number(port_->isRequestToSend());
    }
    else if (name == "cts")
    {
        return QString::number(!!(port_->pinoutSignals() & QSerialPort::ClearToSendSignal));
    }
    else if (name == "dtr")
    {
        return QString::number(port_->isDataTerminalReady());
    }
    else if (name == "dsr")
    {
        return QString::number(!!(port_->pinoutSignals() & QSerialPort::DataSetReadySignal));
    }
    else if (name == "dcd")
    {
        return QString::number(!!(port_->pinoutSignals() & QSerialPort::DataCarrierDetectSignal));
    }
    else if (name == "ri")
    {
        return QString::number(!!(port_->pinoutSignals() & QSerialPort::RingIndicatorSignal));
    }
    return QString::fromLatin1("");
}

bool QlChannelSerial::paramSet(const QString &name, const QString &value) {
    if (!isOpen()) return false;

    if (name == "baud")
    {
        return port_->setBaudRate(value.toInt());
    }
    else if (name == "bits")
    {
        return port_->setDataBits((QSerialPort::DataBits)value.toInt());
    }
    else if (name == "parity")
    {
        QSerialPort::Parity _p = QSerialPort::NoParity;
        if(value.startsWith("e"))
        {
            _p = QSerialPort::EvenParity;
        }
        else if(value.startsWith("o"))
        {
            _p = QSerialPort::OddParity;
        }
        else if(value.startsWith("s"))
        {
            _p = QSerialPort::SpaceParity;
        }
        else if(value.startsWith("m"))
        {
            _p = QSerialPort::MarkParity;
        }
        return port_->setParity(_p);
    }
    else if (name == "stops")
    {
        return port_->setStopBits((QSerialPort::StopBits)value.toInt());
    }
    else if (name == "rts")
    {
        return port_->setRequestToSend((bool)value.toInt());
    }
    else if (name == "dtr")
    {
        return port_->setDataTerminalReady((bool)value.toInt());
    }
    return false;
}

QString QlChannelSerial::readString()
{
    if (isOpen()) {
        return QString::fromLatin1(port_->readAll());
    }
    return QString();
}

bool QlChannelSerial::writeString(const QString &s) {
    if (isOpen()) {
        qWarning()<<"To serial: "<<s;
        const QByteArray data = s.toLatin1();
        const qint64 queued = port_->write(data);
        return (queued == data.size()) && port_->flush();
    }
    return false;
}

void QlChannelSerial::portReadyRead()
{
    QString data = QString::fromLatin1(port_->readAll());
    qWarning()<<"From serial: "<<data;
    emit dataReady(data);
}

void QlChannelSerial::portError(QSerialPort::SerialPortError error)
{
    qWarning()<<"Serial error "<<error;
    switch (error) {
        case QSerialPort::DeviceNotFoundError:
        case QSerialPort::WriteError:
        case QSerialPort::ReadError:
        case QSerialPort::ResourceError:
            qWarning()<<"Request close (from error)";
            QMetaObject::invokeMethod(this, "close", Qt::QueuedConnection);
            break;
        default:
            break;
    }
}
