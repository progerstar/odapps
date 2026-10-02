#ifndef RFIDTABLEVIEW_H
#define RFIDTABLEVIEW_H

#include <QTableView>


class RawTableModelItemDelegate;

class RFIDTableView : public QTableView
{
        Q_OBJECT
    public:
        explicit RFIDTableView(QWidget *parent = 0);
    private slots:
        void customCloseEditor(QWidget * editor, int hint);
    private:
        RawTableModelItemDelegate* raw_delegate;

        void findNextEditableCell(int row, int col);
        void findPreviousEditableCell(int row, int col);
};

#endif // RFIDTABLEVIEW_H
