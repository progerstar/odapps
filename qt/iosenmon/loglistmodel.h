#ifndef LOGLISTMODEL_H
#define LOGLISTMODEL_H

#include <QAbstractListModel>
#include "hidsensorinterface.h"

#ifdef WITH_QML
#include <QQmlEngine>
#endif

typedef QPair<QString,QString> LogEntry;

inline bool operator==(const LogEntry& e1, const LogEntry& e2)
{
    return ((e1.first == e2.first) && (e1.second == e2.second));
}

inline bool operator<(const LogEntry& e1, const LogEntry& e2)
{
    return ((e1.first<e2.first) || ((e1.first==e2.first)&&(e1.second<e2.second)));
}

inline bool operator>(const LogEntry& e1, const LogEntry& e2)
{
    return (e2<e1);
}

class LogListModel : public QAbstractListModel
{
        Q_OBJECT
        Q_PROPERTY(QString name READ name NOTIFY nameChanged)
        Q_PROPERTY(int capacity READ capacity WRITE setCapacity NOTIFY capacityChanged)
    public:
        enum Roles
        {
            TimeRole = Qt::UserRole + 1,
            ValueRole,
        };

#ifdef WITH_QML
        static inline void declareQML()
        {
            qmlRegisterType<LogListModel>("IOTRunner", 1, 0, "LogListModel");
        }
#endif

        explicit LogListModel(QObject *parent = nullptr);
        ~LogListModel();


        int rowCount(const QModelIndex &parent = QModelIndex()) const override;
        QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

        inline QString name() const { return log_name; }
        inline void setName(const QString& n)
        {
            if(log_name!=n)
            {
                log_name = n;
                emit nameChanged(n);
            }
        }

        inline int capacity() const { return log_capacity;}
        void setCapacity(int c);
    signals:
        void nameChanged(const QString& name);
        void capacityChanged(int c);
    public slots:
        void clear();
        void init(const QString& table, const QList<LogEntry>& entries);
        void insert(const LogEntry& entry);
    protected:
        virtual QHash<int,QByteArray> roleNames() const override;

    private:
        QString log_name;
        int log_capacity;
        QList<LogEntry> items;
};

#endif // LOGLISTMODEL_H
