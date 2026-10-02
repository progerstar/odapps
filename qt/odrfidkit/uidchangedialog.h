#ifndef UIDCHANGEDIALOG_H
#define UIDCHANGEDIALOG_H

#include <QDialog>

namespace Ui {
class UIDChangeDialog;
}

class UIDChangeDialog : public QDialog
{
        Q_OBJECT

    public:
        explicit UIDChangeDialog(QWidget *parent = nullptr);
        ~UIDChangeDialog();

        void setup(const QByteArray& uid);

        QByteArray uid() const;

    private slots:
        void on_acceptCheck_toggled(bool on);
        void on_uidEdit_textChanged(const QString& text);
    private:
        QByteArray orig_uid;
        Ui::UIDChangeDialog *ui;

        bool checkText(const QString& text);
};

#endif // UIDCHANGEDIALOG_H
