#ifndef SYSTEMPROCESS_H_
#define SYSTEMPROCESS_H_

#include <QObject>

#ifdef WITH_QML
#include <QtQml>
#endif


class SystemProcess : public QObject
{
        Q_OBJECT
    public:
        enum Status
        {
            Running      = 0x01, /*R on linux*/
            Waiting      = 0x02, /*S on linux*/
            IO           = 0x04, /*D on linux*/
            Stopped      = 0x08, /*T on linux*/
            Debugged     = 0x10, /*t on linux*/
            Dead         = 0x20, /*X on linux*/
            Zombie       = 0x40, /*Z on linux*/
            Foreground   = 0x80, /*+ on linux*/
            HighPriority = 0x100, /*< on linux*/
            LowPriority  = 0x200, /*N on linux*/
        };
        Q_ENUM(Status)

#ifdef WITH_QML
        static inline void declareQML()
        {
            qmlRegisterType<SystemProcess>("ru.opendev.systemprocess",1,0, "SystemProcess");
        }
#endif

        explicit SystemProcess(QObject* parent = 0) : QObject(parent){}
        SystemProcess(const SystemProcess& other);
        SystemProcess& operator =(const SystemProcess& other);

        static QList<SystemProcess> systemProcesses();

        QString name;
        QString path;
        double pmem;
        int status;
};

#endif
