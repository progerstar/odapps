#include "qnumberedit.h"
#include <QHash>
#include <QHBoxLayout>
#include <QDebug>
#include <QStringBuilder>

#include <qmaterialfont.h>

static const QHash<int,QString> basePrefixes = {
    {QNumberEdit::Bin, "0b"},
    {QNumberEdit::Oct, "0"},
    {QNumberEdit::Dec, QString()},
    {QNumberEdit::Hex, "0x"}
};

QNumberEdit::QNumberEdit(QWidget *parent) : QFrame(parent),
    _mod(Bin), _min(0), _max(99), _value(0), prefix(new QLabel(this)), _input(new QLineEdit(this))
{
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Plain);

    prefix->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    prefix->setAlignment(Qt::AlignVCenter|Qt::AlignRight);
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setSpacing(0);
    layout->setMargin(0);

    {
        QFont f = prefix->font();
        f.setBold(true);
        prefix->setFont(f);
    }

    layout->addWidget(prefix);
    layout->addWidget(_input);

    _input->installEventFilter(this);
    connect(_input, &QLineEdit::textEdited, this, &QNumberEdit::inputChanged);
    setMode(Dec);
    setupUI(true);
}

void QNumberEdit::setMinimum(qint64 min)
{
    if(_value<min)
    {
        setValue(min);
    }
    _min = min;
}

void QNumberEdit::setMaximum(qint64 max)
{
    if(_value>max)
    {
        setValue(max);
    }
    _max = max;
}

void QNumberEdit::setMode(Mode m)
{
    if(_mod != m)
    {
        _mod = m;
        prefix->setText(basePrefixes.value(int(_mod)));
        prefix->setHidden(prefix->text().isEmpty());
        setValue(_value);
        emit modeChanged(m);
    }
}

void QNumberEdit::setValue(qint64 v)
{
    qWarning()<<"QNumberEdit: request set to "<<v;
    v = qMin<qint64>(_max,qMax<qint64>(_min,v));
    qWarning()<<"  corrected to "<<v;
    //QLineEdit::setText(basePrefixes.value(int(_mod))+QString::number(v,int(_mod)));
    _input->setText(QString::number(v,int(_mod)));
    if(v != _value)
    {
        _value = v;
        emit valueChanged(_value);
    }
    setupUI(true);
}

bool QNumberEdit::eventFilter(QObject* obj, QEvent* ev)
{
    if((obj == _input) && (ev->type() == QEvent::KeyPress))
    {
        QKeyEvent* e = static_cast<QKeyEvent*>(ev);
        switch(e->key())
        {
            case Qt::Key_Up:
            {
                if(_value < _max)
                {
                    setValue(_value+1);
                    return true;
                }
                break;
            }
            case Qt::Key_Down:
            {
                if(_value > _min)
                {
                    setValue(_value-1);
                    return true;
                }
                break;
            }
#if 0
            case Qt::Key_Return:
            {
                inputEditingFinished();
                return true;
            }
#endif
            default:break;
        }
    }
    return QWidget::eventFilter(obj, ev);
}

void QNumberEdit::inputChanged(const QString& text)
{
    inputConvert(text);
}

void QNumberEdit::inputEditingFinished()
{
    if(!inputConvert(_input->text()))
    {
        setValue(_value);
    }
}

bool QNumberEdit::inputConvert(const QString& text)
{
    bool ok = false;
#if 0
    if(basePrefixes.value(int(_mod)).isEmpty() || text.startsWith(basePrefixes.value(int(_mod))))
#endif
    {
        //qint64 val = text.mid(basePrefixes.value(int(_mod)).size()).toLongLong(&ok, int(_mod));
        qint64 val = text.toLongLong(&ok, int(_mod));
        if(ok && (val>=_min) && (val<=_max))
        {
            if(val != _value)
            {
                _value = val;
                emit valueChanged(val);
            }
            setupUI(true);
        }
        else
        {
            setupUI(false);
        }
    }
    return ok;
}

void QNumberEdit::setupUI(bool correct)
{
    QPalette app_default;

#if 0
    setStyleSheet(QString(".QNumberEdit{border: 1px solid %1; border-radius: 1px; padding: 2px; background-color: %3}"
                          ".QLabel{border:none; border-right: 1px solid %2; background-color: %3}"
                          ".QLineEdit{border:none;}")
                  .arg(correct ? app_default.shadow().color().name() : "#ff0000").arg(app_default.shadow().color().name())
                  .arg(app_default.base().color().name()));
                  //.arg(correct ? (dark ? "#bbbbbb" : "#333333") : "#ff0000").arg(dark ? "#bbbbbb" : "#333333"));
#elif 1
    setStyleSheet(QString(".QNumberEdit{border: 1px solid %1; border-radius: 2px; padding: 2px; background-color: %3}"
                          ".QLabel{border:none; border-right: 1px solid %1; background-color: %3}"
                          ".QLineEdit{border:none;}")
                  .arg(MaterialPaint(correct ? QMaterialIcon::Success : QMaterialIcon::Error).name()) /* Border */
                  .arg(app_default.base().color().name()) /* Base */);
#else
    app_default.setColor(QPalette::Active, QPalette::Highlight, MaterialPaint(correct ? QMaterialIcon::Success : QMaterialIcon::Error));
    setPalette(app_default);
#endif

}
