#include "watcher.hpp"

#include <QtConcurrent/QtConcurrentRun>
#include <QDebug>
#include <QFileInfo>
#include <QProcess>
#include <QRegExp>
#include <QStringList>

namespace {
bool waitForProcess(QProcess& process)
{
    if(process.waitForStarted(3000) && process.waitForFinished(3000))
    {
        return true;
    }

    process.kill();
    process.waitForFinished(1000);
    return false;
}
}

ProcessWatcher::ProcessWatcher(QObject *parent) : QObject(parent), watcher(this)
{
    connect(&watcher, &QFutureWatcher<int>::finished, this, [this]() {
        emit finished(watcher.result());
    });
}

bool ProcessWatcher::fileExists(const QString& name)
{
    const QFileInfo info(name);
    return info.exists() && info.isFile();
}

bool ProcessWatcher::start(const QString& name)
{
    const QString processName = name.trimmed();
    if(processName.isEmpty() || watcher.isRunning())
    {
        return false;
    }

    watcher.setFuture(QtConcurrent::run([processName]() {
        return ProcessWatcher::checkPlatform(processName);
    }));
    return true;
}

#ifdef Q_OS_LINUX
int ProcessWatcher::checkLinux(const QString& name)
{
    QProcess process;
    process.start(QStringLiteral("ps"), QStringList()<<QStringLiteral("-C")<<name<<QStringLiteral("-o")<<QStringLiteral("pid="));
    if(!waitForProcess(process))
    {
        return 1;
    }

    const QStringList pids = QString::fromLocal8Bit(process.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if(pids.isEmpty())
    {
        return 1;
    }

    bool ok = false;
    const QString processId = pids.first().trimmed();
    processId.toUInt(&ok);
    if(!ok)
    {
        qWarning()<<processId<<" is not an integer";
        return 1;
    }

    process.start(QStringLiteral("ps"), QStringList()<<QStringLiteral("-p")<<processId<<QStringLiteral("-o")<<QStringLiteral("stat="));
    if(!waitForProcess(process))
    {
        return 1;
    }

    const QString state = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    qWarning()<<"Process "<<name<<" ["<<processId<<"] state "<<state;
    return QRegExp(QStringLiteral("[TXZ](.*)")).exactMatch(state) ? 1 : 0;
}
#endif

#ifdef Q_OS_DARWIN
int ProcessWatcher::checkMacx(const QString& name)
{
    QProcess process;
    process.start(QStringLiteral("pgrep"), QStringList()<<QStringLiteral("-x")<<name);
    if(!waitForProcess(process))
    {
        return 1;
    }

    const QString processId = QString::fromLocal8Bit(process.readAllStandardOutput())
            .split(QLatin1Char('\n'), Qt::SkipEmptyParts).value(0).trimmed();
    bool ok = false;
    processId.toUInt(&ok);
    if(!ok)
    {
        return 1;
    }

    process.start(QStringLiteral("ps"), QStringList()<<QStringLiteral("-p")<<processId<<QStringLiteral("-o")<<QStringLiteral("stat="));
    if(!waitForProcess(process))
    {
        return 1;
    }

    const QString state = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    return QRegExp(QStringLiteral("[TXZE](.*)")).exactMatch(state) ? 1 : 0;
}
#endif

#ifdef Q_OS_WIN
int ProcessWatcher::checkWin(const QString& name)
{
    QProcess process;
    process.start(QStringLiteral("tasklist"), QStringList()<<QStringLiteral("/FI")
                  <<QStringLiteral("IMAGENAME eq %1").arg(name)<<QStringLiteral("/FO")
                  <<QStringLiteral("CSV")<<QStringLiteral("/NH"));
    if(!waitForProcess(process))
    {
        return 1;
    }

    QString state = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    if(!state.contains(name, Qt::CaseInsensitive))
    {
        return 1;
    }

    process.start(QStringLiteral("tasklist"), QStringList()<<QStringLiteral("/FI")
                  <<QStringLiteral("IMAGENAME eq %1").arg(name)<<QStringLiteral("/FI")
                  <<QStringLiteral("STATUS eq NOT RESPONDING")<<QStringLiteral("/FO")
                  <<QStringLiteral("CSV")<<QStringLiteral("/NH"));
    if(!waitForProcess(process))
    {
        return 1;
    }

    state = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    return state.contains(name, Qt::CaseInsensitive) ? 1 : 0;
}
#endif

int ProcessWatcher::checkPlatform(const QString& name)
{
#ifdef Q_OS_LINUX
    return checkLinux(name);
#elif defined(Q_OS_DARWIN)
    return checkMacx(name);
#elif defined(Q_OS_WIN)
    return checkWin(name);
#else
    Q_UNUSED(name)
    return 1;
#endif
}

int ProcessWatcher::check(const QString& name)
{
    return checkPlatform(name.trimmed());
}
