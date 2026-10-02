#include "rfidtableview.h"

#include "rawtablemodelitemdelegate.h"

RFIDTableView::RFIDTableView(QWidget *parent) : QTableView(parent),
    raw_delegate(new RawTableModelItemDelegate(this))
{
    connect(raw_delegate,SIGNAL(customCloseEditor(QWidget*,int)),
            this,SLOT(customCloseEditor(QWidget*,int)));
    setItemDelegate(raw_delegate);
}

void RFIDTableView::customCloseEditor(QWidget *editor, int hint)
{
    QModelIndex editedIndex = currentIndex();
    QTableView::closeEditor(editor,QAbstractItemDelegate::NoHint);
    switch(hint)
    {
        case RawTableModelItemDelegate::EditHint_Up:
        {
            findPreviousEditableCell(editedIndex.row()-1,editedIndex.column());
            break;
        }
        case RawTableModelItemDelegate::EditHint_Down:
        {
            findNextEditableCell(editedIndex.row()+1,editedIndex.column());
            break;
        }
        case RawTableModelItemDelegate::EditHint_Left:
        {
            findPreviousEditableCell(editedIndex.row(),editedIndex.column()-1);
            break;
        }
        case RawTableModelItemDelegate::EditHint_Right:
        case RawTableModelItemDelegate::EditHint_Next:
        {
            findNextEditableCell(editedIndex.row(),editedIndex.column()+1);
            break;
        }
        default:
            setCurrentIndex(editedIndex);
            break;
    }
}

void RFIDTableView::findNextEditableCell(int row, int col)
{
    if(!model())
        return;

    const QAbstractItemModel* m_model = model();

    while(row<m_model->rowCount())
    {
        while( (col<m_model->columnCount()) &&
               !(m_model->flags(m_model->index(row,col)) & Qt::ItemIsEditable))
        {
            ++col;
        }
        if(col==m_model->columnCount())
        {
            ++row;
            col = 1;
        }
        else
        {
            setCurrentIndex(m_model->index(row,col));
            return;
        }
    }
}

void RFIDTableView::findPreviousEditableCell(int row, int col)
{
    if(!model())
        return;

    const QAbstractItemModel* m_model = model();

    while(row>=0)
    {
        while( (col>0) &&
               !(m_model->flags(m_model->index(row,col)) & Qt::ItemIsEditable))
        {
            --col;
        }
        if(col==0)
        {
            --row;
            col = m_model->columnCount()-1;
        }
        else
        {
            setCurrentIndex(m_model->index(row,col));
            return;
        }
    }
}
