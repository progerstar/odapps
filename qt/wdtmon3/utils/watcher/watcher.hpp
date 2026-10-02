#ifndef WATCHER_HPP
#define WATCHER_HPP

#include <QObject>
#include <QFutureWatcher>

class ProcessWatcher : public QObject
{
        Q_OBJECT
    public:
        explicit ProcessWatcher(QObject* parent = nullptr);
        ~ProcessWatcher() override = default;

        Q_INVOKABLE static bool fileExists(const QString& name);

        Q_INVOKABLE int check(const QString& name);

        Q_INVOKABLE bool start(const QString& name);

    signals:
        void finished(int result);

    private:
        QFutureWatcher<int> watcher;

        static int checkPlatform(const QString& name);

#ifdef Q_OS_LINUX
        static int checkLinux(const QString& name);
#endif

#ifdef Q_OS_DARWIN
        static int checkMacx(const QString& name);
#endif

#ifdef Q_OS_WIN
        static int checkWin(const QString& name);
#endif
};

#endif // WATCHER_HPP
