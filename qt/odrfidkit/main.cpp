#include "rfidkitwindow.h"

#include <qi18n.h>

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QMessageBox>

#include <themedetector.h>
#include <qmaterialfont.h>
#include <odrfidstyle.h>
#include <odhid_global.h>

static bool first_start = true;
static QCommandLineParser parser;

BootMode rfidkit_runner(int argc, char** argv, QString& dfuTransport, QStringList& dfuProducts)
{
    QApplication app(argc,argv);
    app.setOrganizationName("Open-dev.ru");
    app.setApplicationName("ODRFIDKit");
    app.setApplicationVersion(ODRFIDKIT_VERSION);
    app.addLibraryPath(app.applicationDirPath());
    app.setWindowIcon(QIcon(":/odrfidkit.ico"));
    if(!QMaterialIcon::instance()->firstRun()) {
        QMaterialIcon::instance()->reinit();
    }

    if(first_start)
    {
        first_start = false;

        parser.addVersionOption();
        parser.addHelpOption();
        parser.setApplicationDescription("ODRFID demonstration utility");

        ThemeDetector::addOptions(parser);

        QCommandLineOption langOption(QStringList()<<"l"<<"lang", "Set UI language to <lang>.", "lang");
        parser.addOption(langOption);
#ifndef OD_NO_DEVELOPER
        QCommandLineOption devOption(QStringList()<<"meijin", "Run in developer mode");
        devOption.setFlags(QCommandLineOption::HiddenFromHelp);
        parser.addOption(devOption);
#endif

        QCommandLineOption resetOption(QStringList()<<"r"<<"reset", "Reset all settings.");
        parser.addOption(resetOption);

        QCommandLineOption dfuOption(QStringList()<<"d"<<"dfu", "Run in DFU mode.");
        parser.addOption(dfuOption);

        parser.process(app);

        if(parser.isSet(resetOption))
        {
            if(QMessageBox::question(nullptr, QT_TRANSLATE_NOOP("Main","Reset settings"),
                                     QT_TRANSLATE_NOOP("Main","Are you sure you want to reset <b>all</b> settings?<br>"
                                                       "This operation cannot be undone!"),
                                     QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes)
            {
                QSettings().clear();
                QMessageBox::information(nullptr, QT_TRANSLATE_NOOP("Main","Reset settings"),
                                         QT_TRANSLATE_NOOP("Main","Settings have been cleared"));
            }
        }

        if(parser.isSet(langOption))
        {
            const QString lang = parser.value(langOption).trimmed();
            QSettings().setValue(SETTINGS_LANG,
                                 lang.compare(QLatin1String("system"), Qt::CaseInsensitive) == 0
                                     ? QString() : lang);
        }

        ThemeDetector::init(&parser);
        QMaterialIcon::instance()->setDefaultColors(ThemeDetector::isDarkThemeEnabled());
    }
    else
    {
        ThemeDetector::reinit();
    }
    app.setStyleSheet(odRfidStyleSheet(ThemeDetector::isDarkThemeEnabled()));

    QList<QTranslator*> i18n = QLANG::installLanguage(&app);
    RFIDKitWindow* w = new RFIDKitWindow(nullptr);
#ifndef OD_NO_DEVELOPER
    w->setDevMode(parser.isSet("meijin"));
#else
    w->setDevMode(false);
#endif
    w->show();

    (void)app.exec();
    BootMode bmode = w->exitMode(dfuTransport, dfuProducts);
    delete w;
    QLANG::uninstallLanguage(&app, &i18n);
    return bmode;
}

BootMode dfu_runner(int args, char** argv, QString& dfuTransport, QStringList& dfuProducts)
{
    QApplication app(args, argv);
    app.setOrganizationName("Open-dev.ru");
    app.setApplicationName("ODRFIDKit");
    app.setApplicationVersion(ODRFIDKIT_VERSION);
    app.addLibraryPath(app.applicationDirPath());
    app.setWindowIcon(QIcon(":/odrfidkit.ico"));
    QList<QTranslator*> i18n = QLANG::installLanguage(&app);
    BootMode ret = libhid_main(&app, dfuTransport, dfuProducts);
    QLANG::uninstallLanguage(&app, &i18n);
    return ret;
}

int main(int argc, char *argv[])
{
#ifndef Q_OS_DARWIN
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    BootMode mode = Boot_Restart;
    QString dfuTransport;
    QStringList dfuProducts;
    while(1){
        switch(mode)
        {
            case Boot_Restart:
                mode = rfidkit_runner(argc, argv, dfuTransport, dfuProducts);
                break;
            case Boot_DFU:
                mode = dfu_runner(argc, argv, dfuTransport, dfuProducts);
                break;
            case Boot_Normal:
                return 0;
            case Boot_Fault:
            default:
                return 1;
        }
    }
    return 0;
}
