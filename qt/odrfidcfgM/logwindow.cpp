#include "logwindow.h"
#include <ui_logwindow.h>
#include <qmaterialfont.h>

#include <QDateTime>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QFileDialog>
#include <QStringBuilder>
#include <QMessageBox>

LogWindow::LogWindow(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::LogWindow)
{
    ui->setupUi(this);
    MaterialUI("clear_all", ui->clearButton);
    MaterialUI("save", ui->exportButton);
    on_clearButton_clicked();
}

LogWindow::~LogWindow()
{
    delete ui;
}

void LogWindow::append(const QString& direction, const QString& data)
{
    int row = ui->logTable->rowCount();
    ui->logTable->setRowCount(row+1);
    ui->logTable->setItem(row, 0, new QTableWidgetItem(QDateTime::currentDateTime().toString("yyyy.MM.dd hh:mm:ss")));
    ui->logTable->setItem(row, 1, new QTableWidgetItem(direction));
    ui->logTable->setItem(row, 2, new QTableWidgetItem(data));
    ui->exportButton->setEnabled(true);
}

void LogWindow::on_clearButton_clicked()
{
    ui->logTable->clear();
    ui->logTable->setColumnCount(3);
    ui->logTable->setHorizontalHeaderLabels({tr("Time"), QString(), tr("Data")});
    ui->logTable->horizontalHeader()->setStretchLastSection(true);
    ui->exportButton->setEnabled(false);
}

void LogWindow::on_exportButton_clicked()
{
    QString filename = QFileDialog::getSaveFileName(this, tr("Save As"));
    if(!filename.isEmpty())
    {
        QFile ofd(filename);
        if(ofd.open(QFile::WriteOnly))
        {
            ofd.write("#Time\tDirection\tData\n");
            int rows = ui->logTable->rowCount();
            for(int i=0;i<rows;++i)
            {
                ofd.write((ui->logTable->item(i, 0)->text() % QLatin1String("\t") %
                           ui->logTable->item(i, 1)->text() % QLatin1String("\t") %
                           ui->logTable->item(i, 2)->text() % QLatin1String("\n")).toUtf8());
            }
            ofd.close();
        }
        else
        {
            QMessageBox::warning(this, tr("Save Log"), tr("Cannot write to %1: %2").arg(filename, ofd.errorString()));
        }
    }
}
