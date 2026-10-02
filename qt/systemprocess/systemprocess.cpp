#include "systemprocess.h"

#include <QFileInfo>
#include <QProcess>
#include <QDebug>

#ifdef Q_OS_LINUX
#include <stdlib.h>
QList<SystemProcess> systemProcesses_linux()
{
    QList<SystemProcess> ret;
    QProcess psProc;
    psProc.start(QLatin1String("ps"), {"h", "-N", "-U", "root", "-o", "pid,pmem,stat,vsz"}, QProcess::ReadOnly);
    //psProc.start("ps h -N -U root -o pid,pmem,stat,vsz", QProcess::ReadOnly);
    if(!psProc.waitForStarted() || !psProc.waitForFinished())
    {
        qWarning()<<"ps failed to start/finish: "<<psProc.errorString();
        return ret;
    }

    QStringList ps = QString::fromLocal8Bit(psProc.readAllStandardOutput()).split("\n", Qt::SkipEmptyParts);
    char rpath[PATH_MAX];
    foreach(const QString& proc, ps)
    {
        //PID PMEM STAT VSZ
        QStringList cols = proc.split(" ", Qt::SkipEmptyParts);
        if(cols.size()!=4)
        {
            qWarning()<<"Skip invalid ps line "<<proc;
            continue;
        }
        if(cols.at(3).toInt()==0)
        {
            //kthread - ignore
            continue;
        }
        //get process executable
        QString exeFile = QString("/proc/%1/exe").arg(cols.at(0));
        if(!QFile(exeFile).exists() || (realpath(exeFile.toLocal8Bit().constData(),rpath)==NULL))
        {
            qWarning()<<"Cannot resolve "<<exeFile;
            continue;
        }

        SystemProcess sproc;
        sproc.path = QString::fromLocal8Bit(rpath,qMin<size_t>(strlen(rpath),size_t(PATH_MAX)));
        sproc.name = QFileInfo(sproc.path).fileName();
        sproc.pmem = cols.at(1).toDouble();
        sproc.status = 0;
        if(cols.at(2).contains('R'))
        {
            sproc.status |= SystemProcess::Running;
        }
        if(cols.at(2).contains('S'))
        {
            sproc.status |= SystemProcess::Waiting;
        }
        if(cols.at(2).contains('D'))
        {
            sproc.status |= SystemProcess::IO;
        }
        if(cols.at(2).contains('T'))
        {
            sproc.status |= SystemProcess::Stopped;
        }
        if(cols.at(2).contains('t'))
        {
            sproc.status |= SystemProcess::Debugged;
        }
        if(cols.at(2).contains('X'))
        {
            sproc.status |= SystemProcess::Dead;
        }
        if(cols.at(2).contains('Z'))
        {
            sproc.status |= SystemProcess::Zombie;
        }
        if(cols.at(2).contains('+'))
        {
            sproc.status |= SystemProcess::Foreground;
        }
        if(cols.at(2).contains('<'))
        {
            sproc.status |= SystemProcess::HighPriority;
        }
        if(cols.at(2).contains('N'))
        {
            sproc.status |= SystemProcess::LowPriority;
        }
        ret.append(sproc);
    }
    return ret;
}
#elif defined (Q_OS_WIN)

#if 0
/*
 * Code example:
 * https://msdn.microsoft.com/en-us/library/windows/desktop/ms682623(v=vs.85).aspx
 */

#include <windows.h>
#include <tchar.h>
#include <psapi.h>

static inline QString QStringFromTCHAR(TCHAR* ptr)
{
#ifdef _UNICODE
    return QString::fromWCharArray(ptr);
#else
    return QString::fromLocal8Bit(ptr);
#endif
}

QList<SystemProcess> systemProcesses_win()
{
    QList<SystemProcess> ret;
    TCHAR szProcessName[MAX_PATH] = TEXT("<unknown>");
    DWORD procIDs[1024];
    DWORD retBytes;
    DWORD procCount;

    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if(!GlobalMemoryStatusEx(&memInfo))
    {
        qWarning()<<"Cannot query system memory - memory usage info will be unavailable";
        memInfo.ullTotalPhys = 0;
    }

    HANDLE pHnd;
    HMODULE hMod;
    PROCESS_MEMORY_COUNTERS pMemCnt;

    if(!EnumProcesses(procIDs,sizeof(procIDs),&retBytes))
    {
        qWarning()<<"EnumProcesses failed";
        return ret;
    }
    procCount = retBytes/sizeof(DWORD);
    for(DWORD i=0;i<procCount;++i)
    {
        if(procIDs[i]!=0)
        {
            pHnd = OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,procIDs[i]);
            if(pHnd != NULL)
            {
                if(EnumProcessModules(pHnd,&hMod,sizeof(hMod),&retBytes))
                {
                    GetModuleBaseName( pHnd, hMod, szProcessName, sizeof(szProcessName)/sizeof(TCHAR) );
                    SystemProcess sproc;
                    sproc.name = QStringFromTCHAR(szProcessName);
                    GetModuleFileNameEx( pHnd, hMod, szProcessName, sizeof(szProcessName)/sizeof(TCHAR));
                    sproc.path = QStringFromTCHAR(szProcessName);

                    if(GetProcessMemoryInfo(pHnd, &pMemCnt, sizeof(pMemCnt)))
                    {
                        sproc.pmem = memInfo.ullTotalPhys ? pMemCnt.WorkingSetSize / double(memInfo.ullTotalPhys) : 0;
                        sproc.pmem = qRound(sproc.pmem*10)/10.;
                    }
                    else
                    {
                        sproc.pmem = 0.;
                    }
                    sproc.status=0;
                    ret.append(sproc);
                }
                else
                {
                    qWarning()<<"Cannot get HMODULE for process "<<procIDs[i];
                }
                CloseHandle(pHnd);
            }
            else
            {
                qWarning()<<"Skipping process ID "<<procIDs[i]<<" - cannot open handle";
            }
        }
    }
    return ret;
}
#endif

#if 0
#include <QAxObject>
#include <qt_windows.h>

QList<SystemProcess> systemProcesses_win()
{
    QList<SystemProcess> ret;
    HRESULT hr = CoInitialize(0);
    QAxObject *objIWbemLocator = 0;
    QAxObject *objWMIService = 0;
    QAxObject *objIterList = 0;
    QAxObject *objEnum;
    IEnumVARIANT *ifEnum;
    QAxObject *objItem = 0;
    VARIANT *varItem = 0;
    quint64 pPrivateBytes = 0;
    int count;
    SystemProcess sproc;

    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if(!GlobalMemoryStatusEx(&memInfo))
    {
        qWarning()<<"Cannot query system memory - memory usage info will be unavailable";
        memInfo.ullTotalPhys = 0;
    }

    objIWbemLocator = new QAxObject("WbemScripting.SWbemLocator");
    if(!objIWbemLocator)
    {
        qWarning()<<"WbemScripting.SWbemLocator not created";
        goto bail;
    }

    objWMIService = objIWbemLocator->querySubObject(
                        "ConnectServer(QString&,QString&)",
                        QString("."), QString("ROOT\\CIMV2"));
    if (!objWMIService)
    {
        qWarning()<<"WMIService not created";
        goto bail;
    }

    objIterList = objWMIService->querySubObject("ExecQuery(QString&)",QString("SELECT * FROM Win32_PerfRawData_PerfProc_Process"));
    if(!objIterList)
    {
        qWarning()<<"WMI ExecQuery returned NULL";
        goto bail;
    }

    count = objIterList->propertyBag().value("Count",-1).toInt();
    qWarning()<<"Process count: "<<count;

    if(count<0)
    {
        qWarning()<<"Process query failed: count < 0";
        goto bail;
    }


    objEnum = objIterList->querySubObject("_NewEnum");
    if(!objEnum)
    {
        qWarning()<<"Query _NewEnum failed";
        goto bail;
    }

    objEnum->queryInterface(IID_IEnumVARIANT,(void**)&ifEnum);
    if(!ifEnum)
    {
        qWarning()<<"Query interface failed";
        goto bail;
    }

    ifEnum->Reset();

    varItem = (VARIANT*)malloc(sizeof(VARIANT));
    ret.reserve(count);
    for(int i=0;i<count;++i)
    {
        if(ifEnum->Next(1,varItem,NULL) == S_FALSE)
        {
            qWarning()<<"GetNext process failed";
            break;
        }

        objItem = new QAxObject((IUnknown*)varItem->punkVal);
        if(objItem->dynamicCall("ExecutablePath").toString().isEmpty())
        {
            qWarning()<<"No ExecutablePath for "<<objItem->dynamicCall("Name").toString();
        }
        else
        {
            sproc.name = objItem->dynamicCall("Name").toString();
            sproc.path = objItem->dynamicCall("ExecutablePath").toString();
            pPrivateBytes = objItem->dynamicCall("PrivateBytes").toULongLong();
            sproc.pmem = memInfo.ullTotalPhys ? pPrivateBytes / double(memInfo.ullTotalPhys) : 0;
            sproc.pmem = qRound(sproc.pmem*10)/10.;
            sproc.status = 0;
            if(sproc.name.endsWith(".exe",Qt::CaseInsensitive))
            {
                ret.append(sproc);
            }
        }
        delete objItem;objItem=0;
    }
    if(varItem){free(varItem);varItem=0;}
    ifEnum->Release();

bail:
    if(objItem)
    {
        delete objItem;
        objItem = 0;
    }
    if(varItem)
    {
        free(varItem);
        varItem = 0;
    }
    if(objWMIService)
    {
        delete objWMIService;
    }
    if(objIWbemLocator)
    {
        delete objIWbemLocator;
    }
    CoUninitialize();
    return ret;
}
#endif

/*
 * Code example from
 * https://msdn.microsoft.com/en-us/library/ms686701(v=vs.85).aspx
 */

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <tchar.h>

QList<SystemProcess> systemProcesses_win()
{
    QList<SystemProcess> ret;
    HANDLE hProcessSnap;
    HANDLE hProcess;
    PROCESSENTRY32W pe32;
    WCHAR wName[PATH_MAX];
    DWORD wNameSize;
    PROCESS_MEMORY_COUNTERS_EX pmc;

    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if(!GlobalMemoryStatusEx(&memInfo))
    {
        qWarning()<<"Cannot query system memory - memory usage info will be unavailable";
        memInfo.ullTotalPhys = 0;
    }

    // Take a snapshot of all processes in the system.
    hProcessSnap = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
    if( hProcessSnap == INVALID_HANDLE_VALUE )
    {
        qWarning()<<"CreateToolhelp32Snapshot (of processes) fails";
        return ret;
    }

    // Set the size of the structure before using it.
    pe32.dwSize = sizeof( PROCESSENTRY32W );

    // Retrieve information about the first process,
    // and exit if unsuccessful
    if( !Process32FirstW( hProcessSnap, &pe32 ) )
    {
        qWarning()<<"Process32First failed";
        CloseHandle( hProcessSnap );          // clean the snapshot object
        return ret;
    }

    // Now walk the snapshot of processes, and
    // display information about each process in turn
    do
    {
        SystemProcess sproc;
        sproc.path = "";
        sproc.status = 0;
        sproc.pmem = -1;
        sproc.name = QString::fromWCharArray(pe32.szExeFile);

        // Retrieve the full path
        hProcess = OpenProcess( PROCESS_ALL_ACCESS, FALSE, pe32.th32ProcessID );
        if( hProcess == NULL )
        {
            qWarning()<<"Cannot open process handle for "<<sproc.name;
            sproc.path = sproc.name;
        }
        else
        {
            wNameSize = sizeof(wName);
            if(QueryFullProcessImageNameW(hProcess, 0, wName, &wNameSize))
            {
                sproc.path = QString::fromWCharArray(wName,wNameSize);

#if 0 /*How to get 'Working Set - Private'? */
                ZeroMemory(&pmc, sizeof(PROCESS_MEMORY_COUNTERS_EX));
                if(GetProcessMemoryInfo( hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc) ))
                {
                    qWarning()<<"Private memory of "<<sproc.path<<" is "<<(pmc.PrivateUsage / double(1024*1024))
                             <<", working set "<<(pmc.WorkingSetSize / double(1024*1024));
                    sproc.pmem = memInfo.ullTotalPhys ? pmc.PrivateUsage / double(memInfo.ullTotalPhys) : 0;
                    sproc.pmem = qRound(sproc.pmem*10)/10.;
                }
                else
                {
                    sproc.pmem = -1;
                }
#endif
                ret.append(sproc);
            }
            else
            {
                qWarning()<<"Cannot get full path of "<<sproc.name;
            }
            CloseHandle( hProcess );
        }

    } while( Process32Next( hProcessSnap, &pe32 ) );

    CloseHandle( hProcessSnap );
    return ret;
}

#elif defined(Q_OS_DARWIN)

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/sysctl.h>
#include <libproc.h>
#include <mach/mach.h>

/*
 * Code example:
 * https://developer.apple.com/legacy/library/qa/qa2001/qa1123.html
 */

QList<SystemProcess> systemProcesses_osx()
{
    int                 err = 0;
    kinfo_proc *        result;
    static const int    name[] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
    size_t              length;
    bool                done;
    size_t              procCount = 0;
    char                pathbuf[PROC_PIDPATHINFO_MAXSIZE];
    struct proc_bsdshortinfo bsdinfo;
    int                 iret;

    long                total_phymem;
    int mib[2] = {CTL_HW, HW_PHYSMEM};
    size_t              pmem_len = sizeof(total_phymem);
    if (sysctl(mib, 2, &total_phymem, &pmem_len, NULL, 0) == -1)
    {
        qWarning()<<"Cannot get total physical memory value";
        total_phymem = 0;
    }

    // We start by calling sysctl with result == NULL and length == 0.
    // That will succeed, and set length to the appropriate length.
    // We then allocate a buffer of that size and call sysctl again
    // with that buffer.  If that succeeds, we're done.  If that fails
    // with ENOMEM, we have to throw away our buffer and loop.  Note
    // that the loop causes use to call sysctl with NULL again; this
    // is necessary because the ENOMEM failure case sets length to
    // the amount of data returned, not the amount of data that
    // could have been returned.

    result = NULL;
    done = false;
    do
    {
        // Call sysctl with a NULL buffer.
        length = 0;
        err = sysctl( (int *) name, (sizeof(name) / sizeof(*name)) - 1, NULL, &length, NULL, 0);
        if (err == -1)
        {
            err = errno;
        }

        // Allocate an appropriately sized buffer based on the results
        // from the previous call.

        if (err == 0)
        {
            result = (kinfo_proc*)malloc(length);
            if (result == NULL)
            {
                err = ENOMEM;
            }
        }

        // Call sysctl again with the new buffer.  If we get an ENOMEM
        // error, toss away our buffer and start again.

        if (err == 0)
        {
            err = sysctl( (int *) name, (sizeof(name) / sizeof(*name)) - 1, result, &length, NULL, 0);
            if (err == -1)
            {
                err = errno;
            }
            if (err == 0)
            {
                done = true;
            }
            else if (err == ENOMEM)
            {
                free(result);
                result = NULL;
                err = 0;
            }
        }
    } while (err == 0 && ! done);

    // Clean up and establish post conditions.

    QList<SystemProcess> ret;

    if (err != 0 && result != NULL)
    {
        qWarning()<<"Process list failed with error "<<err;
        free(result);
        result = NULL;
        return ret;
    }

    procCount = length / sizeof(kinfo_proc);
    qWarning()<<"Got "<<procCount<<" processes";
    ret.reserve(procCount);
    for(size_t i=0;i<procCount;++i)
    {
        SystemProcess sproc;

        //sproc.name = result[i].kp_proc.p_comm; <- 16byte limit

        if(proc_pidpath(result[i].kp_proc.p_pid,pathbuf,sizeof(pathbuf))>0)
        {
            sproc.path = QString::fromLocal8Bit(pathbuf);
            sproc.name = QFileInfo(sproc.path).fileName();
        }
        else
        {
            qWarning()<<"Skipping "<<result[i].kp_proc.p_comm<<" as it has no path";
            continue;
        }

        sproc.pmem = -1;

        iret = proc_pidinfo(result[i].kp_proc.p_pid,PROC_PIDT_SHORTBSDINFO,0,&bsdinfo,sizeof(bsdinfo));
        sproc.status = 0;
        if(iret == sizeof(bsdinfo))
        {
            if(bsdinfo.pbsi_status & (SIDL|SSLEEP))
            {
                sproc.status |= SystemProcess::Waiting;
            }
            if(bsdinfo.pbsi_status & SRUN)
            {
                sproc.status |= SystemProcess::Running;
            }
            if(bsdinfo.pbsi_status & SSTOP)
            {
                sproc.status |= SystemProcess::Stopped;
            }
            if(bsdinfo.pbsi_status & SZOMB)
            {
                sproc.status |= SystemProcess::Zombie;
            }
        }
        else
        {
            qWarning()<<"Cannot get process info for "<<sproc.name;
        }
        ret.append(sproc);
    }

    free(result);
    result = NULL;
    return ret;
}

#endif

QList<SystemProcess> SystemProcess::systemProcesses()
{
#ifdef Q_OS_LINUX
    return systemProcesses_linux();
#elif defined(Q_OS_WIN)
    return systemProcesses_win();
#elif defined(Q_OS_DARWIN)
    return systemProcesses_osx();
#else
    return QList<SystemProcess>();
#endif
}

SystemProcess::SystemProcess(const SystemProcess& other) : QObject(other.parent()),
    name(other.name), path(other.path),pmem(other.pmem),status(other.status)
{

}

SystemProcess& SystemProcess::operator= (const SystemProcess& other)
{
    if(this != &other)
    {
        name = other.name;
        path = other.path;
        pmem = other.pmem;
        status = other.status;
    }
    return *this;
}
