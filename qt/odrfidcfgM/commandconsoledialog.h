#ifndef COMMANDCONSOLEDIALOG_H
#define COMMANDCONSOLEDIALOG_H

#include <QDialog>
#include <QTimer>
#include <QHideEvent>

namespace Ui {
class CommandConsoleDialog;
}

class CommandConsoleDialog : public QDialog
{
        Q_OBJECT

    public:
        explicit CommandConsoleDialog(QWidget *parent = nullptr);
        ~CommandConsoleDialog();

    signals:
        void commit(const QString& data);
    public slots:
        void display(const QString& reply);
    protected:
        virtual void hideEvent(QHideEvent* evt) override;
    private slots:
        void on_closeTool_clicked();
        void on_sendTool_clicked();
        void on_clearTool_clicked();
        void on_pollCheck_toggled(bool on);
    private:
        Ui::CommandConsoleDialog *ui;
        QTimer pollTimer;
};

#endif // COMMANDCONSOLEDIALOG_H
