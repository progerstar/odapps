#ifndef QSLIDESWITCH_H_
#define QSLIDESWITCH_H_

#include <QtGlobal>
#include <QSlider>

class QSlideSwitch : public QSlider
{
        Q_OBJECT
        Q_DISABLE_COPY(QSlideSwitch)
        Q_PROPERTY(bool checked READ isChecked WRITE setChecked NOTIFY toggled)
    public:
        explicit QSlideSwitch(QWidget* parent = nullptr);
        virtual ~QSlideSwitch();

        void setMaximum(int){};
        void setMinimum(int){};
        void setOrientation(Qt::Orientation){}
        void setTickPosition(QSlider::TickPosition){}

        inline bool isChecked() const {
            return _state;
        }
        void setChecked(bool on);
    signals:
        void clicked(bool checked = false);
        void toggled(bool checked);

    private slots:
        void changed(int value);
        void released();

    private:
        bool _state;
};


#endif // QSLIDESWITCH_H_
