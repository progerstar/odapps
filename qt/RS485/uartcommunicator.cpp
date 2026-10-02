#include "uartcommunicator.h"
#include <QDebug>
#include <QEventLoop>
#include <QTime>
#include <QTimer>
#include <QCoreApplication>

void UARTCommunicator::app_addParserOptions(QCommandLineParser& parser)
{
    QCommandLineOption baudOption(QStringList()<<"b"<<"baud", "Baud <rate> (default: 115200).", "baud", "115200");
    parser.addOption(baudOption);
    QCommandLineOption parityOption(QStringList()<<"p"<<"parity", "Parity (none,odd,even; default: none)", "parity", "none");
    parser.addOption(parityOption);
    QCommandLineOption stopOption(QStringList()<<"s"<<"stop", "Stop bits (1,1.5,2; default: 1)", "stops", "1");
    parser.addOption(stopOption);
    QCommandLineOption addrOption(QStringList()<<"a"<<"address", "Pre-select the device addrss (default: ask).", "address");
    parser.addOption(addrOption);
}

bool UARTCommunicator::app_processParserOptions(QCommandLineParser& parser, QString& errorString)
{
    QSettings set;

    if(parser.isSet("address"))
    {
        QString address_val = parser.value("address");
        bool addr_conv = false;
        int addr = address_val.startsWith("0x", Qt::CaseInsensitive) ?
                       address_val.mid(2).toInt(&addr_conv, 16) : address_val.toInt(&addr_conv);
        if(!addr_conv || (addr<0) || (addr>0xFF))
        {
            errorString = UARTCommunicator::tr("Invalid address %1").arg(address_val);
            return false;
        }

        set.setValue(QCOMPORT_SETTING_ADDR(RS485_SETTINGS_PREFIX), addr);
    }

    if(parser.isSet("parity"))
    {
        QString par = parser.value("parity").toLower();
        QSerialPort::Parity sp = QSerialPort::NoParity;
        if(par == "odd")
        {
            sp = QSerialPort::OddParity;
        }
        else if(par == "event")
        {
            sp = QSerialPort::EvenParity;
        }
        else if(par != "none")
        {
            errorString = UARTCommunicator::tr("Invalid parity value %1").arg(par);
            return false;
        }

        set.setValue(QCOMPORT_SETTING_PARITY(RS485_SETTINGS_PREFIX), sp);
    }

    if(parser.isSet("baud"))
    {
        bool ok;
        quint32 baudrate = parser.value("baud").toUInt(&ok);
        if(!ok)
        {
            errorString = UARTCommunicator::tr("Invalid baudrate %1").arg(parser.value("baud"));
            return false;
        }

        set.setValue(QCOMPORT_SETTING_BAUD(RS485_SETTINGS_PREFIX), baudrate);
    }

    if(parser.isSet("stop"))
    {
        QString stops = parser.value("stop");
        QSerialPort::StopBits sb = QSerialPort::OneStop;
        if(stops == "1.5")
        {
            sb = QSerialPort::OneAndHalfStop;
        }
        else if(stops == "2")
        {
            sb = QSerialPort::TwoStop;
        }
        else if(stops != "1")
        {
            errorString = UARTCommunicator::tr("Invalid stop bits value %1").arg(stops);
        }

        set.setValue(QCOMPORT_SETTING_STOP(RS485_SETTINGS_PREFIX), sb);
    }

    return true;
}

bool UARTCommunicator::app_parserOptionsSet(const QCommandLineParser& parser)
{
    return parser.isSet("baud") || parser.isSet("address") || parser.isSet("stop") || parser.isSet("parity");
}


/*
 * Bytes:
 *  0: 0x7E
 *  1: Payload len
 *  2: Address
 *  3-n: data
 *  n+1: crc8 sum
 *
 */

#define UART_COM_HEADER (0x7E)

static inline uint8_t uart_crc8(const uint8_t* buffer, uint32_t len)
{
    uint8_t sum = 0;
    while(len--)
    {
        sum += *buffer++;
    }
    return sum;
}


UARTCommunicator::UARTCommunicator(QObject *parent) : QObject(parent),
    baudrate(115200), handle(this)
{
    connect(&handle, &QSerialPort::readyRead, this, &UARTCommunicator::hnd_readyRead);
    connect(&handle, &QSerialPort::errorOccurred, this, &UARTCommunicator::hnd_error);
    connect(&handle, SIGNAL(aboutToClose()), this, SIGNAL(disconnected()));
}

bool UARTCommunicator::open(const QString &port, const QHash<QString, QVariant>& cfg)
{
    QSettings set;
    buffer.clear();
    return cfg.isEmpty() ? QComPort::setupComPortFromSettings(port, set, RS485_SETTINGS_PREFIX, handle) :
                           QComPort::setupComPortFromHash(port, cfg, RS485_SETTINGS_PREFIX, handle);
}

UARTMessage UARTCommunicator::get()
{
    return inbox.isEmpty() ? UARTMessage(0xFF,QByteArray()) : inbox.dequeue();
}

bool UARTCommunicator::waitMessageReady(int ms)
{
    if(ms==0)
    {
        return !inbox.isEmpty();
    }


    QEventLoop evt;
    connect(this, &UARTCommunicator::readyRead, &evt, &QEventLoop::quit);
    QTimer timeTicker(this);
    timeTicker.setSingleShot(true);
    connect(&timeTicker, &QTimer::timeout, &evt, &QEventLoop::quit);
    connect(&timeTicker, &QTimer::timeout, this, [=]() {
        qWarning()<<"EventLoop time ticker fired, queue size "<<queue();
    });
    if(ms>0)
    {
        ms+=500;
        timeTicker.setInterval(ms);
        timeTicker.start();
    }
    evt.exec();

    timeTicker.stop();
    return (ms<=0)||!inbox.isEmpty()||(handle.isOpen() && handle.bytesAvailable());

}

bool UARTCommunicator::sendPattern()
{
    static const QByteArray pattern("\x55\x00\x55\x00\x7E\xFF\x7E\xFF", 8);
    if(handle.isOpen()) {
        qWarning("To UART: %s", pattern.toHex().toUpper().constData());
        return (handle.write(pattern, 8) == 8) && handle.flush();
    }
    return false;
}

bool UARTCommunicator::sendto(uint8_t addr, const QByteArray& msg)
{
    if(handle.isOpen())
    {
        QByteArray uartmsg(4+msg.size(),char(0));
        uint8_t* buf = (uint8_t*)uartmsg.data();
        buf[0] = UART_COM_HEADER;
        buf[1] = msg.size();
        buf[2] = addr;
        if(msg.size()) {
            memcpy(buf+3, msg.constData(), buf[1]);
        }
        buf[3+buf[1]] = uart_crc8(buf, 3+buf[1]);
        qWarning("To UART (l:%d): %s", uartmsg.size(), uartmsg.toHex().toUpper().constData());
        return (handle.write(uartmsg) == uartmsg.size()) && handle.flush();
    }
    return false;
}

void UARTCommunicator::hnd_readyRead()
{
    {
        QByteArray dt;
        while(!(dt=handle.readAll()).isEmpty())
        {
            buffer.append(dt);
        }
    }
    qWarning()<<"UART buffer (size "<<buffer.size()<<"): "<<QString::fromLatin1(buffer.toHex().toUpper());
    int consumed = 0;
    do {
        consumed = 0;
        const uint8_t* buf = (const uint8_t*)buffer.constData();
        while((consumed < buffer.size()) && (*buf != UART_COM_HEADER))
        {
            ++consumed;
            ++buf;
        }

        if((buffer.size() - consumed) >= 4)
        {
            uint8_t data_len = buf[1];
            if((consumed+4+data_len) <= buffer.size())
            {
                if(uart_crc8(buf, 3+data_len) == buf[3+data_len])
                {
                    qWarning()<<"Assembled message from "<<int(buf[2])<<" of size "<<int(data_len);
                    qWarning()<<"  :"<<QByteArray((const char*)buf+3, int(data_len));
                    inbox.append(UARTMessage(buf[2], QByteArray((const char*)buf+3, int(data_len))));
                    qWarning()<<"Emit readyread!";
                    emit readyRead();
                }
                else
                {
                    qWarning()<<"UART message dropped because of invalid CRC";
                }
                consumed += 4+data_len;
            }
        }

        if(consumed)
        {
            if(consumed==buffer.size())
            {
                buffer.clear();
            }
            else
            {
                buffer = buffer.mid(consumed);
            }
        }
    }while(consumed && buffer.size());
}

void UARTCommunicator::hnd_error(QSerialPort::SerialPortError error)
{
    if(error != QSerialPort::NoError) {
        qWarning()<<"Serial "<<handle.portName()<<" error: "<<error<<", "<<handle.errorString();
        switch(error)
        {
            case QSerialPort::DeviceNotFoundError:
            case QSerialPort::NotOpenError:
            case QSerialPort::WriteError:
            case QSerialPort::ReadError:
            case QSerialPort::ResourceError:
            case QSerialPort::TimeoutError:
            {
                qWarning()<<"Fatal serial port error"<<error;
                if(handle.isOpen())
                {
                    handle.close();
                    emit disconnected();
                }
                break;
            }
            default:break;
        }
    }
}
