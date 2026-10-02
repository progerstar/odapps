#include "loglistmodel.h"
#include "iosenmon_global.h"

LogListModel::LogListModel(QObject *parent)
    : QAbstractListModel(parent), log_name(""), log_capacity(SETTINGS_DB_ICOUNT_DEFAULT)
{
}

LogListModel::~LogListModel()
{

}

int LogListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return items.size();
}

QVariant LogListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    switch(role)
    {
        case Qt::DisplayRole:
        {
            return items.at(index.row()).first+": "+items.at(index.row()).second;
        }
        case TimeRole:
        {
            return items.at(index.row()).first;
        }
        case ValueRole:
        {
            return items.at(index.row()).second;
        }
        default:break;
    }

    return QVariant();
}

void LogListModel::setCapacity(int c)
{
    log_capacity = c;
    if(items.size() > c)
    {
        //remove last (c - items.size()) entries;
        beginRemoveRows(QModelIndex(),c,items.size()-1);
        items = items.mid(0,c);
        endRemoveRows();
    }
    emit capacityChanged(c);
}

void LogListModel::clear()
{
    if(log_name.size())
    {
        log_name = "";
        emit nameChanged(log_name);
    }
    if(items.size())
    {
        beginRemoveRows(QModelIndex(),0,items.size()-1);
        items.clear();
        endRemoveRows();
    }
}

void LogListModel::init(const QString& table, const QList<LogEntry>& entries)
{
    if(items.size())
    {
        beginRemoveRows(QModelIndex(),0,items.size()-1);
        items.clear();
        endRemoveRows();
    }

    log_name = table;
    emit nameChanged(log_name);

    if(entries.size())
    {
        int esz = qMin(log_capacity,entries.size());
        beginInsertRows(QModelIndex(),0,esz-1);
        items = entries.mid(0,esz);
        endInsertRows();
    }
}

void LogListModel::insert(const LogEntry& entry)
{
    if(items.size() == log_capacity)
    {
        beginRemoveRows(QModelIndex(),log_capacity-1,log_capacity-1);
        items.removeLast();
        endRemoveRows();
    }
    beginInsertRows(QModelIndex(),0,0);
    items.prepend(entry);
    endInsertRows();
}

QHash<int,QByteArray> LogListModel::roleNames() const
{
    QHash<int,QByteArray> roles;
    roles[TimeRole]  = "ltime";
    roles[ValueRole] = "ldata";
    return roles;
}
