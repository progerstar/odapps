#include "sensorlistmodel.h"

#include <QIcon>

SensorListModel::SensorListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

SensorListModel::~SensorListModel()
{
    //do not own the items
    items.clear();
}

int SensorListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return items.size();
}

QVariant SensorListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    switch(role)
    {
        case Qt::DisplayRole:
        {
            return items.at(index.row())->serial();
        }
        case Qt::ToolTip:
        {
            return items.at(index.row())->type()+"<br>"+
                    items.at(index.row())->firmware();
        }
        case Qt::DecorationRole:
        {
            return QIcon(items.at(index.row())->icon());
        }
        case TypeRole:
        {
            return items.at(index.row())->type();
        }
        case UnitRole:
        {
            return items.at(index.row())->unit();
        }
        case SerialRole:
        {
            return items.at(index.row())->serial();
        }
        case DataRole:
        {
            return items.at(index.row())->getData();
        }
        case StateRole:
        {
            return items.at(index.row())->getState();
        }
        case FirmwareRole:
        {
            return items.at(index.row())->firmware();
        }
        default:break;
    }

    return QVariant();
}

void SensorListModel::update(QList<HidSensorInterface *> newItems)
{
    if(items.size())
    {
        beginRemoveRows(QModelIndex(),0,items.size()-1);
        items.clear();
        endRemoveRows();
    }
    if(newItems.size())
    {
        beginInsertRows(QModelIndex(),0,newItems.size()-1);
        items = newItems;
        endInsertRows();
    }
}

void SensorListModel::itemUpdated(HidSensorInterface* item)
{
    int i = items.indexOf(item);
    if(i>=0)
    {
        QModelIndex ind = this->index(i);
        emit dataChanged(ind,ind,QVector<int>()<<Qt::DisplayRole<<DataRole<<StateRole);
    }
}

void SensorListModel::itemRemove(HidSensorInterface* item)
{
    int i = items.indexOf(item);
    if(i>=0)
    {
        beginRemoveRows(QModelIndex(),i,i);
        items.removeAt(i);
        endRemoveRows();
    }
}

void SensorListModel::itemAdd(HidSensorInterface* item)
{
    if(!items.contains(item))
    {
        beginInsertRows(QModelIndex(),items.size(),items.size());
        items.append(item);
        endInsertRows();
    }
}

QHash<int,QByteArray> SensorListModel::roleNames() const
{
    QHash<int,QByteArray> roles;
    roles[TypeRole]      = "stype";
    roles[UnitRole]      = "sunit";
    roles[SerialRole]    = "sserial";
    roles[DataRole]      = "sdata";
    roles[StateRole]     = "sstate";
    roles[FirmwareRole]  = "sfw";
    return roles;
}
