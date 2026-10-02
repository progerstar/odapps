#include "odrfidconfig.h"
#include <QApplication>

#include <QMessageBox>
#include <QInputDialog>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QStringBuilder>

#include <themedetector.h>
#include <qmaterialfont.h>
#include <odrfidstyle.h>
#include <qi18n.h>

int main(int argc, char *argv[])
{
#ifndef Q_OS_DARWIN
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication a(argc, argv);
    a.setOrganizationName("Open-dev.ru");
    a.setApplicationName("ODRFIDCfg");
    a.setApplicationVersion(ODRFIDCFG_VERSION);
    a.addLibraryPath(a.applicationDirPath());
    a.setWindowIcon(QIcon("://odrfidcfg.ico"));

    QCommandLineParser parser;
    parser.addVersionOption();
    parser.addHelpOption();

    ThemeDetector::addOptions(parser);

    QCommandLineOption langOption(QStringList()<<"l"<<"lang", "Set UI language to <lang>.", "lang");
    parser.addOption(langOption);

#ifndef OD_NO_DEVELOPER
    QCommandLineOption dfuOption(QStringList()<<"d"<<"dfu", "Put device into DFU mode.");
    dfuOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dfuOption);
#endif

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
    a.setStyleSheet(odRfidStyleSheet(ThemeDetector::isDarkThemeEnabled()));

    QList<QTranslator*> i18n = QLANG::installLanguage(&a);

    ODRFIDConfig* w = new ODRFIDConfig;
    QString dev;
    QStringList devs = w->availableDevices();
    int ret = 1;

    if(devs.isEmpty()) {
        QMessageBox::critical(w, QCoreApplication::translate("Application", "ODRFID Config"),
                              QCoreApplication::translate("Application", "No connected devices"));
        goto bail;
    }

    dev = devs.at(0);
    if(devs.size() > 1) {
        bool ok;
        dev = QInputDialog::getItem(w, QCoreApplication::translate("Application", "ODRFID Config"),
                                    QCoreApplication::translate("Application", "Select device") % QLatin1String(":"), devs, 0, false, &ok);
        if(!ok) {
            goto bail;
        }
    }

    if(!w->connect(dev)) {
        QMessageBox::critical(w, QCoreApplication::translate("Application", "ODRFID Config"),
                              QCoreApplication::translate("Application", "Connection failed"));
        goto bail;
    }

#ifndef OD_NO_DEVELOPER
    if(parser.isSet(dfuOption)) {
        QMetaObject::invokeMethod(w, &ODRFIDConfig::on_actionUpgrade_triggered, Qt::QueuedConnection);
    }
#endif
    w->show();
    ret = a.exec();

bail:
    delete w;
    QLANG::uninstallLanguage(&a, &i18n);
    return ret;
}
