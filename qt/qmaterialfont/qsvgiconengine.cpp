#include "qsvgiconengine.h"

#include <QPainter>
#include <QtSvg/QSvgRenderer>

QSVGIconEngine::QSVGIconEngine(const std::string& iconBuffer) : QIconEngine()
{
    data = QByteArray::fromStdString(iconBuffer);
}

QSVGIconEngine::QSVGIconEngine(const QByteArray& source) : QIconEngine() {
    data = source;
}

QSVGIconEngine::QSVGIconEngine(const QSVGIconEngine* other) : QIconEngine(), data(other->data) {

}

void QSVGIconEngine::paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) {
    Q_UNUSED(mode);Q_UNUSED(state);

    QSvgRenderer renderer(data);
    renderer.render(painter, rect);
}

QIconEngine *QSVGIconEngine::clone() const {
    return new QSVGIconEngine(*this);
}

QPixmap QSVGIconEngine::pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) {
    // This function is necessary to create an EMPTY pixmap. It's called always
    // before paint()

    QImage img(size, QImage::Format_ARGB32);
    img.fill(qRgba(0, 0, 0, 0));
    QPixmap pix = QPixmap::fromImage(img, Qt::NoFormatConversion);

    {
        QPainter painter(&pix);
        QRect r(QPoint(0.0, 0.0), size);
        this->paint(&painter, r, mode, state);
    }

    return pix;
}
