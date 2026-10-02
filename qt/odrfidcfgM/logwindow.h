#ifndef LOGWINDOW_H
#define LOGWINDOW_H

#include <QWidget>
#include <QCloseEvent>

namespace Ui {
class LogWindow;
}

class LogWindow : public QWidget
{
        Q_OBJECT

    public:
        explicit LogWindow(QWidget *parent = nullptr);
        ~LogWindow();

    public slots:
        void append(const QString& direction, const QString& data);
    protected:
        virtual void closeEvent(QCloseEvent* e) override
        {
            hide();
            e->ignore();
        }
    private slots:
        void on_clearButton_clicked();
        void on_exportButton_clicked();
    private:
        Ui::LogWindow *ui;
};

#endif // LOGWINDOW_H
