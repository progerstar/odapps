#include "wdtmonlite.h"

#include <QApplication>
#include <singleapp.h>

#include <QCommandLineOption>
#include <QCommandLineParser>

#include <qi18n.h>
#include <themedetector.h>
#include <odwidgetstyle.h>

#include <odhid_global.h>

static SingleAppHandler* shandler = nullptr;

BootMode wdtmon3lite_runner(bool first, bool developerMode)
{
    WdtmonLite* w = new WdtmonLite;
    if(!w->init()) {
        QMessageBox::critical(nullptr,"wdtmon3-mini",
                              QT_TRANSLATE_NOOP("Main","A problem has been detected. The application will be closed."));
        delete w;
        return Boot_Fault;
    }
    QObject::connect(shandler,SIGNAL(incomingMessage(QString)),w,SLOT(message(QString)));
#ifndef OD_NO_DEVELOPER
    w->setDevMode(developerMode);
#else
    (void)developerMode;
#endif
    if(!first || !QSettings().value(SETTINGS_SYS_HIDE,false).toBool())
    {
        w->show();
    }

    (void)qApp->exec();
    BootMode bmode = w->exitMode();
    delete w;
    return bmode;
}


int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication a(argc, argv);
    a.setOrganizationName("Open-dev.ru");
    a.setApplicationName("wdtmon3-mini");
    a.setApplicationVersion(WDTMON_LITE_VERSION);
    a.addLibraryPath(a.applicationDirPath());

    QCommandLineParser parser;
    parser.addVersionOption();
    parser.addHelpOption();
    ThemeDetector::addOptions(parser);

    QCommandLineOption langOption(QStringList()<<"l"<<"lang", "Set UI language to <lang>.", "lang");
    parser.addOption(langOption);
#ifndef OD_NO_DEVELOPER
    QCommandLineOption devOption(QStringList()<<"meijin", "Run in developer mode");
    devOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(devOption);
#endif
    QCommandLineOption dfuOption(QStringList()<<"d"<<"dfu", "Run in DFU mode.");
    parser.addOption(dfuOption);
    QCommandLineOption resetOption(QStringList()<<"r"<<"reset", "Reset all settings.");
    parser.addOption(resetOption);

    parser.process(a);

    shandler = new SingleAppHandler(&a);
    if(!shandler->isSingle())
    {
        shandler->sendMessage("show");
        delete shandler;
        return 0;
    }

    if(parser.isSet(resetOption))
    {
        if(QMessageBox::question(0, QT_TRANSLATE_NOOP("Main","Reset settings"),
                                 QT_TRANSLATE_NOOP("Main","Are you sure you want to reset <b>all</b> settings?<br>"
                                    "This operation cannot be undone!"),
                                 QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes)
        {
            QSettings().clear();
            QMessageBox::information(0, QT_TRANSLATE_NOOP("Main","Reset settings"),
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
    ODWidgetStyle::apply(&a, ThemeDetector::isDarkThemeEnabled());
    QList<QTranslator*> i18n = QLANG::installLanguage(&a);

#ifndef OD_NO_DEVELOPER
    const bool developerMode = parser.isSet(devOption);
#else
    const bool developerMode = false;
#endif

    bool firstStart = true;
    BootMode mode = parser.isSet(dfuOption) ? Boot_DFU : Boot_Restart;
    while(mode != Boot_Normal && mode != Boot_Fault)
    {
        if(mode == Boot_DFU)
        {
            mode = libhid_main(&a, "usb", QStringList() << "WDG DFU");
        }
        else
        {
            mode = wdtmon3lite_runner(firstStart, developerMode);
            firstStart = false;
        }
    }

    const int ret = (mode == Boot_Fault) ? 1 : 0;
    QLANG::uninstallLanguage(&a, &i18n);
    delete shandler;
    libhid_deinit();
    return ret;
}
