#ifndef CONNECTIONPOPUP_H
#define CONNECTIONPOPUP_H

#include <QFrame>
#include <QCloseEvent>

namespace Ui {
class ConnectionPopup;
}

typedef QList<QPair<QString, QVariant> > PortList;

class ConnectionPopup : public QFrame
{
        Q_OBJECT
    public:
        explicit ConnectionPopup(QWidget *parent = nullptr);
        ~ConnectionPopup();

        void readSettings();
        void writeSettings();

        inline bool accepted() const { return _accepted; }
        QString text() const;
        QVariant userData() const;

        void setupPorts(const PortList& p);

    signals:
        void updateRequested();
        void done();

    protected:
        void closeEvent(QCloseEvent* e) override {
            QFrame::closeEvent(e);
            emit done();
        }

    private slots:
        void on_applyButton_clicked();
        void enumerateDevices();
    private:
        Ui::ConnectionPopup *ui;
        bool _accepted;
};

#endif // CONNECTIONPOPUP_H
