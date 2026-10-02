#ifndef QLEDLABEL_H_
#define QLEDLABEL_H_

#include <QLabel>
#include <QPixmap>
#include <QPainter>

class QLedLabel : public QLabel
{
        Q_OBJECT
    public:
        QLedLabel(QWidget* parent = 0) : QLabel(parent), m_pix(false) {setMinimumWidth(22);}
        ~QLedLabel(){}

        inline void on(const QString& color, const QString& label = QString())
        {
            m_pix = true;

#ifndef Q_OS_ANDROID
            QPixmap pix((width() < 16) ? QSize(16,16) : size());
            {
                pix.fill(Qt::transparent);

                QPainter paint(&pix);
                paint.setRenderHint(QPainter::Antialiasing);
                paint.setPen(QColor(color));
                paint.setBrush(QColor(color));
                paint.drawEllipse(pix.rect().adjusted(1,1,-1,-1));
            }
            setPixmap(pix);
#endif

            setToolTip(label);
            //setText(label);
        }

        inline void off()
        {
            m_pix=false;

#ifndef Q_OS_ANDROID
            setPixmap(QPixmap());
#endif

            setToolTip("");
        }

        inline bool isOn() const
        {
            return m_pix;
        }

    private:
        bool m_pix;
};

#endif // QLEDLABEL_H_
