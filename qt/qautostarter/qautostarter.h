#ifndef AUTOSTARTER_H
#define AUTOSTARTER_H

#include <QObject>

class QAutoStarter : public QObject
{
        Q_OBJECT
    public:
        explicit QAutoStarter(const QString& regEntry, const QString& icon = "", QObject *parent = 0);

        Q_INVOKABLE void checkAutostart();
        Q_INVOKABLE bool autostartSet();
        Q_INVOKABLE void setAutostart(bool on);
    private:
        QString entryName;
        QString iconPath;
};

#endif // AUTOSTARTER_H
