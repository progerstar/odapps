#ifndef SENSORLISTMODEL_H
#define SENSORLISTMODEL_H

#include <QAbstractListModel>
#include "hidsensorinterface.h"

#ifdef WITH_QML
#include <QQmlEngine>
#endif

class SensorListModel : public QAbstractListModel
{
        Q_OBJECT

    public:
        enum Roles
        {
            TypeRole = Qt::UserRole + 1,
            UnitRole,
            SerialRole,
            DataRole,
            StateRole,
            FirmwareRole,
        };

#ifdef WITH_QML
        static inline void declareQML()
        {
            qmlRegisterType<SensorListModel>("IOTRunner", 1, 0, "SensorListModel");
        }
#endif

        explicit SensorListModel(QObject *parent = nullptr);
        ~SensorListModel();


        int rowCount(const QModelIndex &parent = QModelIndex()) const override;

        QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    public slots:
        void update(QList<HidSensorInterface*> newItems);
        void itemUpdated(HidSensorInterface* item);
        void itemRemove(HidSensorInterface* item);
        void itemAdd(HidSensorInterface* item);
    protected:
        virtual QHash<int,QByteArray> roleNames() const override;

    private:
        QList<HidSensorInterface*> items;
};

#endif // SENSORLISTMODEL_H
