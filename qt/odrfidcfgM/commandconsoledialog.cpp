#include "commandconsoledialog.h"
#include <ui_commandconsoledialog.h>
#include <qmaterialfont.h>

CommandConsoleDialog::CommandConsoleDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::CommandConsoleDialog), pollTimer(this)
{
    ui->setupUi(this);
    pollTimer.setInterval(500);
    setWindowIcon(MaterialIcon("developer_mode"));
    MaterialUI("backspace", ui->clearTool);
    MaterialUI("send", ui->sendTool);
    MaterialUI("close", ui->closeTool);
    connect(ui->inputField, &QLineEdit::returnPressed, this, &CommandConsoleDialog::on_sendTool_clicked);
    connect(&pollTimer, &QTimer::timeout, this, [=] {
        emit this->commit(QString());
    });
}

CommandConsoleDialog::~CommandConsoleDialog()
{
    delete ui;
}

void CommandConsoleDialog::display(const QString& reply)
{
    ui->logBrowser->append(reply);
}

void CommandConsoleDialog::hideEvent(QHideEvent* evt)
{
    if(!evt->spontaneous())
    {
        pollTimer.stop();
        ui->pollCheck->setChecked(false);
    }
    QDialog::hideEvent(evt);
}

void CommandConsoleDialog::on_closeTool_clicked()
{
    this->hide();
}

void CommandConsoleDialog::on_sendTool_clicked()
{
    emit commit(ui->inputField->text());
    ui->inputField->clear();
}

void CommandConsoleDialog::on_clearTool_clicked()
{
    ui->inputField->clear();
}

void CommandConsoleDialog::on_pollCheck_toggled(bool on)
{
    if(on && !this->isHidden())
    {
        pollTimer.start();
        emit commit(QString());
    }
    else
    {
        pollTimer.stop();
    }
}
