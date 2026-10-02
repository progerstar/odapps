import QtQuick 2.10
import QtQuick.Controls 2.3
import QtQuick.Controls.Styles 1.4
import QtQuick.Extras 1.4
import QtQuick.Extras.Private 1.0
import QtGraphicalEffects 1.0
import QtQuick.Controls.Material 2.2

CircularGauge {
    id: gauge
    property string unit: ""
    property real min: 0
    property real max: 100
    property real accept_min: 0.0
    property real accept_max: 0.0
    property real normal_min: 0.0
    property real normal_max: 0.0
    property int  mstepSize: settings.read(SETTINGS_UNITS_TEMP,"C")==="C" ? 5 : 10

    minimumValue: Math.round(min) - (mstepSize - Math.abs(Math.round(min))%mstepSize)%mstepSize
    maximumValue: Math.round(max) + (mstepSize - Math.abs(Math.round(max))%mstepSize)%mstepSize

    style: CircularGaugeStyle {
        id: style
        tickmarkStepSize: gauge.mstepSize

        function degreesToRadians(degrees) {
            return degrees * (Math.PI / 180);
        }

        function percent(value) {
            return (value-gauge.minimumValue)/(gauge.maximumValue-gauge.minimumValue)
        }

        background: Canvas {
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();

                /*
                //overspeed side line
                ctx.beginPath();
                ctx.strokeStyle = Material.primary;
                ctx.lineWidth = 2;

                ctx.arc(outerRadius, outerRadius, outerRadius - ctx.lineWidth / 2,
                        degreesToRadians(valueToAngle(gauge.minimumValue + 0.8*(gauge.maximumValue-gauge.minimumValue)) - 90),
                        degreesToRadians(valueToAngle(gauge.maximumValue) - 90));
                ctx.stroke();
                */


                /*
                //gradient: blue - red
                var grad = ctx.createConicalGradient(outerRadius, outerRadius, 3*Math.PI/2)
                grad.addColorStop(0,Qt.rgba(1,0,0,1))
                grad.addColorStop(1,Qt.rgba(0,0,1,1))
                //grad.addColorStop(0,"red")
                //grad.addColorStop(1,"blue")
                ctx.strokeStyle = grad
                ctx.lineWidth = outerRadius * 0.19;
                ctx.lineCap = "round"
                ctx.arc(outerRadius, outerRadius, outerRadius*0.9- ctx.lineWidth / 2,
                        degreesToRadians(valueToAngle(gauge.minimumValue) - 90),
                        degreesToRadians(valueToAngle(gauge.maximumValue) - 90));
                */

                ctx.lineWidth = 8
                ctx.lineCap = "flat"
                if((gauge.accept_max > gauge.accept_min)&&(gauge.normal_max > gauge.normal_min)) {
                    //min - accept_min; accept_min-normal_min; normal_min-normal_max; normal_max-accept_max; accept_max-max
                    ctx.beginPath();
                    ctx.strokeStyle = IOStyle.sensorCritical
                    ctx.arc(outerRadius, outerRadius, outerRadius*0.94 - ctx.lineWidth/2,
                            degreesToRadians(valueToAngle(gauge.minimumValue) - 90),
                            degreesToRadians(valueToAngle(gauge.accept_min) - 90));
                    ctx.stroke();

                    ctx.beginPath();
                    ctx.strokeStyle = IOStyle.sensorAccept
                    ctx.arc(outerRadius, outerRadius, outerRadius*0.94 - ctx.lineWidth/2,
                            degreesToRadians(valueToAngle(gauge.accept_min) - 90),
                            degreesToRadians(valueToAngle(gauge.normal_min) - 90));
                    ctx.stroke();

                    ctx.beginPath();
                    ctx.strokeStyle = IOStyle.sensorNormal
                    ctx.arc(outerRadius, outerRadius, outerRadius*0.94 - ctx.lineWidth/2,
                            degreesToRadians(valueToAngle(gauge.normal_min) - 90),
                            degreesToRadians(valueToAngle(gauge.normal_max) - 90));
                    ctx.stroke();

                    ctx.beginPath();
                    ctx.strokeStyle = IOStyle.sensorAccept
                    ctx.arc(outerRadius, outerRadius, outerRadius*0.94 - ctx.lineWidth/2,
                            degreesToRadians(valueToAngle(gauge.normal_max) - 90),
                            degreesToRadians(valueToAngle(gauge.accept_max) - 90));
                    ctx.stroke();

                    ctx.beginPath();
                    ctx.strokeStyle = IOStyle.sensorCritical
                    ctx.arc(outerRadius, outerRadius, outerRadius*0.94 - ctx.lineWidth/2,
                            degreesToRadians(valueToAngle(gauge.accept_max) - 90),
                            degreesToRadians(valueToAngle(gauge.maximumValue) - 90));
                    ctx.stroke();
                } else {
                    ctx.beginPath();
                    ctx.strokeStyle = Material.primary
                    ctx.arc(outerRadius, outerRadius, outerRadius*0.94 - ctx.lineWidth/2,
                            degreesToRadians(valueToAngle(gauge.minimumValue) - 90),
                            degreesToRadians(valueToAngle(gauge.maximumValue) - 90));
                    ctx.stroke();
                }
            }
        }

        tickmark: Rectangle {
            implicitWidth: outerRadius * 0.02
            antialiasing: true
            implicitHeight: outerRadius * 0.06
            color: Qt.darker(Material.primary)
        }

        minorTickmark: Rectangle {
            visible: true //percent(styleData.value)<0.8
            implicitWidth: outerRadius * 0.01
            antialiasing: true
            implicitHeight: outerRadius * 0.03
            color: Material.primary
        }

        tickmarkLabel:  Text {
            font.pixelSize: Math.max(6, outerRadius * 0.1)
            text: styleData.value
            color: Material.primary
            antialiasing: true
            visible: (Math.round(styleData.value)%(2*tickmarkStepSize))===0
        }

        needle: Item {
            y: -outerRadius * 0.05
            implicitWidth: outerRadius * 0.08
            implicitHeight: outerRadius * 0.85

            Image {
                id: needle
                source: "needle.png"
                height: parent.height
                width: parent.width
                asynchronous: true
                antialiasing: true
            }
        }

        foreground: Item {

            Rectangle {
                id: speedLabelFrame
                anchors.centerIn: parent
                anchors.verticalCenterOffset: gauge.height*0.15
                width: Math.max(speedLabel.width * 1.2,outerRadius*0.6)
                height: speedLabel.height + 6
                radius: 5
                color: Material.primary
                border.color: "white"
            }

            Text {
                id: speedLabel
                anchors.centerIn: parent
                anchors.verticalCenterOffset: gauge.height*0.15
                text: "%1%2".arg((gauge.value*100).toFixed(0) / 100.).arg(gauge.unit)
                font.pixelSize: outerRadius * 0.2
                color: "white"
                antialiasing: true
            }
        }
    }
}


