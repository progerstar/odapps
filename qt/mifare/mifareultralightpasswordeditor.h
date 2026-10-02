#ifndef MIFAREULTRALIGHTPASSWORDEDITOR_H
#define MIFAREULTRALIGHTPASSWORDEDITOR_H

#include <QDialog>
#include <mifareblock.h>

namespace Ui {
class MifareUltralightPasswordEditor;
}

class MifareSectorInterface;

class MifareUltralightPasswordEditor : public QDialog
{
        Q_OBJECT

    public:
        explicit MifareUltralightPasswordEditor(QWidget *parent = nullptr);
        ~MifareUltralightPasswordEditor();

        void setup(quint32 pwd, quint16 pack);
        void apply(MifareSectorInterface* card);
        quint32 password() const;
        quint16 pack() const;
    private slots:
        void pwdBytes_valueChanged(int);
        void on_pwdEdit_valueChanged(qint64 v);
        void packBytes_valueChanged(int);
        void on_packEdit_valueChanged(qint64 v);
    private:
        Ui::MifareUltralightPasswordEditor *ui;
};

#endif // MIFAREULTRALIGHTPASSWORDEDITOR_H
