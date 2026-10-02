#include "qtexttoolbutton.h"

QTextToolButton::QTextToolButton(QWidget* parent) : QToolButton(parent)
{
    connect(this,SIGNAL(toggled(bool)),this,SLOT(mtoggled(bool)));
}

void QTextToolButton::setTexts(const QString& onText, const QString& offText)
{
    t_on = onText;
    t_off = offText;
    mtoggled(isChecked());
}

void QTextToolButton::mtoggled(bool on)
{
    if(on)
    {
        QToolButton::setText(t_on);
    }
    else
    {
        QToolButton::setText(t_off);
    }
}
