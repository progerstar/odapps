#include "qslideswitch.h"

QSlideSwitch::QSlideSwitch(QWidget* parent) : QSlider(parent), _state(false)
{
    QSlider::setMaximum(1);
    QSlider::setMinimum(0);
    QSlider::setOrientation(Qt::Horizontal);
    QSlider::setTickPosition(QSlider::NoTicks);

    QSlider::setValue(0);

    connect(this, &QSlider::valueChanged, this, &QSlideSwitch::changed);
    connect(this, &QSlider::sliderReleased, this, &QSlideSwitch::released);
}

QSlideSwitch::~QSlideSwitch(){}

void QSlideSwitch::setChecked(bool on)
{
    if(on != _state) {
        QSlider::setValue(int(on));
    }
}

void QSlideSwitch::changed(int value)
{
    if((!!value) != _state) {
        _state = ((!!value)==1);
        emit toggled(_state);
    }
}

void QSlideSwitch::released()
{
    if((!!QSlider::value()) != _state) {
        _state = ((!!QSlider::value())==1);
        emit clicked(_state);
    }
}
