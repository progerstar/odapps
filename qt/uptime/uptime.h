#ifndef UPTIME_H
#define UPTIME_H

#include <QtGlobal>
#include <QObject>

#ifdef Q_OS_WIN
quint64 getSysUptimeMS();
#elif defined(Q_OS_DARWIN)
#include <sys/sysctl.h>
inline quint64 getSysUptimeMS()
{
    struct timeval boottime;
    size_t len = sizeof(boottime);
    int mib[2] = { CTL_KERN, KERN_BOOTTIME };
    if( sysctl(mib, 2, &boottime, &len, NULL, 0) < 0 )
    {
        return 0;
    }
    time_t bsec = boottime.tv_sec, csec = time(NULL);
    return quint64(1000)*(csec-bsec);
}
#elif defined(Q_OS_UNIX)
#include <sys/sysinfo.h>
inline quint64 getSysUptimeMS()
{
    struct sysinfo inf;
    if(sysinfo(&inf)==0)
    {
        return quint64(1000)*inf.uptime;
    }
    return 0;
}
#else
#error "Defined uptime measurement for your OS"
#endif

quint64 getAppUptimeMS();


inline QString uptimeString(quint64 ms)
{
    quint64 sec = ms / 1000;
    if(sec > 129600UL)
    {
        // > 36hours
        return QString("%1d:%2h").arg(sec/86400,2,10,QLatin1Char('0')).arg((sec % 86400) / 3600,2,10,QLatin1Char('0'));
    }
    else if(sec > 5400)
    {
        return QString("%1h:%2m").arg(sec/3600,2,10,QLatin1Char('0')).arg((sec % 3600)/60,2,10,QLatin1Char('0'));
    }
    return QString("%1:%2:%3")
            .arg(sec/3600,2,10,QLatin1Char('0'))
            .arg((sec%3600)/60,2,10,QLatin1Char('0'))
            .arg(sec%60,2,10,QLatin1Char('0'));
}
#endif // UPTIME_H
