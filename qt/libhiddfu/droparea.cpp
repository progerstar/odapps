#include "droparea.h"

#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QUrl>
#include <QDebug>

#include <QFileDialog>
#include <QSettings>

#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QSvgRenderer>

DropArea::DropArea(QWidget *parent) : QLabel(parent)
{
    setAlignment(Qt::AlignCenter);
    setAutoFillBackground(true);
    setFrameStyle(QFrame::NoFrame);

    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);

    QFont f = font();
#ifdef Q_OS_ANDROID
    f.setPointSize(28);
#else
    f.setPointSize(f.pointSize()+2);
#endif
    setFont(f);

    clear();
}

void DropArea::clear()
{
    qWarning()<<"DropArea now accepting drops";
    filled = false;
#ifndef Q_OS_ANDROID
    setAcceptDrops(true);
#endif
    QLabel::setText(tr("Drop firmware file<br>here"));
}

#ifndef Q_OS_ANDROID
void DropArea::dragEnterEvent(QDragEnterEvent *event)
{
    const QMimeData *mimeData = event->mimeData();
    if(mimeData->hasUrls())
    {
        QList<QUrl> urlList = mimeData->urls();
        if((urlList.size()==1) && (urlList.at(0).isLocalFile()))
        {
            setFrameStyle(QFrame::Sunken);
            event->acceptProposedAction();
            return;
        }
    }
}

void DropArea::dragMoveEvent(QDragMoveEvent *event)
{
    event->acceptProposedAction();
}

void DropArea::dragLeaveEvent(QDragLeaveEvent *event)
{
    event->accept();
}

void DropArea::dropEvent(QDropEvent *event)
{
    const QMimeData *mimeData = event->mimeData();
    if(mimeData->hasUrls())
    {
        QList<QUrl> urlList = mimeData->urls();
        if((urlList.size()==1) && (urlList.at(0).isLocalFile()))
        {
            QSettings().setValue(SETTINGS_LAST_FILE,urlList.at(0).toLocalFile());
            QLabel::setText(QFileInfo(urlList.at(0).toLocalFile()).fileName());
            emit changed(urlList.at(0).toLocalFile());
            event->acceptProposedAction();
            filled = true;
            /*
             * Warning: Do not modify this property in a drag and drop event handler.
             */
            //setAcceptDrops(false);
        }
    }
}
#endif

#ifdef Q_OS_ANDROID
void DropArea::mouseReleaseEvent(QMouseEvent* ev)
{
    if(ev->button() == Qt::LeftButton)
    {
        QMetaObject::invokeMethod(this, &DropArea::selectFile, Qt::QueuedConnection);
    }
    QLabel::mouseReleaseEvent(ev);
}

void DropArea::selectFile()
{
    QString std = QFileDialog::getOpenFileName(this,tr("Select DFU File"),
                                               QSettings().value(SETTINGS_LAST_FILE,"").toString(),
                                               tr("DFU Files (*.dfu)"));

    if(!std.isEmpty())
    {
        qWarning()<<"Selected "<<std;
        QSettings().setValue(SETTINGS_LAST_FILE, std);
        emit changed(std);
        filled = true;
    }
    else
    {
        qWarning()<<"DFU file selection canceled (empty ret)";
    }
}
#else
void DropArea::mouseDoubleClickEvent(QMouseEvent *ev)
{
    ev->accept();

    QString std = QFileDialog::getOpenFileName(this,tr("Select DFU File"),
                                               QSettings().value(SETTINGS_LAST_FILE,"").toString(),
                                               tr("DFU Files (*.dfu)"));

    if(!std.isEmpty())
    {
        qWarning()<<"Selected "<<std;
        QSettings().setValue(SETTINGS_LAST_FILE, std);
        emit changed(std);
        filled = true;
        setAcceptDrops(false);
    }
}
#endif

void DropArea::paintEvent(QPaintEvent *e)
{
    const QRect fullRect = rect();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor outline = palette().color(QPalette::Mid);
    outline.setAlpha(190);
    QPen pen(outline);
    pen.setWidth(1);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    QColor surface = palette().color(QPalette::AlternateBase);
    surface.setAlpha(225);
    painter.setBrush(surface);
    QRect outerRect = fullRect.adjusted(1, 1, -1, -1);
    painter.drawRoundedRect(outerRect, 5, 5);
    QRect innerRect = fullRect.adjusted(6, 6, -6, -6);
    pen.setStyle(Qt::DashLine);
    pen.setWidth(3);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(innerRect, 5, 5);
    int iw = qMin<int>(128, qMin<int>(fullRect.width() / 3, fullRect.height() / 2));
    QSvgRenderer(QString(":/dfu/images/firmware.svg")).render(&painter, QRect(fullRect.left()+8, fullRect.bottom() - iw - 8, iw, iw));
    //painter.drawImage(QRect(fullRect.left()+10, fullRect.bottom() - iw - 10, iw, iw), QImage(":/dfu/images/firmware.svg"));

#ifndef OD_NO_DEVELOPER

    painter.save();
    QFont font = painter.font();
    font.setPixelSize(48);
    painter.setFont(font);
    painter.setPen(Qt::red);
    painter.drawText(innerRect, Qt::AlignRight|Qt::AlignBottom, "DEBUG!");
    painter.restore();
#endif

    QLabel::paintEvent(e);
}
