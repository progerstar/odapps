#include "qrfidpreferences.h"
#include <ui_qrfidpreferences.h>

#include <QSettings>
#include <QMessageBox>

#include <qi18n.h>
#include <mifarekeydatabase.h>
#include <themedetector.h>
#include <qmaterialfont.h>

#include "rfidkit_global.h"

QRFIDPreferences::QRFIDPreferences(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::QRFIDPreferences)
{
    QColor icon_color = ThemeDetector::iconColor();
    ui->setupUi(this);
    MaterialUI("help", ui->udpHelpTool, icon_color);
    MaterialUI("help", ui->tcpHelpTool, icon_color);

    connect(this,SIGNAL(accepted()),this,SLOT(writeSettings()));

    ui->udpPortSpin->setRange(1,MAX_PORT);
    ui->tcpPortSpin->setRange(1,MAX_PORT);

    readSettings();
}

QRFIDPreferences::~QRFIDPreferences()
{
    delete ui;
}

void QRFIDPreferences::readSettings()
{
    QSettings set;
    ui->emPwdKeepCheck->setChecked(set.value(SETTINGS_KEEPPWD, false).toBool());
    ui->keydbCheck->setChecked(set.value(SETTINGS_KEYS,false).toBool());
    ui->udpServerGroup->setChecked(set.value(SETTINGS_UDP,false).toBool());
    ui->udpPortSpin->setValue(set.value(SETTINGS_UDP_PORT,12345).toInt());
    ui->tcpServerGroup->setChecked(set.value(SETTINGS_TCP,false).toBool());
    ui->tcpPortSpin->setValue(set.value(SETTINGS_TCP_PORT,12346).toInt());

    QLANG::setupLangCombo(set,ui->langCombo);
}

void QRFIDPreferences::on_keydbDropTool_clicked()
{
    if(QMessageBox::question(this,tr("Key Database"),
                             tr("This will delete all the stored keys. This action is irreversible. Proceed?"),
                             QMessageBox::Yes|QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes)
    {
        MifareKeyDatabase::instance()->cleanDatabase();
        ui->keydbDropTool->setEnabled(false);
    }
}

void QRFIDPreferences::on_tcpHelpTool_clicked()
{
    QMessageBox::information(this, tr("TCP Protocol"),
                             tr("This software will run a TCP server at port %1").arg(ui->tcpPortSpin->value())+
                             QLatin1String("<br><ul><li>")+
                             tr("Each connected client will receive a plain-text message %1 each time an RFID tag is detected.")
                             .arg(QLatin1String("<b>card&gt; type: ...; uid: ...\\n</b>"))+QLatin1String("</li><li>")+
                             tr("Send %1 command to get the UID of the current tag").arg(QLatin1String("<b>uid\\n</b>"))+
                             QLatin1String("</li><li>")+tr("Send %1 command to get the current reader state")
                             .arg(QLatin1String("<b>state\\n</b>"))+QLatin1String("</li></ul>"));
}

void QRFIDPreferences::on_udpHelpTool_clicked()
{
    QMessageBox::information(this, tr("UDP Protocol"),
                             tr("This software uses a session-like approach: send %1 message to port %2 to register as a client. %3 will be sent in reply. A session will be opened and a notification (%4) will be sent to the recorded IP and port via UDP protocol each time an RFID tag is detected.")
                             .arg(QLatin1String("<b>hello\\n</b>")).arg(ui->udpPortSpin->value()).arg(QLatin1String("<b>ok\\n</b>"))
                             .arg(QLatin1String("<b>card&gt; type: ...; uid: ...\\n</b>"))+
                             QLatin1String("<ul><li>")+
                             tr("Send %1 command to get the UID of the current tag").arg(QLatin1String("<b>uid\\n</b>"))+
                             QLatin1String("</li><li>")+
                             tr("Send %1 command to get the current reader state").arg(QLatin1String("<b>state\\n</b>"))+QLatin1String("</li><li>")+
                             tr("Send %1 command to end the session. No more notifications will be sent")
                             .arg(QLatin1String("<b>goodbye\\n</b>"))+
                             QLatin1String("</li></ul>"));

}

void QRFIDPreferences::writeSettings()
{
    QSettings set;
    set.setValue(SETTINGS_KEEPPWD, ui->emPwdKeepCheck->isChecked());
    set.setValue(SETTINGS_KEYS,ui->keydbCheck->isChecked());
    set.setValue(SETTINGS_UDP,ui->udpServerGroup->isChecked());
    set.setValue(SETTINGS_UDP_PORT,ui->udpPortSpin->value());
    set.setValue(SETTINGS_TCP,ui->tcpServerGroup->isChecked());
    if(ui->udpServerGroup->isChecked() && ui->tcpServerGroup->isChecked() &&
       (ui->udpPortSpin->value()==ui->tcpPortSpin->value()))
    {
        int tport = (ui->udpPortSpin->value()==MAX_PORT) ? (MAX_PORT-1) : (ui->udpPortSpin->value()+1);
        QMessageBox::warning(this,tr("Port Conflict"),
                             tr("UDP and TCP servers cannot use the same port.\n"
                                "TCP port has been changed to %1").arg(tport));
        set.setValue(SETTINGS_TCP_PORT,tport);
    }
    else
        set.setValue(SETTINGS_TCP_PORT,ui->tcpPortSpin->value());

    QLANG::applyLangCombo(set,ui->langCombo,this);
}
