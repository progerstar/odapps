#ifndef QOPENHARDWAREMONITOR_H
#define QOPENHARDWAREMONITOR_H

#include <QObject>

class QOpenHardwareMonitor : public QObject
{
        Q_OBJECT
    public:
        explicit QOpenHardwareMonitor(QObject *parent = 0);
        ~QOpenHardwareMonitor();

        Q_INVOKABLE bool init();

        Q_INVOKABLE QStringList cpus();
        Q_INVOKABLE QStringList gpus();
    signals:

    public slots:

    private:
#ifdef Q_OS_WIN
        QWMIOpenHardwareMonitor* handle;
        bool h_valid;
#endif

        QStringList get(const QList<int>& htypes);
};

#endif // QOPENHARDWAREMONITOR_H
