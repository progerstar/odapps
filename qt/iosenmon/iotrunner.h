#ifndef IOTRUNNER_H
#define IOTRUNNER_H

#include <QObject>
#include <QSqlDatabase>
#include <QSettings>
#include <QTimerEvent>

#include <qjournald.h>
#include <qmuhttp.h>

#include "iosenmon_global.h"
#include "hidsensorinterface.h"
#include "sensorlistmodel.h"
#include "loglistmodel.h"

class IOTRunner : public QObject
{
        Q_OBJECT
        Q_PROPERTY(HidSensorInterface* sensor READ sensor NOTIFY sensorChanged)
    public:
        Q_INVOKABLE static inline QString stateString(quint8 val)
        {
            switch(val)
            {
                case HidSensorInterface::Normal: return "normal";
                case HidSensorInterface::Acceptable: return "acceptable";
                case HidSensorInterface::Critical: return "critical";
                case HidSensorInterface::InternalError: return "interr";
                default: break;
            }
            return "custom";
        }
        Q_INVOKABLE QString locStateString(quint8 val);

        Q_INVOKABLE static bool fileExists(const QString& name);

        explicit IOTRunner(QObject *parent = nullptr);
        ~IOTRunner();

        inline void setJournal(QJournal* j)
        {
            journal = j;
        }

        Q_INVOKABLE inline HidSensorInterface* sensor() const { return current_sensor;}
        /* the other channel of the current sensor's device (humidity next to temperature), or null */
        Q_INVOKABLE inline HidSensorInterface* companion() const { return companion_sensor;}
        Q_INVOKABLE HidSensorInterface* select(const QString& type, const QString& sn);
        Q_INVOKABLE inline SensorListModel* sensorModel() { return &_sensorModel;}
        Q_INVOKABLE inline LogListModel* logModel() { return &_logModel; }
        Q_INVOKABLE QByteArray toJSON() const;
        Q_INVOKABLE inline QString alias(const QString& sn) const
        {
            return aliasHash.value(sn,sn).toString();
        }
        Q_INVOKABLE void setAlias(const QString& sn, const QString& name);
        /* a value of the log (always metric) in the units the current sensor shows; NaN if it is not a number */
        Q_INVOKABLE double logValue(const QString& metric) const;
        Q_INVOKABLE QString suggestLogName() const;
        Q_INVOKABLE QString suggestLogPath() const;
    signals:
        void sensorChanged(HidSensorInterface* obj);
        void companionChanged(HidSensorInterface* obj);
        void dataChanged(double value);
        void companionDataChanged(double value);
        void stateChanged(int type);
        void aliasChanged(const QString& serial, const QString& alias);
        void error(const QString& descr);
    public slots:
        void scan();
        void cleanDatabase();
        void settingsUpdated();
        void logExport(const QString& file);
    private slots:
        void sensor_dataChanged(double value);
        void sensor_configChanged();
        void sensor_event(int type);
        void sensor_error(const QString& descr);
        void http_message(HttpClient* client);
    protected:
        void timerEvent(QTimerEvent* e);
    private:
        QSettings set;
        QJournal* journal;
        QMUHttpServer* server;
        QList<HidSensorInterface*> sensors;
        HidSensorInterface* current_sensor;
        HidSensorInterface* companion_sensor;
        SensorListModel _sensorModel;
        LogListModel _logModel;
        QHash<QString,QVariant> aliasHash;
        QSqlDatabase db;
        uint http_port;

        QList<HidSensorInterface *> open(const QString& device);
        void setupCurrentSensor();
        bool findCompanion();
        void updateCompanion();

        QByteArray http_serve(const QString& path, const QString& query = QString());
        QString http_mime_detect(const QString& path);

        QStringList db_tables();
        QString logTitle(HidSensorInterface* sensor) const;
        QString db_name(HidSensorInterface* sensor);
        QString db_name(const QString& type, const QString& uuid);
        QList<LogEntry> db_entries(const QString& table, int max = -1, bool new_first = true);
        bool db_register(const QString& table);
        bool db_save(const QString& table, const QVariant& value);
};

#endif // IOTRUNNER_H
