#include "iotrunner.h"
#include "ds18b20sensor.h"
#include "hdc1080sensor.h"
#include "dhtsensor.h"

#include <QSet>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QDir>

#include <QSqlQuery>
#include <QSqlError>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

#include <QMimeDatabase>
#include <QMimeType>

#include <QDebug>

#define DB_DATE_TIME "yyyy-MM-dd HH:mm:ss"
#define DB_DATE_TIME_FULL "yyyy-MM-dd HH:mm:ss.zzz"

#define DB_APP_TYPE "APP"
#define DB_APP_SERIAL "IOSENMON"

/* table names are built from the device-supplied type/serial: make them safe to quote */
static QString sqlQuote(const QString& name)
{
    QString n(name);
    return n.replace('\'', "''");
}

bool IOTRunner::fileExists(const QString& name)
{
    return QFileInfo(name).exists() && QFileInfo(name).isFile();
}

IOTRunner::IOTRunner(QObject *parent) : QObject(parent),
    journal(0), server(new QMUHttpServer(this)),
    current_sensor(0), _sensorModel(this), _logModel(this)
{
    aliasHash = set.value(SETTINGS_ALIASES).toHash();
    QString dbFile = set.value(SETTINGS_DB_LOC,"").toString();
    if(dbFile.isEmpty() || !QFile(dbFile).exists())
    {
        dbFile = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).at(0)+"/iosenmon.db";
    }
    if(!QFileInfo(dbFile).absoluteDir().exists())
    {
        //best we can do
        QDir().mkpath(QFileInfo(dbFile).absolutePath());
    }
    db = QSqlDatabase::addDatabase("QSQLITE", DB_CONN_NAME);
    db.setDatabaseName(QFileInfo(dbFile).absoluteFilePath());
    if(!db.open())
    {
        qWarning()<<"Cannot open database file "<<dbFile<<" - logging will be unavailable";
    }
    else
    {
        set.setValue(SETTINGS_DB_LOC,QFileInfo(dbFile).absoluteFilePath());
        db_register(db_name(DB_APP_TYPE,DB_APP_SERIAL));
    }
    connect(server,SIGNAL(newMessage(HttpClient*)),this,SLOT(http_message(HttpClient*)));
    http_port = set.value(SETTINGS_HTTP_PORT,SETTINGS_HTTP_PORT_DEFAULT).toUInt();
    if(set.value(SETTINGS_HTTP_ENABLED,false).toBool())
    {
        server->start();
    }
    startTimer(3600*1000);
}

IOTRunner::~IOTRunner()
{
    if(db.isOpen())
    {
        db.close();
    }
    journal = 0;
    current_sensor = 0;
    _sensorModel.update(QList<HidSensorInterface*>());
    qDeleteAll(sensors);
    sensors.clear();
}

QString IOTRunner::locStateString(quint8 val)
{
    switch(val)
    {
        case HidSensorInterface::Normal:        return tr("normal");
        case HidSensorInterface::Acceptable:    return tr("acceptable");
        case HidSensorInterface::Critical:      return tr("critical");
        case HidSensorInterface::InternalError: return tr("internal error");
        default: break;
    }
    return tr("custom");
}

HidSensorInterface* IOTRunner::select(const QString& type, const QString& sn)
{
    foreach(HidSensorInterface* sensor, sensors)
    {
        if((sensor->type()==type)&&(sensor->serial()==sn))
        {
            current_sensor = sensor;
            setupCurrentSensor();
            break;
        }
    }
    return current_sensor;
}

QByteArray IOTRunner::toJSON() const
{
    QJsonObject root;
    root.insert("version", QLatin1String(IOSENMON_VERSION));
    root.insert("timestamp",QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
    QJsonArray objects;
    foreach(HidSensorInterface* sensor, sensors)
    {
        QJsonObject obj;
        obj.insert("id",QString("/%1/%2").arg(sensor->type()).arg(sensor->serial()));
        obj.insert("type",sensor->type());
        obj.insert("unit",sensor->unit());
        obj.insert("serial",sensor->serial());
        obj.insert("alias", alias(sensor->serial()));
        obj.insert("value",sensor->getData());
        obj.insert("min",sensor->dataMin());
        obj.insert("max",sensor->dataMax());
        obj.insert("state",sensor->getState());
        objects.append(obj);
    }
    root.insert("sensors",objects);
    return QJsonDocument(root).toJson();
}

void IOTRunner::setAlias(const QString& sn, const QString& name)
{
    /* the alias equal to the serial number (or empty) means "no alias" */
    QString newAlias = (name == sn) ? QString() : name;
    if(aliasHash.value(sn).toString() != newAlias)
    {
        if(newAlias.isEmpty())
        {
            aliasHash.remove(sn);
        }
        else
        {
            aliasHash[sn] = newAlias;
        }
        set.setValue(SETTINGS_ALIASES,aliasHash);
        if(current_sensor && (current_sensor->serial() == sn))
        {
            _logModel.setName(logTitle(current_sensor));
        }
        emit aliasChanged(sn,alias(sn));
    }
}

QString IOTRunner::suggestLogName() const
{
    return (current_sensor ? (current_sensor->type() + "_" + current_sensor->serial()) : "xxxxxxxx")+"_"+QDateTime::currentDateTime().toString("yyyyMMddhhmmss")+".txt";
}

QString IOTRunner::suggestLogPath() const
{
    return QDir::toNativeSeparators(set.value(SETTINGS_DB_SAVEPATH,QStandardPaths::standardLocations(QStandardPaths::HomeLocation).at(0)).toString()+"/"+suggestLogName());
}

void IOTRunner::scan()
{
    QSet<QString> existingSensors;
    foreach(HidSensorInterface* sensor, sensors)
    {
        existingSensors.insert(sensor->type()+QString(" (%1)").arg(sensor->serial()));
    }
    qDebug()<<"Existing sensors: "<<existingSensors;

    const QStringList found = HidSensorInterface::availableDevices();
    QSet<QString> devices(found.constBegin(), found.constEnd());
    qWarning()<<"Discovered sensors: "<<devices;
    devices.subtract(existingSensors);
    qWarning()<<"New sensors: "<<devices;
    for(QSet<QString>::const_iterator it = devices.constBegin();it!=devices.constEnd();++it)
    {
        QList<HidSensorInterface*> newSensors = open(*it);
        if(!newSensors.isEmpty())
        {
            foreach(HidSensorInterface* sns, newSensors)
            {
                sensors.append(sns);
                _sensorModel.itemAdd(sns);
            }
        }
        else
        {
            qWarning()<<"Sensor "<<(*it)<<" could not be opened";
        }
    }

    if((current_sensor == 0)&&(!sensors.isEmpty()))
    {
        current_sensor = sensors.at(0);
        setupCurrentSensor();
    }
}

void IOTRunner::sensor_dataChanged(double value)
{
    HidSensorInterface* sensor = qobject_cast<HidSensorInterface*>(sender());
    qDebug()<<"Sensor "<<sensor<<" reports "<<value;
    /* the database (and the log view) always keep the metric value */
    db_save(db_name(sensor),sensor->getMetricData());
    if(sensor == current_sensor)
    {
        _logModel.insert(LogEntry(QDateTime::currentDateTime().toString(DB_DATE_TIME_FULL),QString::number(sensor->getMetricData())));
        emit dataChanged(value);
    }
    _sensorModel.itemUpdated(sensor);
}

void IOTRunner::sensor_configChanged()
{
    HidSensorInterface* sensor = qobject_cast<HidSensorInterface*>(sender());
    if(current_sensor && (sensor == current_sensor))
    {
        //update UI
        emit sensorChanged(current_sensor);
    }
}

void IOTRunner::sensor_event(int type)
{
    HidSensorInterface* sensor = qobject_cast<HidSensorInterface*>(sender());
    db_save(db_name(DB_APP_TYPE,DB_APP_SERIAL),QString("%1-%2: %3").arg(sensor->type()).arg(sensor->serial()).arg(stateString(quint8(type))));
    if(journal)
    {
        journal->log(tr("Sensor %1-%2 state %3").arg(sensor->type()).arg(sensor->serial()).arg(locStateString(quint8(type))));
    }
    if(sensor == current_sensor)
    {
        emit stateChanged(type);
    }
    _sensorModel.itemUpdated(sensor);
}

void IOTRunner::sensor_error(const QString& descr)
{
    HidSensorInterface* sensor = qobject_cast<HidSensorInterface*>(sender());
    qWarning()<<"Sensor "<<sensor<<" error "<<descr;
    if(journal)
    {
        journal->log(tr("Sensor %1-%2 error : %3").arg(sensor->type()).arg(sensor->serial()).arg(descr));
    }

    _sensorModel.itemRemove(sensor);
    sensors.removeAll(sensor);
    sensor->disconnect(this);

    if(sensor == current_sensor)
    {
        current_sensor = sensors.isEmpty() ? 0 : sensors.at(0);
        setupCurrentSensor();
        emit error(descr);
    }

    sensor->deleteLater();
}

void IOTRunner::http_message(HttpClient* client)
{
    if(client->method==HTTP_GET)
    {
        QUrl get_url(QString::fromUtf8(client->request_url));

        if(get_url.path()=="/")
        {
            qDebug()<<"Request main page";
            client->fd->write(http_serve("index.html"));
            client->fd->flush();
        }
        else if(get_url.path()=="/json")
        {
            QByteArray jsonData = toJSON();
            QTextStream os(client->fd);
            os.setCodec("UTF-8");
            os<<"HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json; charset=utf-8\r\n"
                "Content-Length: "<<jsonData.size()
             <<"\r\n\r\n";
            os.flush();
            client->fd->write(jsonData);
        }
        else
        {
            client->fd->write(http_serve(get_url.path(), get_url.query()));
            client->fd->flush();
        }
    }
#if 0
    else if(client->method == HTTP_POST)
    {

    }
#endif
    client->keep_alive = false;
    client->replied();
}

QByteArray IOTRunner::http_serve(const QString& path, const QString& query)
{
    Q_UNUSED(query);
    /* never let the request leave the html resource directory */
    const QString clean = QDir::cleanPath("/"+path).mid(1);
    if(clean.isEmpty() || path.contains(QLatin1String("..")))
    {
        return "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    }
    if(QFile("://www/html/"+clean).exists())
    {
        QFile fd("://www/html/"+clean);
        if(fd.open(QFile::ReadOnly))
        {
            QByteArray data = fd.readAll();
            fd.close();

            return QString("HTTP/1.1 200 OK\r\n"
                           "Content-Type: %1; charset=utf-8\r\n"
                           "Content-Length: %2\r\n"
                           "\r\n").arg(http_mime_detect(clean)).arg(data.size())
                    .toUtf8() + data;
        }
    }

    return "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
}

QString IOTRunner::http_mime_detect(const QString &path)
{
    QMimeType mime = QMimeDatabase().mimeTypeForFile(path);
    qDebug()<<"detected mime-type of "<<path<<": "<<mime.name();
    return mime.isValid() ? mime.name() : "text/plain";
}

void IOTRunner::timerEvent(QTimerEvent *e)
{
    Q_UNUSED(e);
    qWarning()<<"Hourly maintenance: clean database";
    cleanDatabase();
}

void IOTRunner::cleanDatabase()
{
    if(!db.isOpen())
    {
        return;
    }
    QDateTime expireEpoch = QDateTime::currentDateTime().addSecs(qint64(set.value(SETTINGS_DB_AGE,SETTINGS_DB_AGE_DEFAULT).toUInt())*-1);
    QStringList tables = db_tables();
    qWarning()<<"Removing entries older than "<<expireEpoch.toString(DB_DATE_TIME)<<" from "<<tables;
    db.transaction();
    foreach(const QString& table, tables)
    {
        QSqlQuery query(db);
        if(query.exec(QString("DELETE FROM '%1' WHERE (strftime('%s', Time) < strftime('%s', '%2'))")
                      .arg(sqlQuote(table)).arg(expireEpoch.toString(DB_DATE_TIME))))
        {

            qWarning()<<"Removed "<<query.numRowsAffected()<<" expired rows from "<<table;
            if(journal && query.numRowsAffected())
            {
                journal->log(tr("Removed %1 rows from %2").arg(query.numRowsAffected()).arg(table));
            }
        }
        else
        {
            qWarning()<<"Failed to clean expired entries from "<<table<<": "<<query.lastError().text();
            if(journal)
            {
                journal->log(tr("%1 cleanup failed: %2").arg(table).arg(query.lastError().text()));
            }
        }
    }
    db.commit();
}

void IOTRunner::settingsUpdated()
{
    const uint port = set.value(SETTINGS_HTTP_PORT,SETTINGS_HTTP_PORT_DEFAULT).toUInt();
    if(set.value(SETTINGS_HTTP_ENABLED,false).toBool())
    {
        /* restart on a port change */
        if(!server->isRunning() || (port != http_port))
        {
            server->start();
        }
        http_port = port;
    }
    else
    {
        server->stop();
    }
    foreach(HidSensorInterface* sensor, sensors)
    {
        sensor->getCurrentData();
    }
    if(_sensorModel.rowCount())
    {
        /*units might have changed*/
        emit _sensorModel.dataChanged(_sensorModel.index(0),_sensorModel.index(_sensorModel.rowCount()-1));
    }
}

QList<HidSensorInterface*> IOTRunner::open(const QString& device)
{
    QList<HidSensorInterface*> ret;
    if(device.startsWith("DS18B20"))
    {
        HidSensorInterface* sns = new DS18B20Sensor(this);
        if(!sns->open(device,HidSensorInterface::Standalone,0))
        {
            delete sns;
        }
        else
        {
            ret.append(sns);
        }
    }
    else if(device.startsWith("DHT"))
    {
        HidSensorInterface* sns_t = new DHTSensor(HDC1080Sensor::HDC1080_Temp, this);
        HidSensorInterface* sns_h = new DHTSensor(HDC1080Sensor::HDC1080_Hum, this);
        if(!sns_t->open(device,HidSensorInterface::Master, sns_h))
        {
            delete sns_h;
            delete sns_t;
        }
        else
        {
            /*Slave open cannot fail*/
            sns_h->open(device, HidSensorInterface::Slave, sns_t);
            ret.append(sns_t);
            ret.append(sns_h);
        }
    }
    else if(device.startsWith("HDC1080")||device.startsWith("HDC2080"))
    {
        HDC1080Sensor::Mod hdc_mod = device.startsWith("HDC2080") ? HDC1080Sensor::HDC1080_2 : HDC1080Sensor::HDC1080_1;
        HidSensorInterface* sns_t = new HDC1080Sensor(hdc_mod, HDC1080Sensor::HDC1080_Temp, this);
        HidSensorInterface* sns_h = new HDC1080Sensor(hdc_mod, HDC1080Sensor::HDC1080_Hum, this);
        if(!sns_t->open(device,HidSensorInterface::Master, sns_h))
        {
            delete sns_h;
            delete sns_t;
        }
        else
        {
            /*Slave open cannot fail*/
            sns_h->open(device, HidSensorInterface::Slave, sns_t);
            ret.append(sns_t);
            ret.append(sns_h);
        }
    }
    else
    {
        qWarning()<<"Unknown sensor type "<<device;
    }

    foreach(HidSensorInterface* sns, ret)
    {
        connect(sns,SIGNAL(dataChanged(double)),this,SLOT(sensor_dataChanged(double)));
        connect(sns,SIGNAL(configChanged()),this,SLOT(sensor_configChanged()));
        connect(sns,SIGNAL(stateChanged(int)),this,SLOT(sensor_event(int)));
        connect(sns,SIGNAL(error(QString)),this,SLOT(sensor_error(QString)));
        db_register(db_name(sns));
        if(journal)
        {
            journal->log(tr("Sensor connected: %1 - %2").arg(sns->type()).arg(sns->serial()));
        }
        (void)sns->getCurrentData();
    }
    return ret;
}

void IOTRunner::setupCurrentSensor()
{
    if(!current_sensor)
    {
        _logModel.clear();
    }
    else
    {
        _logModel.init(logTitle(current_sensor),
                       db_entries(db_name(current_sensor),_logModel.capacity()));
    }
    emit sensorChanged(current_sensor);
}

QStringList IOTRunner::db_tables()
{
    QStringList ret;
    QSqlQuery query(db);
    if(query.exec("SELECT name FROM sqlite_master WHERE type='table'"))
    {
        while(query.next())
        {
            ret.append(query.value(0).toString());
        }
    }
    else
    {
        qWarning()<<"db_tables: sql query failed: "<<query.lastError().text();
    }
    return ret;
}

QString IOTRunner::logTitle(HidSensorInterface* sensor) const
{
    return sensor->type()+"_"+sensor->serial()+
            (aliasHash.contains(sensor->serial()) ?
                 QString(" (%1)").arg(aliasHash.value(sensor->serial()).toString()) : QString());
}

QString IOTRunner::db_name(HidSensorInterface* sensor)
{
    return db_name(sensor->type(),sensor->serial());
}

QString IOTRunner::db_name(const QString& type, const QString& uuid)
{
    return type+"_"+uuid;
}

QList<LogEntry> IOTRunner::db_entries(const QString& table, int max, bool new_first)
{
    QList<LogEntry> ret;
    QSqlQuery query(db);
    /* Sub is the (unpadded) milliseconds: order by it numerically, show it zero-padded */
    if(!query.exec(QString("SELECT (Time || '.' || printf('%03d', Sub)) as FTime, Data from '%1' ORDER BY Time %2, Sub %2 %3")
                   .arg(sqlQuote(table)).arg(new_first ? QLatin1String("DESC") : QLatin1String("ASC"))
                   .arg((max>0) ? QString("LIMIT %1").arg(max) : QString())))
    {
        qWarning()<<"DB query "<<query.lastQuery()<<" failed for "<<table<<": "<<query.lastError().text();
        if(journal)
        {
            journal->log(tr("Cannot query data from %1: %2").arg(table).arg(query.lastError().text()));
        }
    }
    else
    {
        while(query.next())
        {
            ret.append(LogEntry(query.value(0).toString(),query.value(1).toString()));
        }
    }
    return ret;
}

bool IOTRunner::db_register(const QString& table)
{
    QSqlQuery query(db);
    if(!query.exec(QString("CREATE TABLE IF NOT EXISTS '%1' ('Time' TEXT NOT NULL, 'Sub' INTEGER, 'Data' TEXT NOT NULL)").arg(sqlQuote(table))))
    {
        qWarning()<<"Cannot create table for "<<table<<": "<<query.lastError().text();
        if(journal)
        {
            journal->log(tr("Cannot create table %1: %2").arg(table).arg(query.lastError().text()));
        }
        return false;
    }
    return true;
}

bool IOTRunner::db_save(const QString& table, const QVariant& value)
{
    QSqlQuery query(db);
    query.prepare(QString("INSERT INTO '%1' (Time,Sub,Data) VALUES (?, ?, ?)").arg(sqlQuote(table)));
    QDateTime cts = QDateTime::currentDateTime();
    query.addBindValue(cts.toString(DB_DATE_TIME));
    query.addBindValue(cts.time().msec());
    query.addBindValue(value.toString());
    if(!query.exec())
    {
        qWarning()<<"Cannot log to table "<<table<<": "<<query.lastError().text();
        if(journal)
        {
            journal->log(tr("Cannot log %1 to table %2: %3").arg(value.toString()).arg(table).arg(query.lastError().text()));
        }
        return false;
    }
    return true;
}

void IOTRunner::logExport(const QString &file)
{
    qWarning()<<"Request logExport to "<<file;
    if(!current_sensor) {
        emit error(tr("No active sensor"));
        qWarning()<<"No active sensor - cannot export DB";
        return;
    }

    QFile logfile(QUrl(file).toLocalFile());
    if(!logfile.open(QFile::WriteOnly|QFile::Text)) {
        emit error(tr("Cannot export")+":<br>"+logfile.errorString());
        qWarning()<<"Cannot write to "<<logfile.fileName()<<": "<<logfile.errorString();
        return;
    }

    QList<LogEntry> entries = db_entries(db_name(current_sensor->type(),current_sensor->serial()),-1,false);
    foreach(const LogEntry& entry, entries)
    {
        logfile.write(QString("%1\t%2\n").arg(entry.first,entry.second).toUtf8());
    }
    set.setValue(SETTINGS_DB_SAVEPATH,QFileInfo(logfile.fileName()).absolutePath());
    logfile.close();
    QDesktopServices::openUrl(file);
}
