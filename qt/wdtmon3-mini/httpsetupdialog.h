#ifndef HTTPSETUPDIALOG_H
#define HTTPSETUPDIALOG_H

#include <QDialog>

class QMUHttpServer;

namespace Ui {
class HttpSetupDialog;
}

class HttpSetupDialog : public QDialog
{
        Q_OBJECT

    public:
        explicit HttpSetupDialog(QWidget *parent = 0);
        ~HttpSetupDialog();

        void setup(QMUHttpServer* server);
    private slots:
        void on_srvEnableButton_clicked();
        void on_srvOpenButton_clicked();
        void on_srvPortApply_clicked();
        void on_srvPortRestore_clicked();
    private:
        Ui::HttpSetupDialog *ui;
        QMUHttpServer* srv;
};

#endif // HTTPSETUPDIALOG_H
