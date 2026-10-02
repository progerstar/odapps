#ifndef UARTCOMMUNICATOR_H
#define UARTCOMMUNICATOR_H

#include <QObject>
#include <QQueue>
#include <QPair>

#include <QCommandLineOption>
#include <QCommandLineParser>

#include <qcomport.h>

#define RS485_SETTINGS_PREFIX ("RS485")
typedef QPair<uint8_t,QByteArray> UARTMessage;

class UARTCommunicator : public QObject
{
        Q_OBJECT
        Q_PROPERTY(int baud READ baud WRITE setBaud)
        Q_PROPERTY(uint8_t target READ target WRITE setTarget)
        Q_PROPERTY(QString portName READ portName)
    public:
        static bool messageIsValid(const UARTMessage& msg)
        {
            return (msg.first!=0xFF) || (!msg.second.isEmpty());
        }

        static void app_addParserOptions(QCommandLineParser& parser);
        static bool app_processParserOptions(QCommandLineParser& parser, QString& errorString);
        static bool app_parserOptionsSet(const QCommandLineParser& parser);

        explicit UARTCommunicator(QObject *parent = nullptr);
        ~UARTCommunicator()
        {
            handle.disconnect(this);
            close();
        }

        int baud() const
        {
            return baudrate;
        }

        void setBaud(int b)
        {
            baudrate = b;
            if(isOpen())
            {
                handle.setBaudRate(baudrate);
            }
        }

        uint8_t target() const
        {
            return _target_addr;
        }

        void setTarget(uint8_t addr)
        {
            _target_addr = addr;
        }

        Q_INVOKABLE bool isOpen() const
        {
            return  handle.isOpen();
        }

        QString portName() const
        {
            return handle.isOpen() ? (handle.portName()+QString(":%1").arg(_target_addr)) : QString();
        }

        Q_INVOKABLE bool open(const QString& port, const QHash<QString, QVariant>& cfg = QHash<QString,QVariant>());

        Q_INVOKABLE inline void close()
        {
            if(handle.isOpen())
            {
                handle.flush();
            }
            handle.close();
            buffer.clear();
        }

        Q_INVOKABLE void drop()
        {
            inbox.clear();
        }

        Q_INVOKABLE void clear()
        {
            while(handle.isOpen() && !handle.readAll().isEmpty()){}
        }

        Q_INVOKABLE inline bool pending() const
        {
            return !inbox.isEmpty();
        }

        Q_INVOKABLE inline int queue() const
        {
            return inbox.size();
        }

        Q_INVOKABLE UARTMessage get();

        Q_INVOKABLE bool waitMessageReady(int ms = 30000);
    signals:
        void readyRead();
        void disconnected();
    public slots:
        /* Baudrate detect (by 0x55) and clear (0x7E, 0xFF) */
        bool sendPattern();
        bool sendto(uint8_t addr, const QByteArray& msg);
        inline bool sendto(const UARTMessage& msg)
        {
            return sendto(msg.first, msg.second);
        }
        inline bool send(const QByteArray& msg)
        {
            return sendto(_target_addr, msg);
        }
        inline void readAll() {
            if(isOpen()) {
                hnd_readyRead();
            }
        }
    private slots:
        void hnd_readyRead();
        void hnd_error(QSerialPort::SerialPortError error);
    private:
        int baudrate;
        uint8_t _target_addr;
        QSerialPort handle;

        QByteArray buffer;
        QQueue<UARTMessage> inbox;
};

#endif // UARTCOMMUNICATOR_H
