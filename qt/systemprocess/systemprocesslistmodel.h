#ifndef SYSTEMPROCESSLISTMODEL_H
#define SYSTEMPROCESSLISTMODEL_H

#include <systemprocess.h>

#include <QAbstractListModel>
#include <QMultiHash>
#include <QFutureWatcher>

class SystemProcessListModel : public QAbstractListModel
{
        Q_OBJECT

    public:
        enum Roles
        {
            NameRole = Qt::UserRole + 1,
            PathRole,
            CountRole,
            MemRole,
            StatusRole
        };

        enum Status
        {
            Unknown = 0,
            Running,
            NotResponding,
        };
        Q_ENUM(Status)

#ifdef WITH_QML
        static inline void declareQML()
        {
            qmlRegisterType<SystemProcessListModel>("ru.opendev.systemprocess",1,0, "SystemProcessModel");
        }
#endif

        explicit SystemProcessListModel(QObject *parent = nullptr);
        ~SystemProcessListModel();

        int rowCount(const QModelIndex &parent = QModelIndex()) const override;
        QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    signals:
        void reloaded();
    public slots:
        void reload();
    protected:
        virtual QHash<int,QByteArray> roleNames() const override;
    private slots:
        void procEnumerated();
    private:
        QMultiHash<QString,SystemProcess> procHash;
        QStringList procNames;
        QFutureWatcher<QList<SystemProcess> > procUpdateFuture;
};

#endif // SYSTEMPROCESSLISTMODEL_H
