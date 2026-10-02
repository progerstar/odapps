#include "rawtablemodelitemdelegate.h"

#include <QEvent>
#include <QLineEdit>
#include <QKeyEvent>

QWidget* RawTableModelItemDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem& /*option*/,
                                                 const QModelIndex& /*index*/) const
{
    //QTableLineEdit *editor = new QTableLineEdit(parent);
    QLineEdit* editor = new QLineEdit(parent);
    editor->setFrame(false);
    editor->setAlignment(Qt::AlignCenter);
    QPalette palette;
    palette.setBrush(QPalette::Base,Qt::green);
    palette.setBrush(QPalette::Text,Qt::black);
    editor->setPalette(palette);
    editor->installEventFilter(const_cast<RawTableModelItemDelegate*>(this));
    return editor;
}

void RawTableModelItemDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    (static_cast<QLineEdit*>(editor))->setText(index.model()->data(index, Qt::EditRole).toString());
}

void RawTableModelItemDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                             const QModelIndex &index) const
{
    QString text = (static_cast<QLineEdit*>(editor))->text();
    model->setData(index, text, Qt::EditRole);
}

bool RawTableModelItemDelegate::eventFilter ( QObject * obj, QEvent * event )
{
    QLineEdit* editor = qobject_cast<QLineEdit*>(obj);
    if(!editor)
        return QStyledItemDelegate::eventFilter(obj, event);

    if (event->type() == QEvent::KeyPress)
    {
        QKeyEvent *keyEvent = (QKeyEvent*)event;
        CustomEditHint chint = EditHint_None;
        switch(keyEvent->key())
        {
            case Qt::Key_Return:
            case Qt::Key_Enter:
            case Qt::Key_Tab:   chint = EditHint_Next;break;
            case Qt::Key_Down:  chint = EditHint_Down;break;
            case Qt::Key_Left:
            {
                if(editor->text().isEmpty() || (editor->cursorPosition()==0))
                {
                    chint = EditHint_Left;
                    break;
                }
                return QStyledItemDelegate::eventFilter(obj, event);
            }
            case Qt::Key_Right:
            {
                if(editor->text().isEmpty() || (editor->cursorPosition() >= editor->text().size()))
                {
                    chint = EditHint_Right;
                    break;
                }
                return QStyledItemDelegate::eventFilter(obj, event);
            }
            case Qt::Key_Up:    chint = EditHint_Up;break;
            default:
                return QStyledItemDelegate::eventFilter(obj, event);
        }

        emit commitData((QWidget*)editor);
        //closeEditor((QWidget*)editor, QAbstractItemDelegate::EditNextItem);
        emit customCloseEditor((QWidget*)editor,chint);
        return true;
    }
    return QStyledItemDelegate::eventFilter(obj, event);
}
