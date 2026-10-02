#ifndef EMCLONEDIALOG_H
#define EMCLONEDIALOG_H

#include <QDialog>

namespace Ui {
class EMCloneDialog;
}

class EMCloneDialog : public QDialog
{
        Q_OBJECT

    public:
        enum EmCoding {
            Manchester = 0,
            BiPhase = 1
        };

        enum EmSpeed {
            RF64 = 0,
            RF32 = 1,
        };

        explicit EMCloneDialog(QWidget *parent = nullptr);
        ~EMCloneDialog();

        void readSettings();
        void writeSettings();

        void setUID(const QByteArray& uid);

        EmCoding coding() const;
        EmSpeed speed() const;
        QString currentPassword() const;
        QString newPassword() const;
    private:
        Ui::EMCloneDialog *ui;
};

#endif // EMCLONEDIALOG_H
