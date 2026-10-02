#include "logsetupdialog.h"
#include <ui_logsetupdialog.h>

#include <QSettings>
#include "wdtmonlite.h"
#include <qjournald.h>

LogSetupDialog::LogSetupDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::LogSetupDialog)
{
    ui->setupUi(this);

    QSettings set;
    ui->logSizeSpin->setValue(set.value(SETTINGS_LOG_SIZE,64).toInt());
    ui->logBkpSpin->setValue(set.value(SETTINGS_LOG_CNT,3).toInt());
}

LogSetupDialog::~LogSetupDialog()
{
    delete ui;
}

void LogSetupDialog::write(QJournal* journal)
{
    QSettings set;
    set.setValue(SETTINGS_LOG_SIZE,ui->logSizeSpin->value());
    set.setValue(SETTINGS_LOG_CNT,ui->logBkpSpin->value());
    journal->setMaxSizeMB(ui->logSizeSpin->value());
    journal->setBackupCount(ui->logBkpSpin->value());
}
