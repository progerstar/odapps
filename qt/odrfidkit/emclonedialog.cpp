#include "emclonedialog.h"
#include "rfidkit_global.h"
#include <ui_emclonedialog.h>

#include <QDebug>
#include <QSettings>

EMCloneDialog::EMCloneDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::EMCloneDialog)
{
    ui->setupUi(this);
    ui->uidDisplay->setStyleSheet(QLatin1String("QLCDNumber{border:1px solid #4caf50;background-color: black;color: #4caf50;border-radius: 2px;padding: 2px;}"));
}

EMCloneDialog::~EMCloneDialog()
{
    delete ui;
}

void EMCloneDialog::readSettings()
{
    QSettings set;
    if(set.value(SETTINGS_KEEPPWD, false).toBool())
    {
        QString old = set.value(SETTINGS_EM_PASSWORD_OLD, QString()).toString();
        if(!old.isEmpty())
        {
            ui->oldPwdEdit->setText(old);
            ui->oldPwdCheck->setChecked(true);
        }
        QString newp = set.value(SETTINGS_EM_PASSWORD_NEW, QString()).toString();
        if(!newp.isEmpty())
        {
            ui->newPwdEdit->setText(newp);
            ui->newPwdCheck->setChecked(true);
        }
    }
}

void EMCloneDialog::writeSettings()
{
    QSettings set;
    if(set.value(SETTINGS_KEEPPWD, false).toBool())
    {
        set.setValue(SETTINGS_EM_PASSWORD_OLD, currentPassword());
        set.setValue(SETTINGS_EM_PASSWORD_NEW, newPassword());
    }
}

void EMCloneDialog::setUID(const QByteArray& uid)
{
    qWarning()<<"Displaying UID "<<QString::fromLatin1(uid.toHex());
    ui->uidDisplay->display(QString::fromLatin1(uid.toHex()));
}

EMCloneDialog::EmCoding EMCloneDialog::coding() const
{
    return ui->codingSwitch->isChecked() ? BiPhase : Manchester;
}

EMCloneDialog::EmSpeed EMCloneDialog::speed() const
{
    return ui->speedSwitch->isChecked() ? RF32 : RF64;
}

QString EMCloneDialog::currentPassword() const
{
    return ui->oldPwdCheck->isChecked() ? ui->oldPwdEdit->text() : QString();
}

QString EMCloneDialog::newPassword() const
{
    return ui->newPwdCheck->isChecked() ? ui->newPwdEdit->text() : QString();
}
