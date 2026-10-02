#include "qautostarter.h"
#include <QSettings>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>

QAutoStarter::QAutoStarter(const QString &regEntry, const QString &icon, QObject *parent) : QObject(parent),
    entryName(regEntry), iconPath(icon)
{

}

#ifdef Q_OS_WIN
void QAutoStarter::checkAutostart()
{
    QSettings winreg("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",QSettings::NativeFormat);
    qWarning()<<"   ->   "<<entryName<<" entry: "<<winreg.value(entryName,"").toString();
    if(!winreg.value(entryName,"").toString().isEmpty() &&
       (winreg.value(entryName,"").toString() !=
        ("\""+QDir::toNativeSeparators(qApp->applicationFilePath())+"\"")))
    {
        qWarning()<<"Application moved -> fixed autostart entry";
        winreg.setValue(entryName,"\""+QDir::toNativeSeparators(qApp->applicationFilePath())+"\"");
    }
}

bool QAutoStarter::autostartSet()
{
    QSettings winreg("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",QSettings::NativeFormat);
    qWarning()<<"   ->   App autostart entry: "<<winreg.value(entryName,"").toString();
    return (winreg.value(entryName,"").toString() ==
            ("\""+QDir::toNativeSeparators(qApp->applicationFilePath())+"\""));
}

void QAutoStarter::setAutostart(bool on)
{
    QSettings winreg("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",QSettings::NativeFormat);
    if(on)
    {
        winreg.setValue(entryName,"\""+QDir::toNativeSeparators(qApp->applicationFilePath())+"\"");
        qWarning()<<"Autostart set ";
    }
    else if(!winreg.value(entryName,"").toString().isEmpty() && !on)
    {
        winreg.remove(entryName);
        qWarning()<<"Autostart removed";
    }
}
#elif defined (Q_OS_LINUX)
void QAutoStarter::checkAutostart()
{
    QFile desktopEntry(QDir::homePath()+"/.config/autostart/"+entryName+".desktop");
    if(!desktopEntry.exists() || !desktopEntry.open(QFile::ReadOnly))
    {
        qWarning()<<"Desktop entry "<<desktopEntry.fileName()<<" does not exist ("<<desktopEntry.exists()
                  <<") or cannot open: "<<desktopEntry.errorString();
        return;
    }

    QRegExp exec_rx("Exec=([^\\n]*)\\n");
    exec_rx.setMinimal(true);
    QString content = QString::fromUtf8(desktopEntry.readAll());

    QString app_file = (!qgetenv("APPIMAGE").isEmpty()) ? qgetenv("APPIMAGE") : qApp->applicationFilePath();

    if(content.contains(exec_rx)&&(exec_rx.cap(1)!=app_file))
    {
        qWarning()<<"Application moved -> fixed autostart entry";
        content.replace(exec_rx,QString("Exec=%1\n").arg(app_file));
        desktopEntry.close();
        if(desktopEntry.open(QFile::WriteOnly))
        {
            desktopEntry.write(content.toUtf8());
        }
        else
        {
            qWarning()<<"Cannot write to desktop file: "<<desktopEntry.errorString();
        }
    }
}

bool QAutoStarter::autostartSet()
{
    return QFile(QDir::homePath()+"/.config/autostart/"+entryName+".desktop").exists();
}

void QAutoStarter::setAutostart(bool on)
{
    QString app_file = (!qgetenv("APPIMAGE").isEmpty()) ? qgetenv("APPIMAGE") : qApp->applicationFilePath();

    QFile desktopEntry(QDir::homePath()+"/.config/autostart/"+entryName+".desktop");
    if(!on) desktopEntry.remove();
    else if(QDir().mkpath(QDir::homePath()+"/.config/autostart")&&desktopEntry.open(QFile::WriteOnly))
    {
        QFile iconFile(iconPath);
        QString iconFilePath = QDir::homePath()+"/.config/autostart/"+QFileInfo(iconFile).fileName();

        desktopEntry.write(QString("[Desktop Entry]\n"
                           "Type=Application\n"
                           "Name=%1\n"
                           "Exec=").arg(entryName).toUtf8());
        desktopEntry.write(app_file.toUtf8());

        if(iconFile.exists()) {
            iconFile.copy(iconFilePath);
            desktopEntry.write((QString("\nIcon=") + iconFilePath + "\n").toUtf8());
        } else {
            desktopEntry.write("\n");
        }
        desktopEntry.write("Categories=System;Utility;\n");
        desktopEntry.close();
    }
}
#elif defined (Q_OS_DARWIN)
void QAutoStarter::checkAutostart()
{
    return;
}

bool QAutoStarter::autostartSet()
{
    return QFile(QDir::homePath()+"/Library/LaunchAgents/"+entryName+".plist").exists();
}

void QAutoStarter::setAutostart(bool on)
{
    if(!on)
    {
        QFile(QDir::homePath()+"/Library/LaunchAgents/"+entryName+".plist").remove();
        return;
    }

    if(!QDir(QDir::homePath()+"/Library/LaunchAgents/").exists())
    {
        QDir().mkpath(QDir::homePath()+"/Library/LaunchAgents/");
    }

    QFile lfile(QDir::homePath()+"/Library/LaunchAgents/"+entryName+".plist");
    if(lfile.open(QFile::WriteOnly))
    {
        lfile.write(QString("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                            "<plist version=\"1.0\">\n"
                            "<dict>\n\t"
                            "<key>ExitTimeOut</key>\n\t"
                            "<integer>5</integer>\n\t"
                            "<key>RunAtLoad</key>\n\t"
                            "<true/>\n\t"
                            "<key>KeepAlive</key>\n\t"
                            "<dict>\n\t\t"
                            "<key>Crashed</key>\n\t\t"
                            "<true/>\n\t"
                            "</dict>\n\t"
                            "<key>Label</key>\n\t"
                            "<string>%1</string>\n\t"
                            "<key>ProgramArguments</key>\n\t"
                            "<array>\n\t\t"
                            "<string>%2</string>\n\t"
                            "</array>\n"
                            "</dict>\n"
                            "</plist>").arg(entryName).arg(qApp->applicationFilePath()).toUtf8());
        lfile.close();
    }
}
#endif
