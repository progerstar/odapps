#include "mifarekeydatabase.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QSettings>

#define TABLE_VERSION_MAJ 0
#define TABLE_VERSION_MIN 2

MifareKeyDatabase::MifareKeyDatabase(QObject *parent) : QObject(parent), database_opened(false)
{
    QString dbFile = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).at(0)+"/odrfidkit.db";
    if(!QFileInfo(dbFile).absoluteDir().exists())
    {
        QDir().mkpath(QFileInfo(dbFile).absolutePath());
    }

    qDebug()<<"Using database "<<dbFile;
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "ODRFIDKit");
    db.setDatabaseName(dbFile);
    if((database_opened = db.open()))
    {
        qDebug()<<"Database opened";
        QSqlQuery query(db);
        bool recreate_buildinfo = false;

        if(!query.exec("SELECT VERSION FROM BUILDINFO WHERE NAME='ODRFIDKIT'")
           || !query.next())
        {
            //no buildinfo -> invalid table;
            qDebug()<<"Empty/Invalid database - no buildinfo";
            query.exec("DROP TABLE IF EXISTS BUILDINFO");
            query.exec("DROP TABLE IF EXISTS MIFARECLASSIC");
            query.exec("DROP TABLE IF EXISTS MIFAREPLUS");
            query.exec("DROP TABLE IF EXISTS MIFAREULTRA");
            recreate_buildinfo = true;
        }
        else
        {
            QString ver = query.value(0).toString();
            qDebug()<<"Database version "<<ver;
            if((ver.split(".",QString::SkipEmptyParts).size()!=2)||
               (ver.split(".",QString::SkipEmptyParts).first().toInt()!=TABLE_VERSION_MAJ)||
               (ver.split(".",QString::SkipEmptyParts).last().toInt()!=TABLE_VERSION_MIN))
            {
                qWarning()<<"Database is outdated "<<ver<<" vs "
                         <<TABLE_VERSION_MAJ<<"."<<TABLE_VERSION_MIN<<" -> dropping";
                query.exec("DROP TABLE IF EXISTS BUILDINFO");
                query.exec("DROP TABLE IF EXISTS MIFARECLASSIC");
                query.exec("DROP TABLE IF EXISTS MIFAREPLUS");
                query.exec("DROP TABLE IF EXISTS MIFAREULTRA");
                recreate_buildinfo = true;
            }
            else
            {
                if(query.next())
                {
                    //BUG: more than one entry
                    query.exec("DROP TABLE IF EXISTS BUILDINFO");
                    recreate_buildinfo = true;
                }
            }
        }
        query.finish();

        db.transaction();
        if(recreate_buildinfo)
        {
            if(!query.exec("CREATE TABLE IF NOT EXISTS BUILDINFO(NAME TEXT UNIQUE, DATE TEXT, VERSION TEXT)"))
            {
                qWarning()<<"database: buildinfo create failure - "<<query.lastError().text();
                db.commit();
                return;
            }

            query.prepare("INSERT OR IGNORE INTO BUILDINFO VALUES(?,?,?)");
            query.addBindValue("ODRFIDKIT");
            query.addBindValue(QDateTime::currentDateTimeUtc().toString("yyyy.MM.dd hh:mm:ss"));
            query.addBindValue(QString("%1.%2").arg(TABLE_VERSION_MAJ).arg(TABLE_VERSION_MIN));
            if(!query.exec())
            {
                qWarning()<<"database: buildinfo query execution failure - "<<query.lastError().text();
                db.commit();
                return;
            }
            query.finish();
        }

        if(!query.exec("CREATE TABLE IF NOT EXISTS MIFARECLASSIC(UID TEXT NOT NULL, BLOCK INTEGER,"
                       " TYPE INTEGER, KEY TEXT NOT NULL, PRIMARY KEY(UID, BLOCK,TYPE))"))
        {
            qWarning()<<"database: mifare classic table creation failure - "<<query.lastError().text();
        }

        if(!query.exec("CREATE TABLE IF NOT EXISTS MIFAREPLUS(UID TEXT NOT NULL, BLOCK INTEGER,"
                       "KEY TEXT NOT NULL, PRIMARY KEY(UID, BLOCK))"))
        {
            qWarning()<<"database: mifare plus table creation failure - "<<query.lastError().text();
        }

        if(!query.exec("CREATE TABLE IF NOT EXISTS MIFAREULTRA(UID TEXT PRIMARY KEY, PWD INTEGER, PACK INTEGER)"))
        {
            qWarning()<<"database: mifare ultralight table creation failure - "<<query.lastError().text();
        }
        db.commit();
    }
}

MifareKeyDatabase::~MifareKeyDatabase()
{
    if(database_opened)
    {
        QSqlDatabase::database("ODRFIDKit").close();
    }
    QSqlDatabase::removeDatabase("ODRFIDKit");
}

MifareKeyDatabase* MifareKeyDatabase::instance()
{
    static MifareKeyDatabase singleton;
    return &singleton;
}

MifareUltralightEV1Security MifareKeyDatabase::ultralight(const QByteArray& uid, bool* ok)
{
    MifareUltralightEV1Security ret;
    if(ok) *ok = false;

    if(!database_opened)
    {
        return ret;
    }

    QSqlQuery query(QSqlDatabase::database("ODRFIDKit"));
    if(!query.exec(QString("SELECT PWD, PACK FROM MIFAREULTRA WHERE UID='%1'")
                   .arg(uidText(uid))) || !query.next())
    {
        qWarning()<<"Query failed or no such key: "<<query.lastError().text();
        return ret;
    }

    ret.password = query.value(0).toUInt(ok);
    if(ok && !*ok)
    {
        return  MifareUltralightEV1Security();
    }
    ret.pack = query.value(1).toUInt(ok);
    if(ok && !*ok)
    {
        return  MifareUltralightEV1Security();
    }
    return ret;
}

MifareClassicKey MifareKeyDatabase::classic(const QByteArray& uid, quint8 block,
                                            MifareClassicKeyType type, bool* ok)
{
    if(ok) *ok = false;

    if(!database_opened)
        return MifareClassicKey();

    QSqlQuery query(QSqlDatabase::database("ODRFIDKit"));
    if(!query.exec(QString("SELECT KEY FROM MIFARECLASSIC WHERE UID='%1' AND BLOCK='%2' AND TYPE='%3'")
                   .arg(uidText(uid)).arg(block).arg(type)) ||
       !query.next())
    {
        qWarning()<<"Query failed or no such key: sql error "<<query.lastError().text();
        return MifareClassicKey();
    }

    QString keyText = query.value(0).toString();
    MifareClassicKey key;
    if(!key.fromString(keyText))
    {
        qWarning()<<"Invalid key "<<keyText;
    }
    else
    {
        qDebug()<<"Found key for "<<uidText(uid)<<" @ "<<block;
        if(ok) *ok = true;
    }
    return key;
}

MifarePlusKey MifareKeyDatabase::plus(const QByteArray& uid, quint16 block, bool* ok)
{
    if(ok) *ok = false;

    if(!database_opened)
        return MifarePlusKey();

    QSqlQuery query(QSqlDatabase::database("ODRFIDKit"));
    if(!query.exec(QString("SELECT KEY FROM MIFAREPLUS WHERE UID='%1' AND BLOCK='%2'")
                   .arg(uidText(uid)).arg(block)) ||
       !query.next())
    {
        qWarning()<<"Query failed or no such key: sql error "<<query.lastError().text();
        return MifarePlusKey();
    }

    QString keyText = query.value(0).toString();
    MifarePlusKey key;
    if(!key.fromString(keyText))
    {
        qWarning()<<"Invalid key "<<keyText;
    }
    else
    {
        qDebug()<<"Found key for "<<uidText(uid)<<" @ "<<block;
        if(ok) *ok = true;
    }
    return key;
}

void MifareKeyDatabase::save(const QByteArray& uid, quint8 block, MifareClassicKeyType type, const MifareClassicKey& key)
{
    if(!database_opened || !QSettings().value(SETTINGS_KEYS,false).toBool())
    {
        qDebug()<<"Database not opened or key storage is disabled";
        return;
    }

    QSqlDatabase db = QSqlDatabase::database("ODRFIDKit");
    db.transaction();
    QSqlQuery query(db);
    if(!query.exec(QString("INSERT OR REPLACE INTO "
                           "MIFARECLASSIC (UID, BLOCK, TYPE, KEY) "
                           "VALUES('%1',%2,%3,'%4')")
                   .arg(uidText(uid)).arg(block).arg(type).arg(key.toString())))
    {
        qWarning()<<"key store failure - error "<<query.lastError().text();
        return;
    }
    query.finish();
    db.commit();
    qDebug()<<"Key for "<<uidText(uid)<<"@"<<block<<" is saved";
}

void MifareKeyDatabase::save(const QByteArray& uid, quint16 block, const MifarePlusKey& key)
{
    if(!database_opened || !QSettings().value(SETTINGS_KEYS,false).toBool())
    {
        qDebug()<<"Database not opened or key storage is disabled";
        return;
    }

    QSqlDatabase db = QSqlDatabase::database("ODRFIDKit");
    db.transaction();
    QSqlQuery query(db);
    if(!query.exec(QString("INSERT OR REPLACE INTO "
                           "MIFAREPLUS (UID, BLOCK, KEY) "
                           "VALUES('%1', %2, '%3')")
                   .arg(uidText(uid)).arg(block).arg(key.toString())))
    {
        qWarning()<<"key store failure - error "<<query.lastError().text();
        return;
    }
    query.finish();
    db.commit();
    qDebug()<<"Key for "<<uidText(uid)<<"@"<<block<<" is saved";
}

void MifareKeyDatabase::save(const QByteArray& uid, const MifareUltralightEV1Security& sec)
{
    if(!database_opened || !QSettings().value(SETTINGS_KEYS,false).toBool())
    {
        qDebug()<<"Database not opened or key storage is disabled";
        return;
    }

    QSqlDatabase db = QSqlDatabase::database("ODRFIDKit");
    QSqlQuery query(db);
    if(!query.exec(QString("INSERT OR REPLACE INTO MIFAREULTRA "
                                   " (UID, PWD, PACK) VALUES ('%1','%2', '%3')")
                           .arg(uidText(uid)).arg(sec.password).arg(sec.pack)))
    {
        qWarning()<<"password store failure - error "<<query.lastError().text();
    }
    else
    {
        qDebug()<<"password for "<<uidText(uid)<<" saved";
    }
}

void MifareKeyDatabase::cleanDatabase()
{
    if(database_opened)
    {
        QSqlDatabase db = QSqlDatabase::database("ODRFIDKit");
        QSqlQuery query(db);

        db.transaction();
        query.exec("DROP TABLE IF EXISTS BUILDINFO");
        query.exec("DROP TABLE IF EXISTS MIFARECLASSIC");
        query.exec("DROP TABLE IF EXISTS MIFAREULTRA");
        db.commit();

        db.transaction();

        if(!query.exec("CREATE TABLE IF NOT EXISTS BUILDINFO(NAME TEXT UNIQUE, DATE TEXT, VERSION TEXT)"))
        {
            qWarning()<<"database: buildinfo create failure - "<<query.lastError().text();
            db.commit();
            return;
        }

        query.prepare("INSERT OR IGNORE INTO BUILDINFO VALUES(?,?,?)");
        query.addBindValue("ODRFIDKIT");
        query.addBindValue(QDateTime::currentDateTimeUtc().toString("yyyy.MM.dd hh:mm:ss"));
        query.addBindValue(QString("%1.%2").arg(TABLE_VERSION_MAJ).arg(TABLE_VERSION_MIN));
        if(!query.exec())
        {
            qWarning()<<"database: buildinfo query execution failure - "<<query.lastError().text();
            db.commit();
            return;
        }
        query.finish();

        if(!query.exec("CREATE TABLE IF NOT EXISTS MIFARECLASSIC(UID TEXT NOT NULL, BLOCK INTEGER,"
                       " TYPE INTEGER, KEY TEXT NOT NULL, PRIMARY KEY(UID, BLOCK,TYPE))"))
        {
            qWarning()<<"database: mifare table create failure - "<<query.lastError().text();
        }
        db.commit();
    }
}
