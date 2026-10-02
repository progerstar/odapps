#ifndef QJOURNALD_H_
#define QJOURNALD_H_

#include <QObject>

#define KILO_BYTES(x) (quint64(1024)*(x))
#define MEGA_BYTES(x) (quint64(1024)*quint64(1024)*(x))
#define GIGA_BYTES(x) (quint64(1024)*quint64(1024)*quint64(1024)*(x))

class QJournal : public QObject
{
        Q_OBJECT
        Q_PROPERTY(QString file READ getLogFile WRITE setLogFile NOTIFY fileChanged)
        Q_PROPERTY(bool pause READ isPaused WRITE setPaused NOTIFY paused)
        Q_PROPERTY(quint64 size READ getMaxSize() WRITE setMaxSize)
        Q_PROPERTY(bool time READ getWriteTimeStamp() WRITE setWriteTimeStamp)
        Q_PROPERTY(uint backups READ getBackupCount WRITE setBackupCount)
    public:
        explicit QJournal(QObject* parent = NULL,const QString& file = QString());
        ~QJournal();

        static QString mkdir(QString pattern);

        Q_INVOKABLE void setup(QString logdir, const QString& fileName);

        Q_INVOKABLE inline QString getLogFile() const
        {
            return fname;
        }

        Q_INVOKABLE inline void setLogFile(const QString& file)
        {
            fname = file;
            emit fileChanged(file);
        }

        Q_INVOKABLE inline bool isPaused() const
        {
            return _paused;
        }

        Q_INVOKABLE inline void setPaused(bool on)
        {
            _paused = on;
        }

        Q_INVOKABLE inline quint64 getMaxSize() const
        {
            return maxsize;
        }
        Q_INVOKABLE inline void setMaxSizeMB(quint64 mb)
        {
            setMaxSize(MEGA_BYTES(mb));
        }
        Q_INVOKABLE inline void setMaxSize(quint64 ms)
        {
            /*no less than 128kB, no bigger than 2Gb*/
            maxsize = qMin<quint64>(GIGA_BYTES(2),qMax<quint64>(KILO_BYTES(128),ms));
            checkFile();
        }

        Q_INVOKABLE inline bool getWriteTimeStamp() const
        {
            return timestamp;
        }
        Q_INVOKABLE inline void setWriteTimeStamp(bool on)
        {
            timestamp = on;
        }

        Q_INVOKABLE inline uint getBackupCount() const
        {
            return backup_cnt;
        }
        Q_INVOKABLE inline void setBackupCount(uint cnt)
        {
            backup_cnt = qMin<uint>(uint(128),cnt);
            checkFile();
        }

    signals:
        void fileChanged(QString file);
        void paused(bool on);
    public slots:
        void log(const QString& data);
        inline void pause() { _paused = true; }
        inline void resume() { _paused = false; }
    private:
        QString fname;
        bool _paused;
        quint64 maxsize;
        bool    timestamp;
        uint  backup_cnt;

        bool checkFile();
};

#define QJOURNALD_LOD(logger,fmt,...) do{(logger).log(QString::asprintf(fmt,__VA_ARGS__))}while(0)
#define QJOURNALD_APP_LOG(logger,app,fmt,...) do{(logger).log(app+QLatin1String(": ")+QString::asprintf(fmt,__VA_ARGS__))}while(0)

#endif  // QJOURNALD_H_
