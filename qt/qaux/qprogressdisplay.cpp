#include "qprogressdisplay.h"

#include <QPainter>

QProgressDisplay::QProgressDisplay(QWidget* parent) : QCoverWidget(parent),
    _text("")
{

}

void QProgressDisplay::setText(const QString &text)
{
    _text = text;
    repaint();
}

void QProgressDisplay::paintEvent(QPaintEvent *e)
{
    Q_UNUSED(e)

    qreal rx = width()/2., ry = height()/2.;
    qreal r = qMin(rx,ry);

    QPainter p(this);

    p.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing);
    p.translate(rx,ry);

    p.setPen(QPen(Qt::black,1.0));
    p.setBrush(QColor(0,0,0,120));
    p.drawRoundedRect(QRectF(-rx,-ry,rx*2,ry*2),2,2);

    if(!_text.isEmpty())
    {
        qreal tw = 0.8*r, th = 0.4*r;
        qreal factor = tw / p.fontMetrics().boundingRect(QRect(0,0,tw,th), Qt::AlignCenter|Qt::TextWordWrap,_text).width();
        if ((factor < 1) || (factor > 1.25))
        {
            QFont f = p.font();
            f.setPointSizeF(f.pointSizeF()*factor);
            p.setFont(f);
        }
        QRect br = p.fontMetrics().boundingRect(QRect(0,0,tw,th), Qt::AlignCenter|Qt::TextWordWrap,_text);
        br.setWidth(br.width()+4);
        br.setHeight(br.height()+4);
        br.moveCenter(QPoint(0,0));
        p.setPen(QPen(Qt::black,2.0));
        p.setBrush(QColor(255,255,255,130));
        p.drawRoundedRect(br,5,5);
        p.setBrush(Qt::black);
        p.drawText(br,Qt::AlignCenter|Qt::TextWordWrap,_text);
    }
}
