#ifndef QL_CHANNEL_H
    #define QL_CHANNEL_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QVariant>

class QlChannel : public QObject {
    Q_OBJECT

        Q_PROPERTY(QString name READ name NOTIFY nameChanged)
    public:
        explicit QlChannel(QObject* parent = nullptr);
        ~QlChannel() override = default;

        Q_INVOKABLE virtual QStringList channels();

        Q_INVOKABLE virtual bool open(const QString &name);
        Q_INVOKABLE virtual void close();
        Q_INVOKABLE virtual bool isOpen();
        Q_INVOKABLE virtual QString name();

        Q_INVOKABLE virtual QStringList params();
        Q_INVOKABLE virtual QString param(const QString &name);
        Q_INVOKABLE virtual bool paramSet(const QString &name, const QString &value);

        Q_INVOKABLE virtual QString readString();
        Q_INVOKABLE virtual bool writeString(const QString &s);

    signals:
        void nameChanged(const QString& name);

    protected:
        QString     name_;
        QStringList params_;
};

#endif
