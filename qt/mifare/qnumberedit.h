#ifndef QNUMBEREDIT_H
#define QNUMBEREDIT_H

#include <QWidget>
#include <QLineEdit>
#include <QKeyEvent>
#include <QLabel>

class QNumberEdit : public QFrame
{
        Q_OBJECT
        Q_PROPERTY(qint64 minimum READ getMinimum WRITE setMinimum)
        Q_PROPERTY(qint64 maximum READ getMaximum WRITE setMaximum)
        Q_PROPERTY(Mode mode READ getMode WRITE setMode NOTIFY modeChanged)
    public:
        enum Mode {
            Bin = 2,
            Oct = 8,
            Dec = 10,
            Hex = 16
        };
        Q_ENUM(Mode)

        explicit QNumberEdit(QWidget *parent = nullptr);

        qint64 getMinimum() const
        {
            return _min;
        }
        void setMinimum(qint64 min);

        qint64 getMaximum() const
        {
            return _max;
        }
        void setMaximum(qint64 max);

        inline void setRange(qint64 min, qint64 max)
        {
            setMinimum(min);
            setMaximum(max);
        }

        inline Mode getMode() const
        {
            return _mod;
        }

        void setMode(Mode m);

        inline qint64 value() const
        {
            return _value;
        }
    signals:
        void valueChanged(qint64 v);
        void modeChanged(Mode m);
    public slots:
        void setValue(qint64 v);
    protected:
        virtual bool eventFilter(QObject* obj, QEvent* ev) Q_DECL_OVERRIDE;
        //virtual void keyPressEvent(QKeyEvent* e) Q_DECL_OVERRIDE;
    private slots:
        void inputChanged(const QString& text);
        void inputEditingFinished();
    private:
        Mode _mod;
        qint64 _min, _max;
        qint64 _value;

        QLabel* prefix;
        QLineEdit* _input;

        bool inputConvert(const QString& text);
        void setupUI(bool correct);
};

#endif // QNUMBEREDIT_H
