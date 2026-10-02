#include "httpsetupdialog.h"
#include <ui_httpsetupdialog.h>

#include "wdtmonlite.h"
#include <qmuhttp.h>

#include <QDesktopServices>

HttpSetupDialog::HttpSetupDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::HttpSetupDialog), srv(0)
{
    ui->setupUi(this);
    ui->srvEnableButton->setEnabled(false);
    ui->srvOpenButton->setEnabled(false);
    ui->srvAddressLabel->setOpenExternalLinks(true);
    ui->srvAddressLabel->setText("");
    ui->srvAddressLabel->setAlignment(Qt::AlignCenter);
}

void HttpSetupDialog::setup(QMUHttpServer *server)
{
    srv = server;
    if(!srv) return;

    uint port = QSettings().value(SETTINGS_HTTP_PORT,SETTINGS_HTTP_PORT_DEFAULT).toUInt();
    ui->srvPortSpin->setValue(port);
    ui->srvEnableButton->setEnabled(!srv->isRunning());
    ui->srvOpenButton->setEnabled(srv->isRunning());
    if(srv->isRunning())
    {
        ui->srvAddressLabel->setText(QString("<a style=\"text-decoration:none\" href=\"http://localhost:%1\">"
                                             "http://localhost:%1</a>").arg(port));
    }
}

HttpSetupDialog::~HttpSetupDialog()
{
    srv = 0;
    delete ui;
}

void HttpSetupDialog::on_srvEnableButton_clicked()
{
    if(srv)
    {
        QSettings().setValue(SETTINGS_SRV_ENABLED,true);
        uint port = QSettings().value(SETTINGS_HTTP_PORT,SETTINGS_HTTP_PORT_DEFAULT).toUInt();
        srv->start();
        if(!srv->isRunning())
        {
            QSettings().setValue(SETTINGS_SRV_ENABLED,false);
            ui->srvEnableButton->setEnabled(true);
            ui->srvOpenButton->setEnabled(false);
            ui->srvAddressLabel->clear();
            return;
        }
        ui->srvAddressLabel->setText(QString("<a style=\"text-decoration:none\" href=\"http://localhost:%1\">"
                                             "http://localhost:%1</a>").arg(port));
        ui->srvOpenButton->setEnabled(true);
    }
    ui->srvEnableButton->setEnabled(false);
}

void HttpSetupDialog::on_srvOpenButton_clicked()
{
    uint port = QSettings().value(SETTINGS_HTTP_PORT,SETTINGS_HTTP_PORT_DEFAULT).toUInt();
    QDesktopServices::openUrl(QString("http://localhost:%1").arg(port));
}

void HttpSetupDialog::on_srvPortApply_clicked()
{
    QSettings().setValue(SETTINGS_HTTP_PORT,ui->srvPortSpin->value());
    if(srv && srv->isRunning())
    {
        if(!srv->start()) {
            QSettings().setValue(SETTINGS_SRV_ENABLED, false);
            ui->srvAddressLabel->clear();
        }
        ui->srvEnableButton->setEnabled(!srv->isRunning());
        ui->srvOpenButton->setEnabled(srv->isRunning());
    }
    if(srv && srv->isRunning()) {
        ui->srvAddressLabel->setText(QString("<a style=\"text-decoration:none\" href=\"http://localhost:%1\">"
                                             "http://localhost:%1</a>").arg(ui->srvPortSpin->value()));
    }
}

void HttpSetupDialog::on_srvPortRestore_clicked()
{
    ui->srvPortSpin->setValue(SETTINGS_HTTP_PORT_DEFAULT);
    on_srvPortApply_clicked();
}
