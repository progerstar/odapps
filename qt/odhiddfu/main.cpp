#include "odhiddfumain.h"
#include <themedetector.h>
#include <odwidgetstyle.h>

#include <QApplication>
#include <QInputDialog>
#include <QFileInfo>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <limits>

#include <qi18n.h>

#ifdef Q_OS_ANDROID
#include <androidjnihelper.h>
#endif

int main(int argc, char *argv[])
{
#ifndef Q_OS_DARWIN
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication a(argc, argv);
    a.setOrganizationName("Open-dev.ru");
    a.setApplicationName("ODHidDFU");
    a.setApplicationVersion(HIDDFU_VERSION);
    a.addLibraryPath(a.applicationDirPath());
    a.setWindowIcon(QIcon(":/images/odhiddfu.ico"));

    QCommandLineParser parser;
    parser.addVersionOption();
    parser.addHelpOption();
    parser.setApplicationDescription("OpenDev DFU capable devices upgrade utility.");

    ThemeDetector::addOptions(parser);

    QCommandLineOption langOption(QStringList()<<"l"<<"lang", "Set UI language to <lang>.", "lang");
    parser.addOption(langOption);

#ifndef OD_NO_DEVELOPER
    QCommandLineOption devOption(QStringList()<<"meijin", "Run in developer mode.");
    devOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(devOption);

    QCommandLineOption stmOption(QStringList()<<"stm", "Put to STM DFU mode.");
    stmOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(stmOption);
#endif

    QCommandLineOption typeOption(QStringList()<<"t"<<"type", "Bootloader <type>.", "type");
    parser.addOption(typeOption);

    QCommandLineOption prodOption(QStringList()<<"p"<<"product", "Product <name>.", "product");
    parser.addOption(prodOption);

    QCommandLineOption dfuOption(QStringList()<<"f"<<"file", "Firmware update <file>.", "file");
    parser.addOption(dfuOption);

    QCommandLineOption acceptOption(QStringList()<<"a"<<"accept", "Suppress confirmation dialog.");
    acceptOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(acceptOption);

    QCommandLineOption transportOption(QStringList()<<"d"<<"device", "Transport <device>.", "device");
    parser.addOption(transportOption);

    QCommandLineOption baudOption(QStringList()<<"b"<<"baud", "Serial port <baudrate> (default: 115200).", "baud", "115200");
    parser.addOption(baudOption);

    QCommandLineOption addrOption(QStringList()<<"r"<<"address", "Device address (default: scan all).", "address", "");
    parser.addOption(addrOption);

    parser.process(a);

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

    HidDFUParams params;

#ifndef OD_NO_DEVELOPER
    params.meijin = parser.isSet(devOption);
    params.stmsw = parser.isSet(stmOption);
#endif
    params.auto_mode = parser.isSet(acceptOption);

    if(parser.isSet(prodOption)) {
        params.product = parser.value(prodOption);
    }
    else if(parser.isSet(typeOption)) {
        params.type = parser.value(typeOption);
    }

    if(parser.isSet(dfuOption)) {
        params.fw_file = parser.value(dfuOption);
        const QFileInfo firmwareInfo(params.fw_file);
        if(!firmwareInfo.exists() || !firmwareInfo.isFile() || !firmwareInfo.isReadable()) {
            qCritical()<<"Firmware file is not readable: "<<params.fw_file;
            QLANG::uninstallLanguage(&a, &i18n);
            return 2;
        }
    }
    if(parser.isSet(transportOption)) {
        params.medium = parser.value(transportOption);
    }
    if(parser.isSet(baudOption)) {
        bool ok = false;
        params.baudrate = parser.value(baudOption).toUInt(&ok);
        if(!ok || !params.baudrate || params.baudrate > quint32(std::numeric_limits<int>::max())) {
            qCritical()<<"Invalid baudrate: "<<parser.value(baudOption);
            QLANG::uninstallLanguage(&a, &i18n);
            return 2;
        }
    }
    if(parser.isSet(addrOption)) {
        bool ok;
        params.address = parser.value(addrOption).toInt(&ok);
        if(!ok || params.address < -1 || params.address > 254) {
            qCritical()<<"Invalid device address: "<<parser.value(addrOption);
            QLANG::uninstallLanguage(&a, &i18n);
            return 2;
        }
    }

#ifdef Q_OS_ANDROID
    AndroidJNIHelper::requestPermission("android.permission.READ_INTERNAL_STORAGE");
    AndroidJNIHelper::requestPermission("android.permission.READ_EXTERNAL_STORAGE");
#endif

    //Wrong: can happen BEFORE libusb_close, that is in another thread!
    //QObject::connect(&a, &QApplication::aboutToQuit, [=](){HIDProxy::deinit();});
    ODHidDFUMain* main = new ODHidDFUMain(params);

    main->show();
    const int ret = a.exec();
    delete main;
    QLANG::uninstallLanguage(&a, &i18n);
    // Now and no sooner
    HIDProxy::deinit();
    return ret;
}
