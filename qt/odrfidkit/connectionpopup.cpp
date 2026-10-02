#include "connectionpopup.h"

#include "rfidkit_global.h"

#include <ui_connectionpopup.h>
#include <qmaterialfont.h>

#include <rfidcardcdcatreader.h>
#include <rfidcardhidreader.h>

#include <QSettings>

ConnectionPopup::ConnectionPopup(QWidget* parent) : QFrame(parent, Qt::Popup), ui(new Ui::ConnectionPopup), _accepted(false)
{
    ui->setupUi(this);
    MaterialUI("sync", ui->rescanButton);
    MaterialUI("done", ui->applyButton);

    connect(ui->rescanButton, &QToolButton::clicked, this, &ConnectionPopup::enumerateDevices);
}

ConnectionPopup::~ConnectionPopup()
{
    delete ui;
}

void ConnectionPopup::readSettings()
{
    QSettings set;
    QString lp = set.value(SETTINGS_LAST_PORT).toString();

    enumerateDevices();

    if(!lp.isEmpty()) {
        ui->portsCombo->setCurrentText(lp);
    }
}

void ConnectionPopup::writeSettings()
{
    QSettings().setValue(SETTINGS_LAST_PORT, ui->portsCombo->currentText());
}

QString ConnectionPopup::text() const
{
    return ui->portsCombo->currentText();
}

QVariant ConnectionPopup::userData() const
{
    return ui->portsCombo->currentData();
}

void ConnectionPopup::setupPorts(const PortList& p)
{
    QString sel = ui->portsCombo->currentText();
    ui->portsCombo->clear();
    for(const auto& i : p) {
        ui->portsCombo->addItem(i.first, i.second);
    }
    ui->portsCombo->setCurrentText(sel);
}

void ConnectionPopup::on_applyButton_clicked()
{
    _accepted = true;
    close();
}

void ConnectionPopup::enumerateDevices()
{
    PortList ports;
    ports.append(RFIDCardCdcAtReader::availableDeviceList(RFIDCardCdcAtReader::CDC));
    //prepare selection;
    ports.append(RFIDCardHIDReader::availableDeviceList());
#ifndef OD_NO_DEVELOPER
    ports.append(RFIDCardCdcAtReader::availableDeviceList(RFIDCardCdcAtReader::UART));
    ports.append(RFIDCardCdcAtReader::availableDeviceList(RFIDCardCdcAtReader::RS485));
#endif

    ports.append(QPair<QString,QVariant>(tr("Simulator"), QVariant()));

    setupPorts(ports);
}
