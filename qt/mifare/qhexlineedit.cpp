#include "qhexlineedit.h"

#include <QKeyEvent>
#include <QRegExpValidator>
#include <QClipboard>
#include <QApplication>
#include <QFontDatabase>
#include <QEvent>

QHexLineEdit::QHexLineEdit(QWidget *parent) : QLineEdit(parent), n_size(4), d_val(0xFF)
{
    setSize(4);
    setDefaultValue(0xFF);
    QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
#ifdef Q_OS_DARWIN
    monoFont.setPointSizeF(monoFont.pointSizeF()+1);
#endif
    QLineEdit::setFont(monoFont);
    setAlignment(Qt::AlignCenter);
}

void QHexLineEdit::setSize(int count)
{
    n_size = count;
    setup_int();
}

void QHexLineEdit::setDefaultValue(quint8 value)
{
    d_val = value;
    setup_int();
}

void QHexLineEdit::keyPressEvent(QKeyEvent *e)
{
    int key = e->key();
    int pos = hasSelectedText() ? selectionStart() : cursorPosition();

    //qDebug()<<"QHexLineEdit::keyPressed key "<<key<<" mods "<<e->modifiers()<<" text "<<e->text();

    if(isReadOnly())
    {
        e->ignore();
        return;
    }

    if((key==Qt::Key_Left)||(key==Qt::Key_Right)||(key==Qt::Key_Home)||(key==Qt::Key_End)||
            ((key==Qt::Key_C)&&(e->modifiers()&Qt::ControlModifier)))
    {
        QLineEdit::keyPressEvent(e);
        return;
    }

    //qDebug()<<"QHexLineEdit::keyPressed pos "<<pos<<" ctext "<<text();
    if((pos>0)&&(key==Qt::Key_Backspace))
    {
        setText(text().replace(--pos,1,defaultChar()));
        setCursorPosition(pos);
    }
    else if((pos<2*n_size)&&(key==Qt::Key_Delete))
    {
        setText(text().replace(pos,1,defaultChar()));
        setCursorPosition(++pos);
    }
    else if(pos<2*n_size)
    {
        if((key>=Qt::Key_0)&&(key<=Qt::Key_9)&&!(e->modifiers()&~quint32(Qt::KeypadModifier)))
        {
            setText(text().replace(pos,1,QChar('0'+key-Qt::Key_0)));
            setCursorPosition(++pos);
        }
        else if((key>=Qt::Key_A)&&(key<=Qt::Key_F)&&(e->modifiers()==Qt::NoModifier))
        {
            setText(text().replace(pos,1,QChar('A'+key-Qt::Key_A)));
            setCursorPosition(++pos);
        }
        else if((key==Qt::Key_V)&&(e->modifiers()&Qt::ControlModifier))
        {
            //handle paste
            QString ctext = qApp->clipboard()->text().toUpper();
            if(!ctext.isEmpty() && QRegExp("[A-Fa-f0-9]+").exactMatch(ctext))
            {
                if(selectedText().isEmpty())
                {
                    //paste from cursor till the end
                    int size = qMin(2*n_size-pos,ctext.size());
                    setText(text().replace(pos,size,ctext.left(size)));
                }
                else
                {
                    int size = qMin(selectedText().size(),ctext.size());
                    setText(text().replace(pos,size,ctext.left(size)));
                }
            }
            else
            {
                e->ignore();
                return;
            }
        }
        else
        {
            e->ignore();
            return;
        }
    }
    else
    {
        e->ignore();
        return;
    }

    e->accept();
    emit textEdited(text());
    return;
}

bool QHexLineEdit::event(QEvent *e)
{
    //forbid global selection paste
    if((e->type()==QEvent::InputMethod)||(e->type()==QEvent::InputMethodQuery))
    {
        return false;
    }
    if( ((e->type()==QEvent::MouseButtonPress)||(e->type()==QEvent::MouseButtonRelease))&&(((QMouseEvent*)e)->button()==Qt::MiddleButton))
    {
            return false;
    }
    return QLineEdit::event(e);
}

void QHexLineEdit::setup_int()
{
    QString ntext;
    for(int i=0;i<n_size;++i)
    {
        ntext += QString("%1").arg(d_val,2,16,QLatin1Char('0'));
    }
    setText(ntext.toUpper());
}
