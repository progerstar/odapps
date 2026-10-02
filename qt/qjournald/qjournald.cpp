#include "qjournald.h"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QDebug>
#include <QStandardPaths>

QJournal::QJournal(QObject *parent,const QString &file) : QObject(parent),
    _paused(false), maxsize(MEGA_BYTES(64)), timestamp(true), backup_cnt(3)
{
    fname = file;
}

QJournal::~QJournal()
{

}

QString QJournal::mkdir(QString pattern)
{
    if(pattern.isEmpty() || (!QDir(pattern).exists() && !QDir().mkpath(pattern)))
    {
        pattern = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).at(0);
    }
    if(!QDir(pattern).exists())
    {
        QDir().mkpath(pattern);
    }

    return pattern;
}

void QJournal::setup(QString logdir, const QString& fileName)
{
    logdir = mkdir(logdir);
    qWarning()<<"Logging to "<<logdir;
    setLogFile(logdir+"/"+fileName);
}

void QJournal::log(const QString &data)
{
    if(_paused) return;
    if(!checkFile())
    {
        qWarning()<<"File check failed!";
        return;
    }

    QFile of(fname);
    if(of.open(QFile::WriteOnly|QFile::Append))
    {
        if(timestamp)
        {
            of.write(QDateTime::currentDateTime().toString("ddd MMM d hh:mm:ss t yyyy ").toUtf8());
        }
        of.write(data.toUtf8());
#ifdef Q_OS_WIN
        of.write("\r\n");
#else
        of.write("\n");
#endif
        of.flush();
        of.close();
    }
}

bool QJournal::checkFile()
{
    if(fname.isEmpty())
    {
        qWarning()<<"Cannot check file - empty filename";
        return false;
    }


    QFileInfo fi(fname);
    if(!fi.absoluteDir().exists() && !QDir().mkpath(fi.absolutePath()))
    {
        qWarning()<<"Cannot log to "<<fname<<" - directory "<<fi.absolutePath()<<" cannot be created";
        return false;
    }
    if(fi.exists() && (quint64(fi.size()) >= maxsize))
    {
        if(backup_cnt==0)
        {
            return QFile(fname).remove();
        }

        //logrotate
        qint64 i=0;
        while(QFileInfo(fname+"."+QString::number(i)).exists()) ++i;
        while(i>=(backup_cnt-1))
        {
            QFile(fname+"."+QString::number(i)).remove();
            --i;
        }

        while(i>=0)
        {
            QFile(fname+"."+QString::number(i)).rename(fname+"."+QString::number(i+1));
            --i;
        }

        return QFile(fname).rename(fname+".0");
    }
    return true;
}
