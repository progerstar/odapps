#include "qcoverwidget.h"

#include <QDebug>

QCoverWidget::QCoverWidget(QWidget *parent) :
    QWidget(parent,Qt::WindowStaysOnTopHint),scene(0)
{
    setVisible(false);
}

bool QCoverWidget::eventFilter(QObject *watched, QEvent *event)
{
    if(scene && (watched==scene) && (event->type()==QEvent::Resize))
    {
        QResizeEvent* rev = (QResizeEvent*)event;
        resize(rev->size());
    }
    return false;
}

void QCoverWidget::start(QWidget* bg)
{
    scene = bg;
    if(scene)
    {
        resize(scene->size());
        QWidget* m_parent = parentWidget();
        if(m_parent)
        {
            qDebug()<<"Scene "<<scene<<" coord (0,0) -> global "<<scene->mapToGlobal(QPoint(0,0));
            qDebug()<<"Resized "<<this<<" to "<<scene->size()<<", moving to "<<m_parent->mapFromGlobal(scene->mapToGlobal(QPoint(0,0)))
                    <<" in "<<m_parent<<" coords (0,0 in "<<scene<<" coords)";
            move(scene->mapTo(m_parent,QPoint(0,0)));
        }
        else
        {
            move(scene->mapToGlobal(QPoint(0,0)));
        }
        scene->installEventFilter(this);
    }
    setVisible(true);
}

void QCoverWidget::stop()
{
    setVisible(false);

    if(scene)
    {
        scene->removeEventFilter(this);
        scene = 0;
    }
}
