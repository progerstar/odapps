#ifndef QSVGICONENGINE_H
#define QSVGICONENGINE_H

/*
 * ref: https://stackoverflow.com/a/44757951
 */

#include <QIconEngine>
#include <QByteArray>

class QSVGIconEngine : public QIconEngine
{
    public:
        QSVGIconEngine(const std::string& iconBuffer);
        QSVGIconEngine(const QByteArray& source);
        QSVGIconEngine(const QSVGIconEngine* other);

        virtual ~QSVGIconEngine() {}

        void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode,
                   QIcon::State state) override;
        QIconEngine *clone() const override;
        QPixmap pixmap(const QSize &size, QIcon::Mode mode,
                       QIcon::State state) override;
    private:
        QByteArray data;
};

#endif // QSVGICONENGINE_H
