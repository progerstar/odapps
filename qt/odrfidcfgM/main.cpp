#include "odrfidconfig.h"
#include <uartcommunicator.h>

#include <QApplication>

#include <QMessageBox>
#include <QInputDialog>
#include <QCommandLineParser>
#include <QCommandLineOption>

#include <qi18n.h>
#include <themedetector.h>
#include <qmaterialfont.h>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setOrganizationName("Open-dev.ru");
    a.setApplicationName("ODRFIDCfgM");
    a.setApplicationVersion(ODRFIDCFG_VERSION);
    a.addLibraryPath(a.applicationDirPath());
    a.setWindowIcon(QIcon(":/odrfidconfigM.ico"));

    QCommandLineParser parser;
    parser.addVersionOption();
    parser.addHelpOption();
    UARTCommunicator::app_addParserOptions(parser);
#ifndef OD_NO_DEVELOPER
    QCommandLineOption dfuOption(QStringList()<<"d"<<"dfu", "Put device into DFU mode.");
    dfuOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dfuOption);
    QCommandLineOption offOption(QStringList()<<"offset", "Holding register offset [0]", "offset", "0");
    parser.addOption(offOption);
#endif

    ThemeDetector::addOptions(parser);

    QCommandLineOption langOption(QStringList()<<"l"<<"lang", "Set UI language to <lang>.", "lang");
    parser.addOption(langOption);

    parser.process(a);

    if(parser.isSet(langOption))
    {
        const QString lang = parser.value(langOption).trimmed();
        QSettings().setValue(SETTINGS_LANG,
                             lang.compare(QLatin1String("system"), Qt::CaseInsensitive) == 0
                                 ? QString() : lang);
    }

    ThemeDetector::init(&parser);
    QMaterialIcon::instance()->setDefaultColors(ThemeDetector::isDarkThemeEnabled());

    QList<QTranslator*> i18n = QLANG::installLanguage(&a);

    ODRFIDConfig* w = new ODRFIDConfig();
    QStringList devs = w->availableDevices();
    if(devs.isEmpty())
    {
        QMessageBox::critical(w, a.tr("RFID Config"),a.tr("No serial ports detected"));
        delete w;
        QLANG::uninstallLanguage(&a, &i18n);
        return 1;
    }
    QString dev = devs.at(0);
    if(devs.size()>1)
    {
        bool ok;
        dev = QInputDialog::getItem(w, a.tr("RFID Config"),a.tr("Select a serial device:"),devs,0,false,&ok);
        if(!ok)
        {
            delete w;
            QLANG::uninstallLanguage(&a, &i18n);
            return 1;
        }
    }

#ifndef OD_NO_DEVELOPER
    if(parser.isSet("offset"))
    {
        int _off = parser.value("offset").toInt();
        if(_off < 0) {
            qFatal("Invalid register offset");
            return 1;
        }
        w->setHoldingOffset(_off);
    }
#endif

    w->show();

    QString errorString;
    if(!UARTCommunicator::app_processParserOptions(parser, errorString))
    {
        QMessageBox::warning(w, a.tr("RFID Config"), errorString);
        delete w;
        QLANG::uninstallLanguage(&a, &i18n);
        return 1;
    }


    if(!w->connect(dev, UARTCommunicator::app_parserOptionsSet(parser)))
    {
        QMessageBox::critical(w, a.tr("RFID Config"),a.tr("Connection failed"));
        delete w;
        QLANG::uninstallLanguage(&a, &i18n);
        return 1;
    }

#ifndef OD_NO_DEVELOPER
    if(parser.isSet(dfuOption))
    {
        QMetaObject::invokeMethod(w, "on_fwTool_clicked", Qt::QueuedConnection);
    }
#endif

    int ret = a.exec();
    delete w;
    QLANG::uninstallLanguage(&a, &i18n);
    return ret;
}
