#include <QtGlobal>
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QDebug>
#include <QQuickView>
#include <QQuickWindow>
#include <QtQuick/qsgrendererinterface.h>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QtWidgets/QSystemTrayIcon>

#include <QCommandLineParser>
#include <QCommandLineOption>

#include "settings.h"
#include "version.h"

#include <ql-channel-serial.hpp>
#include <pinger.h>
#include <watcher.hpp>
#include <qautostarter.h>
#include <qi18n.h>

#include <singleapp.h>
#include <qqmlsystrayicon.h>
#include <qjournald.h>
#include <themedetector.h>
#include <systemprocess.h>
#include <systemprocesslistmodel.h>


static const char* journalStrings[] = {
    QT_TRANSLATE_NOOP("Logger","application started"),
    QT_TRANSLATE_NOOP("Logger","application closed"),
    QT_TRANSLATE_NOOP("Logger","application is about to be closed"),
};

int main(int argc, char *argv[])
{
    static const QString compile_date(__DATE__);

    qmlRegisterType<QlChannelSerial>("QlChannelSerial", 1,0, "QlChannelSerial");
    qmlRegisterType<PingProcess>("PingProcess", 1, 0, "PingProcess");
    //qmlRegisterType<ProcessWatcher>("ProcessWatcher", 1, 0, "ProcessWatcher");
    //qmlRegisterType<AutoStarter>("AutoStarter", 1, 0, "AutoStarter");
    qmlRegisterType<Settings>("Settings", 1, 0, "Settings");
    QQmlSystrayIcon::declareQML();
    SystemProcess::declareQML();
    SystemProcessListModel::declareQML();

    qmlRegisterUncreatableType<SingleAppHandler>("ru.opendev.SingleAppHandler", 1, 0, "SingleAppHandler",
                                                "Cannot create an instance of SingleAppHandler from QML");
    //qmlRegisterUncreatableType<QJournal>("ru.opendev.qjournal", 1, 0, "QJournal", "Cannot create an instance of QJournal from QML");

    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc, argv);
    app.setOrganizationName("Open-dev.ru");
    app.setApplicationName("WatchdogMonitor3");
    app.setApplicationVersion(WDTMON_VERSION);
    app.setWindowIcon(QIcon(":/wdtmon3.png"));
    app.addLibraryPath(app.applicationDirPath());

    SingleAppHandler shandler(&app);
    if(!shandler.isSingle())
    {
        shandler.sendMessage("show");
        return 0;
    }

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption langOption(QStringList()<<"lang","Set UI language to <lang>","lang");
    parser.addOption(langOption);
    QCommandLineOption softwareOpenGLOption(QStringList()<<"softgl","User software rendering instead of OpenGL");
    parser.addOption(softwareOpenGLOption);
    QCommandLineOption legacyOption(QStringList()<<"legacy","Run in wdtmon2 compability mode");
    parser.addOption(legacyOption);

    ThemeDetector::addOptions(parser);

    parser.process(app);

    QSettings settings;

    if(parser.isSet(langOption))
    {
        const QString lang = parser.value(langOption).trimmed();
        settings.setValue(SETTINGS_LANG,
                          lang.compare(QLatin1String("system"), Qt::CaseInsensitive) == 0
                              ? QString() : lang);
    }

    if(parser.isSet(softwareOpenGLOption))
    {
        settings.setValue("softgl",true);
    }
    if(parser.isSet(legacyOption))
    {
        settings.setValue("legacy",true);
    }

    ThemeDetector::init(&parser, "theme/dark");
    QScopedPointer<QTranslator> app_tr(QLANG::installLanguage(&app));
    QScopedPointer<QAutoStarter> astarter_instance(new QAutoStarter("OpenDevWdtmon3", ":/wdtmon3.png", &app));
    QScopedPointer<ProcessWatcher> pwatcher_instance(new ProcessWatcher(&app));

    QQuickStyle::setStyle("Material");
    if(settings.value("softgl",false).toBool())
    {
        QQuickWindow::setSceneGraphBackend(QSGRendererInterface::Software);
    }

    app.setQuitOnLastWindowClosed(false);

    QScopedPointer<QJournal> journal(new QJournal());
    journal->setup("","wdtmon3.log");
    journal->setMaxSize(MEGA_BYTES(settings.value("logging/size",64).toUInt()));
    journal->setBackupCount(settings.value("logging/count",3).toUInt());
    journal->setPaused(!(settings.value("logging/enable",false).toBool()));
    journal->log(qApp->translate("Logger",journalStrings[0]));
    QJournal* const journalPtr = journal.data();
    QObject::connect(qApp, &QApplication::commitDataRequest, journalPtr, [journalPtr](){
        qWarning()<<"App request commitData";
        journalPtr->log(qApp->translate("Logger", journalStrings[2]));
    });

    QQmlApplicationEngine engine;
    qmlRegisterSingletonInstance("ru.opendev.AutoStarter", 2, 0, "AStarter", astarter_instance.get());
    qmlRegisterSingletonInstance("ru.opendev.ProcessWatcher", 2, 0, "PWatcher", pwatcher_instance.get());
    qmlRegisterSingletonInstance("ru.opendev.QJournal", 2, 0, "Journal", journal.get());
    qmlRegisterSingletonInstance("ru.opendev.SingleApp", 2, 0, "AppServer", &shandler);

    engine.addImportPath(app.applicationDirPath());
    engine.rootContext()->setContextProperty("appversion", QString(WDTMON_VERSION));
    engine.rootContext()->setContextProperty("darktheme", ThemeDetector::isDarkThemeEnabled());
    engine.rootContext()->setContextProperty("__DATE__", compile_date);
    engine.rootContext()->setContextProperty("PWD", app.applicationDirPath());
    engine.rootContext()->setContextProperty("lang_key", QString(SETTINGS_LANG));
    engine.rootContext()->setContextProperty("theme_key", QString("theme/dark"));
    engine.rootContext()->setContextProperty("software_rendering", settings.value("softgl",false).toBool());
    engine.rootContext()->setContextProperty("lang_current", settings.value(SETTINGS_LANG).toString());
    QObject::connect(&engine, &QQmlApplicationEngine::quit, &app, &QApplication::quit);

    engine.load(QUrl(QLatin1String("qrc:/main.qml")));
    if(engine.rootObjects().isEmpty())
    {
        qCritical()<<"Cannot load the main QML object";
        return 1;
    }

    int ret = app.exec();
    journal->log(qApp->translate("Logger",journalStrings[1]));

    return ret;
}
