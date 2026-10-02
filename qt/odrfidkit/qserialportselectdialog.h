#ifndef QSERIALPORTSELECTDIALOG_H
#define QSERIALPORTSELECTDIALOG_H

#include <QDialog>

namespace Ui {
class QSerialPortSelectDialog;
}

class QSerialPortSelectDialog : public QDialog
{
        Q_OBJECT
        Q_PROPERTY(QString label READ label WRITE setLabel)
        Q_PROPERTY(QString item READ item WRITE setItem)
    public:
        explicit QSerialPortSelectDialog(QWidget *parent = nullptr);
        ~QSerialPortSelectDialog();

        QString label() const;
        void setLabel(const QString& value);

        int count() const;
        int currentIndex() const;
        QString item() const;
        void setItem(const QString& value);
    signals:
        void updateRequested();
    public slots:
        void updateSelection(const QStringList& items);
    private:
        Ui::QSerialPortSelectDialog *ui;
};

#endif // QSERIALPORTSELECTDIALOG_H
