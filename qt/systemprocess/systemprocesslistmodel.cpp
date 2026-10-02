#include "systemprocesslistmodel.h"

#include <QtConcurrent>
#include <QBrush>
#include <QFileInfo>

#include <algorithm>

/*************************************/

SystemProcessListModel::SystemProcessListModel(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(&procUpdateFuture,SIGNAL(finished()),this,SLOT(procEnumerated()));
}

SystemProcessListModel::~SystemProcessListModel()
{
    procUpdateFuture.waitForFinished();
}

int SystemProcessListModel::rowCount(const QModelIndex &parent) const
{
    // For list models only the root node (an invalid parent) should return the list's size. For all
    // other (valid) parents, rowCount() should return 0 so that it does not become a tree model.
    if (parent.isValid())
        return 0;

    return procNames.size();
}

QVariant SystemProcessListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= procNames.size())
        return QVariant();

    switch(role)
    {
        case Qt::DisplayRole:
        {
            return procHash.value(procNames.at(index.row())).name + QString(" (%1)").arg(procHash.count(procNames.at(index.row())));
        }
        case Qt::DecorationRole:
        {
            bool ok = true;
            QList<SystemProcess> processes = procHash.values(procNames.at(index.row()));
            foreach(const SystemProcess& p, processes)
            {
                if(p.status & (SystemProcess::Dead|SystemProcess::Stopped|SystemProcess::Zombie))
                {
                    ok = false;
                    break;
                }
            }
            return ok ? QBrush(Qt::green) : QBrush(Qt::red);
        }
        case NameRole:
        {
            return procHash.value(procNames.at(index.row())).name;
        }
        case PathRole:
        {
            return procNames.at(index.row());
        }
        case CountRole:
        {
            return procHash.count(procNames.at(index.row()));
        }
        case MemRole:
        {
            double mem = 0;
            QList<SystemProcess> processes = procHash.values(procNames.at(index.row()));
            foreach(const SystemProcess& p, processes)
            {
                mem+=p.pmem;
            }
            return mem;
        }
        case StatusRole:
        {
            bool ok = true;
            bool detected = false;
            QList<SystemProcess> processes = procHash.values(procNames.at(index.row()));
            foreach(const SystemProcess& p, processes)
            {
                if(p.status) detected = true;
                if(p.status & (SystemProcess::Dead|SystemProcess::Stopped|SystemProcess::Zombie))
                {
                    ok = false;
                    break;
                }
            }
            if(!detected) return Unknown;
            return ok ? Running : NotResponding;
        }
        default:break;
    }

    return QVariant();
}

void SystemProcessListModel::reload()
{
    if(procUpdateFuture.isRunning())
    {
        qWarning()<<"Cannot reload model - update pending";
        return;
    }

    if(!procNames.isEmpty())
    {
        beginRemoveRows(QModelIndex(), 0, procNames.size() - 1);
        procHash.clear();
        procNames.clear();
        endRemoveRows();
    }

    procUpdateFuture.setFuture(QtConcurrent::run(SystemProcess::systemProcesses));
}

struct path_less_than
{
        inline bool operator()(const QString& path1, const QString& path2)
        {
            return QFileInfo(path1).fileName().toLower() < QFileInfo(path2).fileName().toLower();
        }
};

void SystemProcessListModel::procEnumerated()
{
    const QList<SystemProcess> procs = procUpdateFuture.result();
    if(!procs.isEmpty())
    {
        QMultiHash<QString, SystemProcess> newHash;
        QStringList newNames;
        for(const SystemProcess& process : procs)
        {
            if(!newHash.contains(process.path))
            {
                newNames.append(process.path);
            }
            newHash.insert(process.path, process);
        }
        std::sort(newNames.begin(), newNames.end(), path_less_than());

        beginInsertRows(QModelIndex(), 0, newNames.size() - 1);
        procHash = newHash;
        procNames = newNames;
        endInsertRows();
    }
    emit reloaded();
}

QHash<int,QByteArray> SystemProcessListModel::roleNames() const
{
    QHash<int,QByteArray> roles;
    roles[NameRole]   = "pname";
    roles[PathRole]   = "ppath";
    roles[CountRole]  = "pcount";
    roles[MemRole]    = "pmem";
    roles[StatusRole] = "pstat";
    return roles;
}
