#include "uptime.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <QLibrary>

typedef ULONGLONG (*GetTickCount64_Proto)();
static QLibrary kernelLib("Kernel32");
static GetTickCount64_Proto ticks_ptr = (GetTickCount64_Proto) kernelLib.resolve("GetTickCount64");

quint64 getSysUptimeMS()
{
    if(ticks_ptr)
    {
        return ticks_ptr();
    }
    return 0;
}

#endif

#include <QDateTime>

static QDateTime gStartupTime = QDateTime::currentDateTime();

quint64 getAppUptimeMS()
{
    return gStartupTime.msecsTo(QDateTime::currentDateTime());
}
