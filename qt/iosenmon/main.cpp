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

#include <qi18n.h>
#include <qautostarter.h>
#include <qmlsettings.h>

#include <singleapp.h>
#include <qqmlsystrayicon.h>

#include "iotrunner.h"

#ifdef Q_OS_DARWIN
#define AutoStarter QAutoStarter("ru.open-dev.iosenmon",":/iosenmon.png",0)
#endif

#ifdef Q_OS_WIN
#define AutoStarter QAutoStarter("OpenDevIOSenMon",":/iosenmon.png",0)
#endif

#ifdef Q_OS_LINUX
#define AutoStarter QAutoStarter("OpenDev-IOSenMon",":/iosenmon.png",0)
#endif

static const char* journalStrings[] = {
    QT_TRANSLATE_NOOP("Logger","application started"),
    QT_TRANSLATE_NOOP("Logger","application closed"),
    QT_TRANSLATE_NOOP("Logger","application is about to be closed"),
};

static QJournal* journal = 0;

int main(int argc, char *argv[])
{
    static const QString compile_date(__DATE__);

    QMLSettings::declareQML();
    QQmlSystrayIcon::declareQML();
    SensorListModel::declareQML();
    LogListModel::declareQML();

    qmlRegisterUncreatableType<QAutoStarter>("ru.opendev.AutoStarter", 1, 0, "AutoStarter",
                                             "Cannot create an instanve of AutoStarter from QML");
    qmlRegisterUncreatableType<SingleAppHandler>("ru.opendev.SingleAppHandler", 1, 0, "SingleAppHandler",
                                                "Cannot create an instance of SingleAppHandler from QML");
    qmlRegisterUncreatableType<QJournal>("ru.opendev.qjournal", 1, 0, "QJournal",
                                         "Cannot create an instance of QJournal from QML");
    qmlRegisterUncreatableType<IOTRunner>("IOTRunner", 1, 0, "IOTRunner",
                                         "Cannot create an instance of IOTRunner from QML");
    qmlRegisterUncreatableType<HidSensorInterface>("HidSensorInterface", 1, 0, "HidSensorInterface",
                                         "Cannot create an instance of HidSensorInterface from QML");

    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication app(argc, argv);
    app.setOrganizationName("Open-dev.ru");
    app.setApplicationName("IOSenMon");
    app.setApplicationVersion(IOSENMON_VERSION);
    app.setWindowIcon(QIcon(":/iosenmon.ico"));
    app.addLibraryPath(app.applicationDirPath());

    SingleAppHandler shandler(&app);
    if(!shandler.isSingle())
    {
        shandler.sendMessage("show");
        return 0;
    }

    QAutoStarter* autoStarter = new AutoStarter;

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption langOption(QStringList()<<"l"<<"lang", "Set UI language to <lang>.", "lang");
    parser.addOption(langOption);
    QCommandLineOption softwareOpenGLOption(QStringList()<<"softgl", "Use software rendering instead of OpenGL.");
    parser.addOption(softwareOpenGLOption);
    QCommandLineOption databaseOption(QStringList()<<"db", "Database location", "database");
    parser.addOption(databaseOption);
#ifndef OD_NO_DEVELOPER
    QCommandLineOption meijinOption(QStringList()<<"meijin", "Run in developer mode.");
    meijinOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(meijinOption);
#endif

    parser.process(app);
    QSettings settings;

    if(parser.isSet(langOption))
    {
        settings.setValue(SETTINGS_LANG,parser.value(langOption));
    }
    else if(!settings.contains(SETTINGS_LANG))
    {
        QString defLang = QLocale::system().name().split("_").first();
        settings.setValue(SETTINGS_LANG,((defLang=="en")||(defLang=="ru"))?defLang:"en");
    }

    if(parser.isSet(softwareOpenGLOption))
    {
        settings.setValue(SETTINGS_SOFTGL,true);
    }

    QList<QTranslator*> i18n = QLANG::installLanguage(&app);

    QQuickStyle::setStyle("Material");
    if(settings.value(SETTINGS_SOFTGL,false).toBool())
    {
        QQuickWindow::setSceneGraphBackend(QSGRendererInterface::Software);
    }

    app.setQuitOnLastWindowClosed(false);

    journal = new QJournal();
    journal->setup("","iosenmon.log");
    journal->setMaxSize(MEGA_BYTES(settings.value(SETTINGS_LOG_SIZE,64).toUInt()));
    journal->setBackupCount(settings.value(SETTINGS_LOG_COUNT,3).toUInt());
    journal->setPaused(!(settings.value(SETTINGS_LOG_ENABLE,false).toBool()));
    journal->log(qApp->translate("Logger",journalStrings[0]));
    QObject::connect(qApp,&QApplication::commitDataRequest, [=](){
        qWarning()<<"App request commitData";
        journal->log(qApp->translate("Logger",journalStrings[2]));
    });

    IOTRunner* runner = new IOTRunner;
    runner->setJournal(journal);
    runner->cleanDatabase();

    int ret = 0;
    {
        /* the engine must go away before the runner it references */
        QQmlApplicationEngine engine;
        engine.addImportPath(app.applicationDirPath());
        QQmlContext* root_ctx = engine.rootContext();

        root_ctx->setContextProperty("appversion",IOSENMON_VERSION);
        root_ctx->setContextProperty("__DATE__",compile_date);
        root_ctx->setContextProperty("PWD",app.applicationDirPath());
        root_ctx->setContextProperty("runner",runner);
        root_ctx->setContextProperty("appServer",&shandler);
        root_ctx->setContextProperty("journal",journal);
        root_ctx->setContextProperty("autoStarter",autoStarter);
        root_ctx->setContextProperty("lang_key",SETTINGS_LANG);
        root_ctx->setContextProperty("software_rendering",settings.value(SETTINGS_SOFTGL,false).toBool());
        root_ctx->setContextProperty("lang_current",settings.value(SETTINGS_LANG,"en").toString());

        root_ctx->setContextProperty(STRINGIFY(SETTINGS_DB_LOC),SETTINGS_DB_LOC);
        root_ctx->setContextProperty(STRINGIFY(SETTINGS_DB_AGE),SETTINGS_DB_AGE);
        root_ctx->setContextProperty(STRINGIFY(SETTINGS_DB_SAVEPATH),SETTINGS_DB_SAVEPATH);
        root_ctx->setContextProperty(STRINGIFY(SETTINGS_UI_VISIBLE),SETTINGS_UI_VISIBLE);
        root_ctx->setContextProperty(STRINGIFY(SETTINGS_SOFTGL),SETTINGS_SOFTGL);
        root_ctx->setContextProperty(STRINGIFY(SETTINGS_UNITS_TEMP),SETTINGS_UNITS_TEMP);
        root_ctx->setContextProperty(STRINGIFY(SETTINGS_UNITS_LENGTH),SETTINGS_UNITS_LENGTH);
        root_ctx->setContextProperty(STRINGIFY(SETTINGS_LOG_SIZE),SETTINGS_LOG_SIZE);
        root_ctx->setContextProperty(STRINGIFY(SETTINGS_LOG_COUNT),SETTINGS_LOG_COUNT);
        root_ctx->setContextProperty(STRINGIFY(SETTINGS_LOG_ENABLE),SETTINGS_LOG_ENABLE);
        root_ctx->setContextProperty(STRINGIFY(SETTINGS_RESCAN_TIMEOUT),SETTINGS_RESCAN_TIMEOUT);
        root_ctx->setContextProperty(STRINGIFY(SETTINGS_HTTP_ENABLED),SETTINGS_HTTP_ENABLED);
        root_ctx->setContextProperty(STRINGIFY(SETTINGS_HTTP_PORT),SETTINGS_HTTP_PORT);

        QObject::connect(&engine,SIGNAL(quit()),qApp,SLOT(quit()));

        engine.load(QUrl(QLatin1String("qrc:/main.qml")));

        ret = engine.rootObjects().isEmpty() ? -1 : app.exec();
    }
    journal->log(qApp->translate("Logger",journalStrings[1]));

    delete runner;
    QLANG::uninstallLanguage(&app, &i18n);
    delete journal;
    delete autoStarter;

    QSqlDatabase::removeDatabase(DB_CONN_NAME);
    return ret;
}
