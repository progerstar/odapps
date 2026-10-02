#ifndef QPROGRESSDISPLAY_H
#define QPROGRESSDISPLAY_H

#include <qcoverwidget.h>

#include <QPaintEvent>

class QProgressDisplay : public QCoverWidget
{
        Q_OBJECT
    public:
        QProgressDisplay(QWidget *parent = 0);
        virtual ~QProgressDisplay(){}

        void setText(const QString& text);

    protected:
        virtual void paintEvent(QPaintEvent* e) Q_DECL_OVERRIDE;

    private:
        QString _text;
};

#endif // QPROGRESSDISPLAY_H
