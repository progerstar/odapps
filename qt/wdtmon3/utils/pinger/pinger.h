#ifndef PROCESS_H
#define PROCESS_H

#include <QProcess>
#include <QVariant>
#include <QDebug>
#include <QSettings>

class PingProcess : public QObject {
        Q_OBJECT
        Q_PROPERTY(int timeout READ timeout WRITE setTimeout)
    public:
        explicit PingProcess(QObject *parent = nullptr) : QObject(parent), runner(this),
            m_exitCode(255), outputData("Not Running"), timeout_ms(1000)
        {
            connect(&runner,SIGNAL(finished(int,QProcess::ExitStatus)),this,SLOT(runner_finished(int,QProcess::ExitStatus)));
            connect(&runner, &QProcess::errorOccurred, this, &PingProcess::runner_error);
        }

        ~PingProcess()
        {
            while(runner.state() != QProcess::NotRunning)
            {
                runner.kill();
                runner.waitForFinished(2000);
            }
        }

        Q_INVOKABLE int timeout() const
        {
            return timeout_ms;
        }

        Q_INVOKABLE void setTimeout(int ms)
        {
            timeout_ms = qMax(1, ms);
        }

        Q_INVOKABLE bool start(const QString &address)
        {
            if(runner.state() != QProcess::NotRunning)
            {
                return false;
            }

            m_exitCode = 255;
            outputData = QStringLiteral("Running");

            runner.setProgram("ping");
#ifdef Q_OS_WIN
            runner.setArguments(QStringList()<<"-n"<<"1"<<"-w"<<QString::number(qMax<int>(200,timeout_ms))<<(address.isEmpty() ? "127.0.0.1" : address));
#elif defined(Q_OS_DARWIN)
            runner.setArguments(QStringList()<<"-n"<<"-o"<<"-W"<<QString::number(qMax<int>(200,timeout_ms))<<(address.isEmpty() ? "127.0.0.1" : address));
#else
            runner.setArguments(QStringList()<<"-n"<<"-c"<<"1"<<"-w"<<QString::number(qMax<int>(1,(timeout_ms+499)/1000))<<(address.isEmpty() ? "127.0.0.1" : address));
#endif
            //qWarning()<<"exec: "<<runner.program()<<" "<<runner.arguments().join(" ");
            runner.start();
            return true;
        }

        Q_INVOKABLE QString output()
        {
            return outputData;
        }

        Q_INVOKABLE int exitCode()
        {
            return m_exitCode;
        }

    signals:
        void finished();
    private slots:
        inline void runner_finished(int code, QProcess::ExitStatus exitStatus)
        {
            if(exitStatus != QProcess::NormalExit)
            {
                m_exitCode = 255;
                outputData = "Crashed";
                emit finished();
            }
            else
            {
                outputData = runner.readAllStandardOutput()+"\n"+runner.readAllStandardError();
                m_exitCode = code;
                emit finished();
            }
        }

        void runner_error(QProcess::ProcessError error)
        {
            if(error == QProcess::FailedToStart)
            {
                m_exitCode = 255;
                outputData = runner.errorString();
                emit finished();
            }
        }

    private:
        QProcess runner;
        int m_exitCode;
        QString outputData;
        int timeout_ms;
};
#endif // PROCESS_H
