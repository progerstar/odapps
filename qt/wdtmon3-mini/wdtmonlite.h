#ifndef WDTMONLITE_H
#define WDTMONLITE_H

#include <QWidget>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QSettings>

#include <hidapi.h>
#include <qjournald.h>

#include "odhid_global.h"
#include "qmuhttp.h"

#define WDTMON_LITE_VERSION     "3.4.0"

#define SETTINGS_GLED           "Device/GLED"
#define SETTINGS_SYS_HIDE       "System/StartHidden"
#define SETTINGS_LOG_ENABLE     "System/LogEnabled"
#define SETTINGS_LOG_DIR        "System/LogDirectory"
#define SETTINGS_LOG_TIME       "System/LogTimestamp"
#define SETTINGS_LOG_SIZE       "System/LogSize"
#define SETTINGS_LOG_CNT        "System/LogCount"
#define SETTINGS_SYS_STARTALERT "System/StartupAlert"
#define SETTINGS_SYS_STARTBLOCK "System/StartupBlock"
#define SETTINGS_SRV_ENABLED    "HTTP/Run"

typedef enum{
    ODC_WDGPRO2_CHANNELSETTINGS_TEMP_OFF = 0,
    ODC_WDGPRO2_CHANNELSETTINGS_TEMP_INPUT = 1,
    ODC_WDGPRO2_CHANNELSETTINGS_TEMP_OUTPUT = 2,
    ODC_WDGPRO2_CHANNELSETTINGS_TEMP_TEMP = 3,
} ODC_WDGPRO2_CHANNELSETTINGS_TEMP;

namespace Ui {
class wdtmon3mini;
}

class WdtmonLite : public QWidget
{
        Q_OBJECT

    public:
        enum LEDS {
            OFF = 0,
            ON,
            WARN,
            TINT
        };

        enum Actions {
            A_LOG,
            A_LOGCFG,
            A_LOG_V,
            A_LOG_VA,
            A_LED,
            A_LANG_SYSTEM,
            A_LANG_EN,
            A_LANG_RU,
            A_DFU,
            A_RESCAN,
            A_EXIT,
            A_VSWITCH,
            A_TEST_RST,
            A_TEST_HRST,
            A_SRV_RUN,
            A_SRV_PORT,
            A_START,
            A_ABOUT,
            A_HIDE
        };

        explicit WdtmonLite(QWidget *parent = 0);
        ~WdtmonLite();

        bool init();
        void setDevMode(bool on);
        inline BootMode exitMode()
        {
            return mode;
        }
    public slots:
        void message(const QString& msg);
    protected:
        virtual void closeEvent(QCloseEvent* e) override;
        virtual void timerEvent(QTimerEvent* ev) override;
    protected slots:
        void mhide();
    private slots:
        void on_wdgselCombo_currentIndexChanged(int index);
        void on_ledButton_clicked();
        void on_resetButton_clicked();
        void on_powerButton_clicked();
        void on_shutButton_clicked();
        void on_testButton_clicked();

        void update_timeout();
        void rescan_timeout();
        inline void led_off() { set_led(OFF); }

        void cfg_action();
        void trayActivated(QSystemTrayIcon::ActivationReason reason);
        void http_message(HttpClient* client);
    private:
        Ui::wdtmon3mini *ui;
        BootMode mode;
        QSettings settings;
        QJournal journal;
        QTimer* updTimer;
        QSystemTrayIcon* systray;
        QMUHttpServer* server;
        QAction* srvEnableAction;
        QString httpToken;
        QTimer rescanTimer;

        QString wdg_mod;
        QString wdg_ver;

        QDateTime wdg_data_time;
        quint8 wdg_ch1,wdg_ch2, wdg_ch3;
        double wdg_ch3_val;

        bool sendWdgCMD(const QString& sn, uint8_t cmd);
        void set_led(LEDS type);
        void sendMainPage(QTextStream& os);
};

#endif // WDTMONLITE_H
