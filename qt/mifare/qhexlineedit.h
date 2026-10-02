#ifndef QHEXLINEEDIT_H
#define QHEXLINEEDIT_H

#include <QLineEdit>
#include <QMouseEvent>

class QHexLineEdit : public QLineEdit
{
        Q_OBJECT
    public:
        explicit QHexLineEdit(QWidget *parent = 0);

        /*number of digits in the number-> twice when displayed*/
        void setSize(int count);
        void setDefaultValue(quint8 value);

        void setInputMask(const QString&) {/*NOP*/}
        void setValidator(const QValidator*) {/*NOP*/}
        void setFont(const QFont&) {/*NOP*/}
    signals:

    public slots:

    protected:
        virtual void keyPressEvent(QKeyEvent* e) Q_DECL_OVERRIDE;
        virtual bool event(QEvent *e) Q_DECL_OVERRIDE;
    private:
        int n_size;
        quint8 d_val;

        inline QChar defaultChar() const
        {
            return  ( (d_val&0xF) < 10) ? ('0'+(d_val&0xF)) : ('A' + (d_val&0xF)-10);
        }

        void setup_int();

};

#endif // QHEXLINEEDIT_H
