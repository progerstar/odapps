#include "uidchangedialog.h"
#include <ui_uidchangedialog.h>

UIDChangeDialog::UIDChangeDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::UIDChangeDialog)
{
    ui->setupUi(this);
    ui->acceptCheck->setChecked(false);
    ui->okButton->setEnabled(false);
}

UIDChangeDialog::~UIDChangeDialog()
{
    delete ui;
}

void UIDChangeDialog::setup(const QByteArray& uid)
{
    orig_uid = uid;
    ui->uidEdit->setInputMask(QString(uid.size()*2, QChar('H'))+QLatin1String(";."));
    ui->uidEdit->setText(QString::fromLatin1(uid.toHex().toUpper()));
}

QByteArray UIDChangeDialog::uid() const
{
    return QByteArray::fromHex(ui->uidEdit->text().toLatin1());
}

void UIDChangeDialog::on_acceptCheck_toggled(bool on)
{
    if(!on)
    {
        ui->okButton->setEnabled(false);
    }
    else
    {
        ui->okButton->setEnabled(checkText(ui->uidEdit->text()));
    }
}

void UIDChangeDialog::on_uidEdit_textChanged(const QString& text)
{
    ui->okButton->setEnabled(ui->acceptCheck->isChecked() && checkText(text));
}

bool UIDChangeDialog::checkText(const QString& text)
{
    QByteArray hex = QByteArray::fromHex(text.toLatin1());
    return (hex.size() == orig_uid.size()) && (hex != orig_uid);
}
