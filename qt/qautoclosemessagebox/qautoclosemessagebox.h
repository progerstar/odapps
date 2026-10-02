#ifndef QAUTOCLOSEMESSAGEBOX_H_
#define QAUTOCLOSEMESSAGEBOX_H_

#include <QMessageBox>
#include <QTimer>

class QAutoCloseMessageBox : public QMessageBox
{
        Q_PROPERTY(int timeout READ timeout WRITE setTimeout NOTIFY timeoutChanged)
        Q_OBJECT
    public:
        /*Convenience*/
        /*  v1 */
        static QMessageBox::StandardButton critical(QWidget* parent, const QString& text, int timeout = 10, const QString& title = QString(),
                                                    QMessageBox::StandardButtons buttons = QMessageBox::Ok, QMessageBox::StandardButton defaultButton = NoButton);
        static QMessageBox::StandardButton information(QWidget* parent, const QString& text, int timeout = 10, const QString& title = QString(),
                                                       QMessageBox::StandardButtons buttons = QMessageBox::Ok, QMessageBox::StandardButton defaultButton = NoButton);
        static QMessageBox::StandardButton question(QWidget* parent, const QString& text, int timeout = 10, const QString& title = QString(),
                                                    QMessageBox::StandardButtons buttons = QMessageBox::Yes|QMessageBox::No,
                                                    QMessageBox::StandardButton defaultButton = NoButton);
        static QMessageBox::StandardButton warning(QWidget* parent, const QString& text, int timeout = 10, const QString& title = QString(),
                                                   QMessageBox::StandardButtons buttons = QMessageBox::Ok, QMessageBox::StandardButton defaultButton = NoButton);

        /* v2 */
        static QMessageBox::StandardButton critical(int timeout, QWidget *parent, const QString &title, const QString &text,
                                                    QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                                    QMessageBox::StandardButton defaultButton = NoButton);
        static QMessageBox::StandardButton information(int timeout, QWidget *parent, const QString &title, const QString &text,
                                                       QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                                       QMessageBox::StandardButton defaultButton = NoButton);
        static QMessageBox::StandardButton question(int timeout, QWidget *parent, const QString &title, const QString &text,
                                                    QMessageBox::StandardButtons buttons = QMessageBox::Yes|QMessageBox::No,
                                                    QMessageBox::StandardButton defaultButton = NoButton);
        static QMessageBox::StandardButton warning(int timeout, QWidget *parent, const QString &title, const QString &text,
                                                   QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                                   QMessageBox::StandardButton defaultButton = NoButton);

        /*Compatibility*/
        static QMessageBox::StandardButton critical(QWidget *parent, const QString &title, const QString &text,
                                                    QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                                    QMessageBox::StandardButton defaultButton = NoButton, int timeout = 0);
        static QMessageBox::StandardButton information(QWidget *parent, const QString &title, const QString &text,
                                                       QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                                       QMessageBox::StandardButton defaultButton = NoButton, int timeout = 0);
        static QMessageBox::StandardButton question(QWidget *parent, const QString &title, const QString &text,
                                                    QMessageBox::StandardButtons buttons = QMessageBox::Yes|QMessageBox::No,
                                                    QMessageBox::StandardButton defaultButton = NoButton, int timeout = 0);
        static QMessageBox::StandardButton warning(QWidget *parent, const QString &title, const QString &text,
                                                   QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                                   QMessageBox::StandardButton defaultButton = NoButton, int timeout = 0);

        /*Helper function (from qmessagebox.cpp -> QMessageBoxPrivate class)*/
        static QMessageBox::StandardButton showNewMessageBox(QWidget *parent,
                        QMessageBox::Icon icon, const QString& title, const QString& text,
                        QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton, int timeout);

        QAutoCloseMessageBox(QWidget* parent = 0);
        QAutoCloseMessageBox(QMessageBox::Icon icon, const QString &title, const QString &text, QMessageBox::StandardButtons buttons, QWidget *parent = nullptr, Qt::WindowFlags f = Qt::Dialog | Qt::MSWindowsFixedSizeDialogHint);
        virtual ~QAutoCloseMessageBox(){}

        inline int timeout() const {
            return qam_timeout;
        }

        inline void setTimeout(int value) {
            if(qam_timeout != value) {
                qam_timeout = value;
                qam_ticker = qam_timeout;
                emit timeoutChanged(value);
            }
        }

    signals:
        void timeoutChanged(int value);

    public slots:
        virtual int exec();
        virtual void show();

    protected slots:
        virtual void d_timeout();
        virtual void d_finished(int result);
    protected:
        QTimer qam_timer;
        int qam_timeout;
        int qam_ticker;
        QString qam_default_button_text;

        void timeoutHelper();
};

#endif  // QAUTOCLOSEMESSAGEBOX_H_
