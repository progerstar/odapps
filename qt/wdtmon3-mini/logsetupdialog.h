#ifndef LOGSETUPDIALOG_H
#define LOGSETUPDIALOG_H

#include <QDialog>

namespace Ui {
class LogSetupDialog;
}

class QJournal;

class LogSetupDialog : public QDialog
{
        Q_OBJECT

    public:
        explicit LogSetupDialog(QWidget *parent = 0);
        ~LogSetupDialog();

        void write(QJournal* journal);
    private:
        Ui::LogSetupDialog *ui;
};

#endif // LOGSETUPDIALOG_H
