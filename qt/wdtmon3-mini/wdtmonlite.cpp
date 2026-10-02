#include "wdtmonlite.h"
#include <ui_wdtmonlite.h>

#include <qi18n.h>
#include <uptime.h>

#include <qautostarter.h>

#include "logsetupdialog.h"
#include "httpsetupdialog.h"

#ifdef Q_OS_DARWIN
#define AutoStarter (QAutoStarter("ru.open-dev.wdtmon3-lite",":/images/wdtmon3_lite.png",this))
#endif

#ifdef Q_OS_WIN
#define AutoStarter (QAutoStarter("OpenDevWdtmon3Mini",":/images/wdtmon3_lite.png",this))
#endif

#ifdef Q_OS_LINUX
#define AutoStarter (QAutoStarter("OpenDev-Wdtmon3-Lite",":/images/wdtmon3_lite.png",this))
#endif

#ifdef Q_OS_WIN
/*
 * on Windows - it is iInterface of the HID interface
 */
#include <wchar.h>
#include <iostream>
#define USB_WDG_PRODUCT_HID "WDG HID Interface"
#else
/*
 * on other platforms - it is iProduct of the device
 */
#define USB_WDG_PRODUCT_HID "USB Watchdog"
#endif

#include <QDebug>
#include <QSet>
#include <QSettings>
#include <QMenu>
#include <QAction>
#include <QDir>
#include <QStandardPaths>
#include <QFile>
#include <QMessageBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QTimerEvent>
#include <QInputDialog>
#include <QDesktopServices>
#include <QUrlQuery>
#include <QUuid>
#include <QVector>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

typedef enum {
    HID_RELOAD = 1,
    HID_REBOOT,
    HID_HREBOOT,
    HID_SHUT,
    HID_TEST,
    HID_MREBOOT,
    HID_MHREBOOT,
    HID_CH1_HI,
    HID_CH1_LOW,
    HID_CH2_HI,
    HID_CH2_LOW,
    HID_CHT_HI,
    HID_CHT_LOW,

    HID_LED_G_OFF,
    HID_LED_G_ON,

    HID_TIM_OFF,
    HID_TIM_ON,

    HID_BOOT_SDFU,
    HID_BOOT_UDFU,
} WDT_HID_COMMANDS;

typedef enum {
    HID_ID_CMD  = 1,
    HID_ID_VER  = 2,
    HID_ID_UID  = 3,
    HID_ID_DATA = 4,
    HID_ID_CFG  = 5
} HID_REPORT_IDS;

static const char* led_images[] = {
    ":/images/led_off.png",
    ":/images/led_green.png",
    ":/images/led_orange.png",
    ":/images/led_tinted.png",
};

#ifdef Q_OS_LINUX
QString shellQuote(const QString& value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QStringLiteral("'") + escaped + QLatin1Char('\'');
}
#endif

hid_device* openHid(const QString& sn)
{
    if(sn.isEmpty()) return nullptr;

    QVector<wchar_t> wsn(sn.size() + 1);
    wsn[sn.toWCharArray(wsn.data())] = L'\0';
    hid_device* handle = hid_open(OD_VID, OD_PID_WDG, wsn.constData());
    if(!handle) {
        qWarning()<<"Cannot open hid "<<sn;
    }
    return handle;
}

QString wdgLabelStyle(const QString& img)
{
    return QString("QLabel{"
                   "    border-image: url(\"%1\") stretch;"
                   "    color: #20232A;"
                   "    padding: 8px;"
               #ifndef Q_OS_LINUX
                   "    margin: 2px;"
               #endif
                   "}").arg(img);
}

/******************************************************************************************/

WdtmonLite::WdtmonLite(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::wdtmon3mini), mode(Boot_Normal),
    settings(this), journal(this),
    updTimer(new QTimer(this)),
    systray(nullptr), server(new QMUHttpServer(this)), srvEnableAction(nullptr),
    httpToken(QUuid::createUuid().toString(QUuid::WithoutBraces)),
    rescanTimer(this),
    wdg_mod("???"), wdg_ver("..."),
    wdg_data_time(QDateTime::currentDateTime()),
    wdg_ch1(0xFF),wdg_ch2(0xFF),wdg_ch3(ODC_WDGPRO2_CHANNELSETTINGS_TEMP_OFF),
    wdg_ch3_val(-1)
{
    ui->setupUi(this);
    setWindowTitle(tr("wdtmon3-mini")+QStringLiteral(" ")+WDTMON_LITE_VERSION);

    QFont f = ui->wdginfoLabel->font();
    f.setPointSize(f.pointSize()+2);
    ui->wdginfoLabel->setFont(f);

    f = ui->wdgStatusLabel->font();
    if(f.pointSize()>2)
    {
#ifdef Q_OS_WIN
        f.setPointSize(f.pointSize()-1);
#else
        f.setPointSize(f.pointSize()-2);
#endif
    }
    ui->wdgStatusLabel->setFont(f);

    updTimer->setSingleShot(false);
    updTimer->setInterval(2500);
    connect(updTimer,SIGNAL(timeout()),this,SLOT(update_timeout()));
    ui->controlFrame->setEnabled(false);
    ui->testButton->setHidden(true);

    /*Logger*/
    journal.setup(settings.value(SETTINGS_LOG_DIR,"").toString(),"wdtmon3-mini.log");
    journal.setPaused(settings.value(SETTINGS_LOG_ENABLE,false).toBool()==false);
    journal.setMaxSize(MEGA_BYTES(settings.value(SETTINGS_LOG_SIZE,64).toUInt()));
    journal.setWriteTimeStamp(settings.value(SETTINGS_LOG_TIME,true).toBool());
    journal.setBackupCount(settings.value(SETTINGS_LOG_CNT,3).toUInt());

    journal.log(tr("Application started"));

    /*Build cfg menu*/
    QMenu* cfgMenu = new QMenu(ui->cfgTool);

    cfgMenu->addAction(tr("Scan"),this,SLOT(cfg_action()))->setData(int(A_RESCAN));

    QMenu* logMenu = new QMenu(cfgMenu);
    logMenu->setTitle(tr("Logging"));
    QAction* logAction = logMenu->addAction(tr("Enable"),this,SLOT(cfg_action()));
    logAction->setCheckable(true);
    logAction->setChecked(settings.value(SETTINGS_LOG_ENABLE,false).toBool());
    logAction->setData(int(A_LOG));
    logMenu->addAction(tr("Settings"),this,SLOT(cfg_action()))->setData(A_LOGCFG);
    logMenu->addAction(tr("View"),this,SLOT(cfg_action()))->setData(int(A_LOG_V));
    logMenu->addAction(tr("View All"),this,SLOT(cfg_action()))->setData(int(A_LOG_VA));
    cfgMenu->addMenu(logMenu);

    QAction* ledAction = cfgMenu->addAction(tr("Green LED"),this,SLOT(cfg_action()));
    ledAction->setCheckable(true);
    ledAction->setChecked(settings.value(SETTINGS_GLED,true).toBool());
    ledAction->setData(int(A_LED));

    cfgMenu->addAction(tr("Firmware Update"),this,SLOT(cfg_action()))->setData(int(A_DFU));

    cfgMenu->addSeparator();

    QMenu* srvMenu = new QMenu(cfgMenu);
    srvMenu->setTitle(tr("Web Server"));
    srvEnableAction = srvMenu->addAction(tr("Run"),this,SLOT(cfg_action()));
    srvEnableAction->setCheckable(true);
    srvEnableAction->setChecked(settings.value(SETTINGS_SRV_ENABLED,false).toBool());
    srvEnableAction->setData(int(A_SRV_RUN));

    srvMenu->addAction(tr("Settings"),this,SLOT(cfg_action()))->setData(int(A_SRV_PORT));

    cfgMenu->addMenu(srvMenu);

    QMenu* langMenu = new QMenu(cfgMenu);
    langMenu->setTitle(tr("Language"));
    QAction* systemAction = langMenu->addAction(tr("System"),this,SLOT(cfg_action()));
    systemAction->setCheckable(true);
    systemAction->setData(int(A_LANG_SYSTEM));
    QAction* engAction = langMenu->addAction(tr("English"),this,SLOT(cfg_action()));
    engAction->setCheckable(true);
    engAction->setData(int(A_LANG_EN));
    QAction* rusAction = langMenu->addAction(tr("Russian"),this,SLOT(cfg_action()));
    rusAction->setCheckable(true);
    rusAction->setData(int(A_LANG_RU));

    QActionGroup* langGroup = new QActionGroup(this);
    langGroup->setExclusive(true);
    langGroup->addAction(systemAction);
    langGroup->addAction(engAction);
    langGroup->addAction(rusAction);
    const QString configuredLanguage = settings.value(SETTINGS_LANG).toString();
    systemAction->setChecked(configuredLanguage.isEmpty() ||
                             configuredLanguage.compare(QLatin1String("system"), Qt::CaseInsensitive) == 0);
    engAction->setChecked(configuredLanguage.startsWith(QLatin1String("en"), Qt::CaseInsensitive));
    rusAction->setChecked(configuredLanguage.startsWith(QLatin1String("ru"), Qt::CaseInsensitive));

    cfgMenu->addMenu(langMenu);

    AutoStarter.checkAutostart();
    QAction* astartAct = cfgMenu->addAction(tr("Autostart"),this,SLOT(cfg_action()));
    astartAct->setCheckable(true);
    astartAct->setChecked(AutoStarter.autostartSet());
    astartAct->setData(int(A_START));

    QAction* hideAct = cfgMenu->addAction(tr("Hide at startup"),this,SLOT(cfg_action()));
    hideAct->setCheckable(true);
    hideAct->setChecked(settings.value(SETTINGS_SYS_HIDE,false).toBool());
    hideAct->setData(int(A_HIDE));

    cfgMenu->addSeparator();

    cfgMenu->addAction(tr("About"),this,SLOT(cfg_action()))->setData(int(A_ABOUT));

    cfgMenu->addAction(tr("Exit"),this,SLOT(cfg_action()))->setData(int(A_EXIT));

    ui->cfgTool->setMenu(cfgMenu);

    if(QSystemTrayIcon::isSystemTrayAvailable())
    {
        systray = new QSystemTrayIcon(this);
        systray->setIcon(QIcon(":/images/wdtmon3_lite_red.png"));
        connect(systray,&QSystemTrayIcon::activated,this,&WdtmonLite::trayActivated);
        QMenu* trayMenu = new QMenu(this);
        trayMenu->addAction(tr("Show/Hide"),this,SLOT(cfg_action()))->setData(int(A_VSWITCH));
        trayMenu->addSeparator();
        trayMenu->addAction(tr("Test Reset"),this,SLOT(cfg_action()))->setData(int(A_TEST_RST));
        trayMenu->addAction(tr("Test Hard Reset"),this,SLOT(cfg_action()))->setData(int(A_TEST_HRST));
        trayMenu->addSeparator();
        trayMenu->addAction(tr("Exit"),this,SLOT(cfg_action()))->setData(int(A_EXIT));
        systray->setContextMenu(trayMenu);
        systray->show();
    }

    rescanTimer.setSingleShot(false);
    rescanTimer.setInterval(10000);
    connect(&rescanTimer,SIGNAL(timeout()),this,SLOT(rescan_timeout()));
    rescanTimer.start();

    connect(server,SIGNAL(newMessage(HttpClient*)),this,SLOT(http_message(HttpClient*)));
    if(settings.value(SETTINGS_SRV_ENABLED,false).toBool())
    {
        if(!server->start()) {
            settings.setValue(SETTINGS_SRV_ENABLED, false);
            srvEnableAction->setChecked(false);
        }
    }
    timerEvent(0);
    startTimer(5000);
    set_led(OFF);
}

WdtmonLite::~WdtmonLite()
{
    delete ui;
}

bool WdtmonLite::init()
{
    struct hid_device_info *devs, *cur_dev;
    QSet<QString> watchdogs;

    if (hid_init()!=0) {
        qWarning()<<"HID init failed";
        return false;
    }

    devs = hid_enumerate(OD_VID, OD_PID_WDG);
    cur_dev = devs;
    while (cur_dev) {
        QString vendor  = QString::fromWCharArray(cur_dev->manufacturer_string);
        QString product = QString::fromWCharArray(cur_dev->product_string);
        qWarning()<<"HID watchdog "<<vendor<<" / "<<product<<" @ "<<QString::fromWCharArray(cur_dev->serial_number);
        if((vendor=="Open Development")&&(product==USB_WDG_PRODUCT_HID))
        {
            QString sn = QString::fromWCharArray(cur_dev->serial_number);
            qWarning()<<"Found true watchdog: SN "<<sn;
            watchdogs.insert(sn);
            journal.log(tr("Detected watchdog %1").arg(sn));
        }
        else
        {
            qWarning()<<"Not a true watchdog - ignore";// - throw away this shit!
        }
        cur_dev = cur_dev->next;
    }
    hid_free_enumeration(devs);

    qWarning()<<"Connected watchdogs: "<<QStringList(watchdogs.values());
    ui->wdgselCombo->blockSignals(true);
    ui->wdgselCombo->clear();
    ui->wdgselCombo->addItems(watchdogs.values());
    ui->wdgselCombo->blockSignals(false);
    if(ui->wdgselCombo->count()) {
#ifdef Q_OS_LINUX
        //check rules
        hid_device* test = openHid(ui->wdgselCombo->itemText(0));
        if(!test && !QFile("/etc/udev/rules.d/99-opendev-hid.rules").exists()) {
            //prep temp dir
            const QString appDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            if(appDir.isEmpty() || !QDir().mkpath(appDir)) {
                qWarning()<<"Cannot create application data directory for udev rules: "<<appDir;
                return false;
            }
            const QString targetTmpFile = QDir(appDir).filePath(QStringLiteral("99-opendev-hid.rules"));
            if((QFile::exists(targetTmpFile) && !QFile::remove(targetTmpFile))
                    || !QFile::copy(QStringLiteral(":/unix/99-opendev-hid.rules"), targetTmpFile)) {
                qWarning()<<"Cannot prepare udev rules at "<<targetTmpFile;
                return false;
            }
            const QString installCommand = QStringLiteral("sudo cp %1 /etc/udev/rules.d/99-opendev-hid.rules")
                    .arg(shellQuote(targetTmpFile));
            qApp->clipboard()->setText(installCommand);
            QMessageBox msg(this);
            msg.setWindowTitle(tr("Device Access Permissions"));
            msg.setInformativeText(tr("It seems the udev rules for accessing the watchdog devices are not set.<br>"
                                      "The *.rules file has been prepared and copied to a temporary location.<br>"
                                      "Please open a terminal window and execute the following command:<br>"
                                      "<b>sudo cp %1 /etc/udev/rules.d/99-opendev-hid.rules</b><br>"
                                      "(The command has been copied to clipboard)<br>"
                                      "Reboot your PC after executing this command")
                                   .arg(shellQuote(targetTmpFile).toHtmlEscaped()));
            QFile rules(":/unix/99-opendev-hid.rules");
            rules.open(QFile::ReadOnly);
            msg.setDetailedText(QString::fromUtf8(rules.readAll()));
            msg.setStandardButtons(QMessageBox::Ok);
            msg.exec();
            return false;
        }
        if(test) {
            hid_close(test);
        }
#endif
        on_wdgselCombo_currentIndexChanged(0);
    } else {
        ui->controlFrame->setEnabled(false);
    }
    return true;
}

void WdtmonLite::setDevMode(bool on)
{
    ui->testButton->setHidden(!on);
}

void WdtmonLite::message(const QString& msg)
{
    if(msg=="show")
    {
        show();
        raise();
    }
    else if(msg=="hide")
    {
        mhide();
    }
}

void WdtmonLite::closeEvent(QCloseEvent *e)
{
    e->ignore();
    mhide();
}

void WdtmonLite::timerEvent(QTimerEvent*)
{
    ui->uptimeLabel->setText(QString("APP: %1\nSYS: %2\nCON: %3")
                             .arg(uptimeString(getAppUptimeMS()))
                             .arg(uptimeString(getSysUptimeMS()))
                             .arg(server->clients()));
}

void WdtmonLite::mhide()
{
    if(systray)
    {
        hide();
    }
    else
    {
        showMinimized();
    }
}

void WdtmonLite::on_wdgselCombo_currentIndexChanged(int index)
{
    updTimer->stop();
    ui->wdginfoLabel->setStyleSheet(wdgLabelStyle(":/images/watchdog_error_bg.png"));
    ui->wdginfoLabel->setText(tr("Cannot communicate with device %1").arg(ui->wdgselCombo->itemText(index)));
    ui->controlFrame->setEnabled(false);
    hid_device* hnd = openHid(ui->wdgselCombo->itemText(index));
    if(hnd) {
        // Set the hid_read() function to be non-blocking.
        hid_set_nonblocking(hnd, 1);

        uint8_t feature_buf[32];
        feature_buf[0] = HID_ID_VER;
        int res = hid_get_feature_report(hnd,feature_buf,17);
        if(res != 17) {
            qWarning()<<"Watchdog did not reply to VER report - returned "<<res;
#ifdef Q_OS_WIN
            qWarning()<<"Last error: "<<QString::fromWCharArray(hid_error(hnd));
#endif
            hid_close(hnd);
            return;
        }
        wdg_ver = QString::fromLatin1((const char*)(feature_buf+1),16);
        wdg_mod = "???";
        if(wdg_ver.at(3)=='U') {
            //pro
            ui->wdginfoLabel->setStyleSheet(wdgLabelStyle(":/images/watchdog_pro_2.png"));
            ui->controlFrame->setEnabled(true);
            ui->resetButton->setHidden(false);
            ui->powerButton->setHidden(false);
            ui->shutButton->setHidden(false);
            updTimer->start();
            wdg_mod = tr("Pro2");
        } else if(wdg_ver.at(3)=='L') {
            ui->wdginfoLabel->setStyleSheet(wdgLabelStyle(":/images/watchdog_lite_2.png"));
            ui->controlFrame->setEnabled(true);
            ui->resetButton->setHidden(true);
            ui->powerButton->setHidden(true);
            ui->shutButton->setHidden(true);
            updTimer->start();
            wdg_mod = tr("Lite");
        } else {
            ui->wdginfoLabel->setStyleSheet(wdgLabelStyle(":/images/watchdog_unknown_bg.png"));
        }
        ui->wdginfoLabel->setText(tr("<b>Watchdog %3</b> v<i>%1</i>&nbsp;&nbsp;<br>build <b>%2</b>&nbsp;&nbsp;")
                                  .arg(wdg_ver.left(3))
                                  .arg(wdg_ver.mid(5))
                                  .arg(wdg_mod));

        //Setup LED state
        uint8_t buf[2];
        buf[0] = HID_ID_CMD;
        buf[1] = settings.value(SETTINGS_GLED,true).toBool() ? HID_LED_G_ON : HID_LED_G_OFF;
        hid_write(hnd,buf,2);

        hid_close(hnd);
    }
}

void WdtmonLite::on_ledButton_clicked()
{
    bool new_state = !settings.value(SETTINGS_GLED,true).toBool();
    settings.setValue(SETTINGS_GLED,new_state);
    QList<QAction*> actList = ui->cfgTool->menu()->actions();
    foreach(QAction* act, actList)
    {
        if(act->data().toInt()==A_LED)
        {
            act->setChecked(new_state);
            break;
        }
    }
    sendWdgCMD(ui->wdgselCombo->currentText(),new_state ? HID_LED_G_ON : HID_LED_G_OFF);
}

void WdtmonLite::on_resetButton_clicked()
{
    journal.log("System reset");
    sendWdgCMD(ui->wdgselCombo->currentText(), HID_REBOOT);
}

void WdtmonLite::on_powerButton_clicked()
{
    journal.log("System hard reset");
    sendWdgCMD(ui->wdgselCombo->currentText(), HID_HREBOOT);
}

void WdtmonLite::on_shutButton_clicked()
{
    journal.log("System shut down");
    sendWdgCMD(ui->wdgselCombo->currentText(), HID_SHUT);
}

void WdtmonLite::on_testButton_clicked()
{
    sendWdgCMD(ui->wdgselCombo->currentText(), HID_TEST);
}

void WdtmonLite::update_timeout()
{
    sendWdgCMD(ui->wdgselCombo->currentText(), HID_RELOAD);
}

void WdtmonLite::rescan_timeout()
{
    if(ui->wdgselCombo->count())
    {
        rescanTimer.stop();
        return;
    }
    init();
}

void WdtmonLite::cfg_action()
{
    QAction* act = qobject_cast<QAction*>(sender());
    switch(act->data().toInt())
    {
        case A_LOG:
        {
            settings.setValue(SETTINGS_LOG_ENABLE,act->isChecked());
            journal.setPaused(!act->isChecked());
            break;
        }
        case A_LOGCFG:
        {
            LogSetupDialog lsdlg(this);
            if(lsdlg.exec()==QDialog::Accepted)
            {
                lsdlg.write(&journal);
            }
            break;
        }
        case A_LOG_V:
        {
            if(QFileInfo(journal.getLogFile()).exists())
            {
                QDesktopServices::openUrl(QUrl::fromLocalFile(journal.getLogFile()));
            }
            else
            {
                QMessageBox::warning(this,tr("wdtmon3-mini"),tr("No log file"));
            }
            break;
        }
        case A_LOG_VA:
        {
            if(QFileInfo(journal.getLogFile()).exists())
            {
                QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(journal.getLogFile()).absolutePath()));
            }
            else
            {
                QMessageBox::warning(this,tr("wdtmon3-mini"),tr("No log files"));
            }
            break;
        }
        case A_LED:
        {
            settings.setValue(SETTINGS_GLED,act->isChecked());
            sendWdgCMD(ui->wdgselCombo->currentText(),act->isChecked() ? HID_LED_G_ON : HID_LED_G_OFF);
            break;
        }
        case A_LANG_SYSTEM:
        {
            if(!settings.value(SETTINGS_LANG).toString().isEmpty())
            {
                settings.setValue(SETTINGS_LANG, QString());
                mode = Boot_Restart;
                qApp->quit();
                return;
            }
            break;
        }
        case A_LANG_EN:
        {
            if(!settings.value(SETTINGS_LANG).toString()
                    .startsWith(QLatin1String("en"), Qt::CaseInsensitive))
            {
                settings.setValue(SETTINGS_LANG,"en_US");
                mode = Boot_Restart;
                qApp->quit();
                return;
            }
            break;
        }
        case A_LANG_RU:
        {
            if(!settings.value(SETTINGS_LANG).toString()
                    .startsWith(QLatin1String("ru"), Qt::CaseInsensitive))
            {
                settings.setValue(SETTINGS_LANG,"ru_RU");
                mode = Boot_Restart;
                qApp->quit();
                return;
            }
            break;
        }
        case A_DFU:
        {
            if(ui->wdgselCombo->count())
            {
                if(QMessageBox::question(this,tr("Firmware Update"),
                                         tr("Are you sure you want to update the firmware?"),
                                         QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)
                {
                    break;
                }
                sendWdgCMD(ui->wdgselCombo->currentText(),HID_BOOT_UDFU);
            }
            mode = Boot_DFU;
            updTimer->stop();
            QTimer::singleShot(1000,qApp,SLOT(quit()));
            break;
        }
        case A_RESCAN:
            init();
            break;
        case A_EXIT:
            mode = Boot_Normal;
            qApp->quit();
            return;
        case A_VSWITCH:
            if(!isVisible()) show();
            else mhide();
            break;
        case A_TEST_RST:
            on_resetButton_clicked();
            break;
        case A_TEST_HRST:
            on_powerButton_clicked();
            break;
        case A_SRV_RUN:
        {
            settings.setValue(SETTINGS_SRV_ENABLED,act->isChecked());
            if(act->isChecked())
            {
                server->start();
                if(!server->isRunning()) {
                    act->setChecked(false);
                    settings.setValue(SETTINGS_SRV_ENABLED, false);
                }
            }
            else
            {
                server->stop();
            }
            break;
        }
        case A_SRV_PORT:
        {
            HttpSetupDialog hdlg(this);
            hdlg.setup(server);
            (void)hdlg.exec();
            if(server->isRunning())
            {
                srvEnableAction->setChecked(true);
            }
            break;
        }
        case A_START:
        {
            AutoStarter.setAutostart(act->isChecked());
            break;
        }
        case A_HIDE:
        {
            settings.setValue(SETTINGS_SYS_HIDE,act->isChecked());
            break;
        }
        case A_ABOUT:
        {
            QMessageBox::about(this,tr("wdtmon3-mini")+QStringLiteral(" ")+WDTMON_LITE_VERSION,
                               tr("Software for USB Watchdog Pro2/Lite - 2018 devices control.")+
                               QStringLiteral("<br>")+
                               tr("Developed by: Open Development LLC")+
                               QStringLiteral(" ")+QString(QChar(0x00A9))+QStringLiteral("2018")+
                               QStringLiteral("<br>")+
                               QStringLiteral("<a href=\"https://help.unitx.pro\">help.unitx.pro</a><br><br>")+
                               tr("Uses:")+
                               QStringLiteral("<ul>"
                                              "<li><a href=\"https://github.com/signal11/hidapi\">signal11/hidapi</a> (BSD-style license)</li>"
                                              "<li><a href=\"https://github.com/nodejs/http-parser\">http-parser</a> (MIT license)</li>"
                                              "<li><a href=\"https://github.com/GNOME/adwaita-icon-theme\">adwaita-icon-theme</a> (CC-BY-SA 3.0)</li>"
                                              "<li><a href=\"https://material.io/icons/\">Material Icons</a> (Apache v2.0 License)</li>"
                                              "</ul>"));
            break;
        }
        default:break;
    }
}

void WdtmonLite::trayActivated(QSystemTrayIcon::ActivationReason reason)
{
#ifndef Q_OS_DARWIN
    if(reason==QSystemTrayIcon::Trigger)
    {
        if(!isVisible()) show();
        else hide();
    }
#endif
}

void WdtmonLite::http_message(HttpClient* client)
{
    if(client->method==HTTP_GET)
    {
        QTextStream os(client->fd);
        os.setCodec("UTF-8");

        if(client->request_url=="/")
        {
            qDebug()<<"Request main page";
            sendMainPage(os);
        }
        else if((client->request_url=="/logo")||(client->request_url=="/favicon.png"))
        {
            qDebug()<<"Request logo";
            QFile logo_file((client->request_url=="/logo") ? ":/srv/http/logo.png" : ":/srv/http/favicon.png");
            if(logo_file.open(QFile::ReadOnly))
            {
                qDebug()<<"Sending logo ("<<logo_file.size()<<") bytes";
                os<<"HTTP/1.1 200 OK\r\n"
                    "Content-Type: image/png\r\n"
                    "Date: "<<QDateTime::currentDateTime().toString("ddd, dd MMM yyyy hh:mm:ss t")
                 <<"\r\n"
                   "Content-Length: "
                <<logo_file.size()
                <<"\r\n"
                  "\r\n";
                os.flush();
                client->fd->write(logo_file.readAll());
            }
            else
            {
                qWarning()<<"Cannot read logo";
                os<<"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
            }
        }
        else if(client->request_url=="/data")
        {
            QJsonObject dataObj;
            dataObj.insert("time",wdg_data_time.toString("yyyy.MM.dd hh:mm:ss"));
            QJsonObject ch1_obj;
            if(wdg_ch1!=0xFF)
            {
                ch1_obj.insert("value",wdg_ch1);
                ch1_obj.insert("state",wdg_ch1 ? QStringLiteral("open") : QStringLiteral("closed"));
            }
            dataObj.insert("ch1",ch1_obj);

            QJsonObject ch2_obj;
            if(wdg_ch2!=0xFF)
            {
                ch2_obj.insert("value",wdg_ch2);
                ch2_obj.insert("state",wdg_ch2 ? QStringLiteral("open") : QStringLiteral("closed"));
            }
            dataObj.insert("ch2",ch2_obj);

            if(wdg_ch3_val<0)
            {
                dataObj.insert("ch3",QJsonObject());
            }
            else
            {
                QJsonObject tempObj;
                switch(wdg_ch3)
                {
                    case ODC_WDGPRO2_CHANNELSETTINGS_TEMP_INPUT:
                        tempObj.insert("type",QJsonValue(QStringLiteral("input")));
                        break;
                    case ODC_WDGPRO2_CHANNELSETTINGS_TEMP_OUTPUT:
                        tempObj.insert("type",QJsonValue(QStringLiteral("output")));
                        break;
                    case ODC_WDGPRO2_CHANNELSETTINGS_TEMP_TEMP:
                        tempObj.insert("type",QJsonValue(QStringLiteral("temperature")));
                        break;
                    default:
                        tempObj.insert("type",QJsonValue(QStringLiteral("off")));
                        break;
                }
                tempObj.insert("value",QJsonValue(wdg_ch3_val));
                dataObj.insert("ch3",tempObj);
            }
            QByteArray jsonData = QJsonDocument(dataObj).toJson();
            os<<"HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json; charset=utf-8\r\n"
                "Cache-Control: no-store\r\n"
                "Content-Length: "<<jsonData.size()
             <<"\r\n\r\n";
            os.flush();
            client->fd->write(jsonData);
        }
#if 0
        else
        {
            QUrl url(QStringLiteral("http://localhost")+QString::fromUtf8(client->request_url));
            qDebug()<<"Requested path: "<<url.path();
            if(url.path()=="/action")
            {
                QUrlQuery queries(url);
                if(queries.queryItemValue("ping")=="1")
                {
                    qDebug()<<"Request ping";
                    sendWdgCMD(ui->wdgselCombo->currentText(), HID_RELOAD);
                }
                else if(queries.queryItemValue("reset")=="1")
                {
                    qDebug()<<"Request reset";
                    on_resetButton_clicked();
                }
                else if(queries.queryItemValue("hreset")=="1")
                {
                    qDebug()<<"Hard reset requested";
                    on_powerButton_clicked();
                }
                else
                {
                    qWarning()<<"Invalid query";
                }

                sendMainPage(os);
            }
            else
            {
                qWarning()<<"Unknown 'GET' url "<<QString::fromUtf8(client->request_url);
                os<<"HTTP/1.1 404 Not Found\r\n\r\n";
            }
        }
#endif
        else
        {
            qWarning()<<"Unknown 'GET' url "<<QString::fromUtf8(client->request_url);
            os<<"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        }
    }
    else if(client->method == HTTP_POST)
    {
        if(client->request_url=="/")
        {
            QTextStream os(client->fd);
            os.setCodec("UTF-8");

            QUrl url(QStringLiteral("http://localhost/action?")+QString::fromUtf8(client->body));
            qDebug()<<"Requested path: "<<url.path();

            QUrlQuery queries(url);
            if(queries.queryItemValue(QStringLiteral("token")) != httpToken)
            {
                qWarning()<<"Rejected HTTP action with an invalid CSRF token";
                os<<"HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
                client->keep_alive = false;
                client->replied();
                return;
            }
            if(queries.queryItemValue("ping")=="1")
            {
                qDebug()<<"Request ping";
                sendWdgCMD(ui->wdgselCombo->currentText(), HID_RELOAD);
            }
            else if(queries.queryItemValue("reset")=="1")
            {
                qDebug()<<"Request reset";
                on_resetButton_clicked();
            }
            else if(queries.queryItemValue("hreset")=="1")
            {
                qDebug()<<"Hard reset requested";
                on_powerButton_clicked();
            }
            else
            {
                qWarning()<<"Invalid query";
            }

            sendMainPage(os);
        } else {
            QTextStream os(client->fd);
            os<<"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        }
    }
    else
    {
        QTextStream os(client->fd);
        os<<"HTTP/1.1 405 Method Not Allowed\r\nAllow: GET, POST\r\nContent-Length: 0\r\n\r\n";
    }
    client->keep_alive = false;
    client->replied();
}

bool WdtmonLite::sendWdgCMD(const QString& sn, uint8_t cmd)
{
    uint8_t feature_buf[17];

    hid_device* hnd = openHid(sn);
    if(hnd) {
        uint8_t buf[2];
        buf[0] = HID_ID_CMD;
        buf[1] = cmd;
        int res = hid_write(hnd,buf,2);

        if(res < 2) {
            qWarning()<<"WDG cmd returned error";
            ui->wdgStatusLabel->setText("");
            set_led(WARN);
            hid_close(hnd);
            return false;
        }

        switch(cmd)
        {
            case HID_REBOOT:
            {
                journal.log(tr("Reset requested"));
                hid_close(hnd);
                return true;
            }
            case HID_HREBOOT:
            {
                journal.log(tr("Hard reset requested"));
                hid_close(hnd);
                return true;
            }
            case HID_SHUT:
            {
                journal.log(tr("Shut down requested"));
                hid_close(hnd);
                return true;
            }
            case HID_BOOT_SDFU:
            case HID_BOOT_UDFU:
            {
                journal.log(tr("Reboot to DFU requested"));
                hid_close(hnd);
                return true;
            }
            default:break;
        }

        set_led(settings.value(SETTINGS_GLED,true).toBool() ? ON : TINT);

        hid_set_nonblocking(hnd, 1);
        feature_buf[0] = HID_ID_DATA;
        if(hid_get_feature_report(hnd,feature_buf,17)==17) {
            wdg_data_time = QDateTime::currentDateTime();
            wdg_ch1 = feature_buf[1];
            if((wdg_ch2=feature_buf[2])==0xFF) {
                //Lite
                ui->wdgStatusLabel->setText(tr("CH1: %1").arg( wdg_ch1 ));
                wdg_ch3 = 0xFF;
                wdg_ch3_val = -1;
            } else {
                //Pro
                QString state = tr("CH1: %1, CH2: %2")
                                .arg( (wdg_ch1==0) ? tr("closed") : tr("open") )
                                .arg( (wdg_ch2==0) ? tr("closed") : tr("open") );
                state += QStringLiteral("\n");
                switch((wdg_ch3 = feature_buf[3])) {
                    case ODC_WDGPRO2_CHANNELSETTINGS_TEMP_INPUT: {
                        wdg_ch3_val = feature_buf[4];
                        state += tr("INPUT: ")+QString::number(feature_buf[4]);
                        break;
                    }
                    case ODC_WDGPRO2_CHANNELSETTINGS_TEMP_OUTPUT: {
                        wdg_ch3_val = feature_buf[4];
                        state += tr("CH3: ")+(feature_buf[4] ? "1" : "0");
                        break;
                    }
                    case ODC_WDGPRO2_CHANNELSETTINGS_TEMP_TEMP: {
                        if(feature_buf[4]==0xFF) {
                            state += tr("TEMP: n/a");
                            wdg_ch3_val = -1;
                        } else {
                            int8_t temp_int = *(int8_t*)(feature_buf+4);
                            uint16_t temp_frac = ((uint16_t)feature_buf[6])<<8 | feature_buf[5];
                            wdg_ch3_val = double(temp_int) + double(temp_frac/1000)/10;
                            state += tr("TEMP: ") + QString("%1%2").arg(wdg_ch3_val).arg(QChar(0x2103));
                        }
                        break;
                    }
                    default: {
                        state += tr("CH3: off");
                        wdg_ch3_val = -1;
                    }
                }
                ui->wdgStatusLabel->setText(state);
            }
            hid_close(hnd);
            return true;
        } else {
            ui->wdgStatusLabel->setText("");
        }
        hid_close(hnd);
    } else {
        ui->wdgStatusLabel->setText("");
        set_led(WARN);
    }
    return false;
}

void WdtmonLite::set_led(LEDS type)
{
    ui->ledButton->setIcon(QIcon(led_images[type]));
    if(type != OFF) {
        QTimer::singleShot(1000,this,SLOT(led_off()));
    }
    if(systray)
    {
        switch(type)
        {
            case OFF:
                systray->setIcon(QIcon(":/images/wdtmon3_lite.png"));
                break;
            case ON:
            case TINT:
                systray->setIcon(QIcon(":/images/wdtmon3_lite_green.png"));
                break;
            case WARN:
                systray->setIcon(QIcon(":/images/wdtmon3_lite_orange.png"));
                break;
        }
    }
}

void WdtmonLite::sendMainPage(QTextStream& os)
{
    QFile http_index(":/srv/http/index.html");
    if(ui->wdgselCombo->currentText().isEmpty())
    {
        os<<"HTTP/1.1 503 Service Unavailable\r\n\r\n";
    }
    else if( !http_index.open(QFile::ReadOnly))
    {
        os<<"HTTP/1.1 404 Not Found\r\n\r\n";
    }
    else
    {
        QString data = QString::fromUtf8(http_index.readAll());
        http_index.close();

        data.replace("%SYS_UPTIME%",uptimeString(getSysUptimeMS()));
        data.replace("%APP_UPTIME%",uptimeString(getAppUptimeMS()));

        data.replace("%SERIAL%",ui->wdgselCombo->currentText().toHtmlEscaped());
        data.replace("%WDG_TYPE%",wdg_mod.toHtmlEscaped());
        data.replace("%WDG_VER%",wdg_ver.toHtmlEscaped());
        data.replace("%CSRF_TOKEN%",httpToken.toHtmlEscaped());
        data.replace("%SYS_TIME%",wdg_data_time.toString("yyyy.MM.dd hh:mm:ss"));
        data.replace("%WDG_CH1%", wdg_ch1 == 0xFF ? tr("n/a") : ((wdg_ch1==0) ? tr("closed") : tr("open")));
        data.replace("%WDG_CH2%", wdg_ch2 == 0xFF ? tr("n/a") : ((wdg_ch2==0) ? tr("closed") : tr("open")));
        switch(wdg_ch3)
        {
            case ODC_WDGPRO2_CHANNELSETTINGS_TEMP_INPUT:
                data.replace("%WDG_TEMP%",(QString::number(wdg_ch3_val)+QStringLiteral(" (")+tr("input")+QStringLiteral(")")));
                break;
            case ODC_WDGPRO2_CHANNELSETTINGS_TEMP_OUTPUT:
                data.replace("%WDG_TEMP%",(QString::number(wdg_ch3_val)+QStringLiteral(" (")+tr("output")+QStringLiteral(")")));
                break;
            case ODC_WDGPRO2_CHANNELSETTINGS_TEMP_TEMP:
                if(wdg_ch3_val<0)
                {
                    data.replace("%WDG_TEMP%",tr("n/a"));
                }
                else
                {
                data.replace("%WDG_TEMP%",QString("%1&deg;C").arg(wdg_ch3_val));
                }
                break;
            default:
                data.replace("%WDG_TEMP%",tr("off"));
                break;
        }


        const QByteArray page = data.toUtf8();
        os<<"HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Cache-Control: no-store\r\n"
            "Content-Security-Policy: default-src 'self'; style-src 'unsafe-inline'; img-src 'self'\r\n"
            "X-Content-Type-Options: nosniff\r\n"
            "Content-Length: "<<page.size()<<"\r\n\r\n";
        os.flush();
        os.device()->write(page);
    }
}
