import QtQuick 2.10
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.3
import QtQuick.Controls.Material 2.2

Rectangle {
    id: timeSlider

    border.width: 0
    color: "transparent"

    property int timeFrom: 0
    property int timeTo: 3600
    property int time: 0

    function timeStep(value) {
        if(value<120) {
            return 1;
        } else if(value<7200) {
            return 60;
        } else if(value<172800) {
            return 3600;
        } else {
            return 86400;
        }
    }

    function timeString(value) {
        if(value<120) {
            return qsTr("%1 sec.").arg(value)
        } else if(value < 7200) {
            return qsTr("%1 min.").arg(Math.round(value/60.0))
        } else if(value < 172800) {
            return qsTr("%1 hr.").arg(Math.round(value/3600.0))
        } else {
            return qsTr("%1 d.").arg(Math.round(value/86400.0))
        }
    }

    function init(value) {
        var steps = 0;

        var totalSteps = 0;
        var i = timeFrom;
        while(i < timeTo)
        {
            ++totalSteps;
            i+=timeStep(i);
        }
        i=timeFrom;
        while(i<value)
        {
            ++steps;
            i += timeStep(i);
        }
        scontrol.value = Math.round(steps*100./totalSteps)
        time = value;
    }

    height: layout.height

    RowLayout {
        id: layout
        width: parent.width

        Slider {
            id: scontrol
            from: 0
            to: 100
            live: false
            Layout.fillWidth: true

            onValueChanged: {
                var totalSteps = 0;
                var i = timeFrom;
                while(i < timeTo)
                {
                    ++totalSteps;
                    i+=timeStep(i);
                }
                var valueStep = Math.round(value*1.0*totalSteps/100.);
                i = timeFrom;
                while(valueStep>0)
                {
                    i+=timeStep(i);
                    --valueStep;
                }
                time = i
            }
        }

        Text {
            text: timeString(time)
        }
    }
}
