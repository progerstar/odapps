#ifndef QTEXTTOOLBUTTON_H
#define QTEXTTOOLBUTTON_H

#include <QToolButton>

class QTextToolButton : public QToolButton
{
        Q_OBJECT
    public:
        QTextToolButton(QWidget* parent = 0);

        void setText(const QString& /*text*/) {/*NOP*/}
        void setTexts(const QString& onText, const QString& offText);
    private slots:
        void mtoggled(bool on);
    private:
        QString t_on,t_off;
};

#endif // QTEXTTOOLBUTTON_H
