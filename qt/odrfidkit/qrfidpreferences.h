#ifndef QDFUPREFERENCES_H
#define QDFUPREFERENCES_H

#include <QDialog>

namespace Ui {
class QRFIDPreferences;
}

class QRFIDPreferences : public QDialog
{
        Q_OBJECT

    public:
        explicit QRFIDPreferences(QWidget *parent = nullptr);
        ~QRFIDPreferences();
    private slots:
        void on_tcpHelpTool_clicked();
        void on_udpHelpTool_clicked();
        void on_keydbDropTool_clicked();
        void writeSettings();
    private:
        Ui::QRFIDPreferences *ui;

        void readSettings();
};

#endif // QDFUPREFERENCES_H
