#include "qserialportselectdialog.h"
#include <ui_qserialportselectdialog.h>

#include <qmaterialfont.h>
#include <themedetector.h>

QSerialPortSelectDialog::QSerialPortSelectDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::QSerialPortSelectDialog)
{
    ui->setupUi(this);

    MaterialUI("cached", ui->updateTool, ThemeDetector::iconColor());
    connect(ui->updateTool, &QToolButton::clicked, this, &QSerialPortSelectDialog::updateRequested);
}

QSerialPortSelectDialog::~QSerialPortSelectDialog()
{
    delete ui;
}

QString QSerialPortSelectDialog::label() const
{
    return ui->label->text();
}

void QSerialPortSelectDialog::setLabel(const QString& value)
{
    ui->label->setText(value);
}

int QSerialPortSelectDialog::count() const
{
    return ui->selectorCombo->count();
}

int QSerialPortSelectDialog::currentIndex() const
{
    return ui->selectorCombo->currentIndex();
}

QString QSerialPortSelectDialog::item() const
{
    return ui->selectorCombo->currentText();
}

void QSerialPortSelectDialog::setItem(const QString& value)
{
    ui->selectorCombo->setCurrentText(value);
}

void QSerialPortSelectDialog::updateSelection(const QStringList& items)
{
    ui->selectorCombo->clear();
    ui->selectorCombo->addItems(items);
}
