#ifndef QL_CHANNEL_SERIAL_H
#define QL_CHANNEL_SERIAL_H

#include <QtSerialPort/QSerialPort>
#include <QtSerialPort/QSerialPortInfo>
#include <QIODevice>
#include <QString>
#include <QDebug>
#include <QTimer>
#include "ql-channel.hpp"

#define OD_VID 0x0483

class QlChannelSerial : public QlChannel {
    Q_OBJECT

    public:
        explicit QlChannelSerial(QObject* parent = nullptr);
        ~QlChannelSerial() override;

        Q_INVOKABLE QStringList channels() override;

        Q_INVOKABLE bool open(const QString &name) override;
        Q_INVOKABLE void close() override;
        Q_INVOKABLE bool isOpen() override;
        Q_INVOKABLE QString name() override;

        Q_INVOKABLE QString param(const QString &name) override;
        Q_INVOKABLE bool paramSet(const QString &name, const QString &value) override;

        Q_INVOKABLE QString readString() override;
        Q_INVOKABLE bool writeString(const QString &s) override;

    signals:
        void dataReady(QString data);

    protected slots:
        void portReadyRead();
        void portError(QSerialPort::SerialPortError error);
    protected:
        QSerialPort *port_;
};

#endif
