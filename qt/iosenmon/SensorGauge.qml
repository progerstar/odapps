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

    //optional second channel (e.g. humidity next to temperature): inner scale, second needle and second readout
    property bool dual: false
    property real value2: 0
    property string unit2: ""
    property real min2: 0
    property real max2: 100
    property real accept_min2: 0.0
    property real accept_max2: 0.0
    property real normal_min2: 0.0
    property real normal_max2: 0.0
    property int  mstepSize2: 5

    function floorToStep(v, step) {
        return Math.round(v) - (step - Math.abs(Math.round(v))%step)%step
    }
    function ceilToStep(v, step) {
        return Math.round(v) + (step - Math.abs(Math.round(v))%step)%step
    }
    readonly property real minimumValue2: floorToStep(min2, mstepSize2)
    readonly property real maximumValue2: ceilToStep(max2, mstepSize2)

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

        //like valueToAngle(), for the scale of the second channel (same sweep, own range)
        function valueToAngle2(value) {
            var fraction = (value-gauge.minimumValue2)/(gauge.maximumValue2-gauge.minimumValue2)
            return minimumValueAngle + Math.max(0, Math.min(1, fraction)) * angleRange
        }

        background: Canvas {
            //zones and tickmarks of the inner scale
            function drawInnerScale(ctx) {
                var radius = outerRadius*0.55
                var zoneWidth = 6

                function zone(from, to, color) {
                    ctx.beginPath();
                    ctx.strokeStyle = color
                    ctx.lineWidth = zoneWidth
                    ctx.lineCap = "flat"
                    ctx.arc(outerRadius, outerRadius, radius - zoneWidth/2,
                            degreesToRadians(valueToAngle2(from) - 90),
                            degreesToRadians(valueToAngle2(to) - 90));
                    ctx.stroke();
                }

                if((gauge.accept_max2 > gauge.accept_min2)&&(gauge.normal_max2 > gauge.normal_min2)) {
                    zone(gauge.minimumValue2, gauge.accept_min2, IOStyle.sensorCritical)
                    zone(gauge.accept_min2, gauge.normal_min2, IOStyle.sensorAccept)
                    zone(gauge.normal_min2, gauge.normal_max2, IOStyle.sensorNormal)
                    zone(gauge.normal_max2, gauge.accept_max2, IOStyle.sensorAccept)
                    zone(gauge.accept_max2, gauge.maximumValue2, IOStyle.sensorCritical)
                } else {
                    zone(gauge.minimumValue2, gauge.maximumValue2, Material.accent)
                }

                //a major tickmark every mstepSize2 with a minor one halfway, pointing to the centre
                var half = gauge.mstepSize2 / 2
                var count = Math.round((gauge.maximumValue2 - gauge.minimumValue2) / half)
                ctx.strokeStyle = Material.accent
                for(var i = 0; i <= count; i++) {
                    var major = ((i % 2) === 0)
                    var angle = degreesToRadians(valueToAngle2(gauge.minimumValue2 + i*half))
                    var start = radius - zoneWidth - 2
                    var end = start - outerRadius*(major ? 0.06 : 0.03)
                    ctx.lineWidth = major ? 2 : 1
                    ctx.beginPath();
                    ctx.moveTo(outerRadius + start*Math.sin(angle), outerRadius - start*Math.cos(angle));
                    ctx.lineTo(outerRadius + end*Math.sin(angle), outerRadius - end*Math.cos(angle));
                    ctx.stroke();
                }
            }

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

                if(gauge.dual) {
                    drawInnerScale(ctx)
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
            id: foregroundItem

            //not inside the label delegate: Text has a property of its own named "style", hiding the style here
            function angle2(value) {
                return style.degreesToRadians(style.valueToAngle2(value))
            }

            //labels of the inner scale, every second tickmark
            Repeater {
                model: gauge.dual ? Math.floor((gauge.maximumValue2-gauge.minimumValue2)/(2*gauge.mstepSize2)) + 1 : 0

                Text {
                    property real labelValue: gauge.minimumValue2 + index*2*gauge.mstepSize2
                    property real labelAngle: foregroundItem.angle2(labelValue)
                    x: parent.width/2 + Math.sin(labelAngle)*outerRadius*0.36 - width/2
                    y: parent.height/2 - Math.cos(labelAngle)*outerRadius*0.36 - height/2
                    font.pixelSize: Math.max(6, outerRadius * 0.075)
                    text: labelValue
                    color: Material.accent
                    antialiasing: true
                }
            }

            //second needle: shorter, so that it reaches the inner scale only
            Item {
                id: needle2
                visible: gauge.dual
                x: parent.width/2
                y: parent.height/2
                rotation: style.valueToAngle2(gauge.value2)

                Behavior on rotation {
                    NumberAnimation { duration: 250; easing.type: Easing.OutCubic }
                }

                Canvas {
                    width: outerRadius * 0.06
                    height: outerRadius * 0.52
                    x: -width/2
                    y: -height
                    onPaint: {
                        var ctx = getContext("2d");
                        ctx.reset();
                        ctx.fillStyle = Material.accent;
                        ctx.beginPath();
                        ctx.moveTo(0, height);
                        ctx.lineTo(width, height);
                        ctx.lineTo(width/2, 0);
                        ctx.closePath();
                        ctx.fill();
                    }
                }
            }

            Rectangle {
                visible: gauge.dual
                anchors.centerIn: parent
                width: outerRadius * 0.09
                height: width
                radius: width/2
                color: Material.primary
                border.color: "white"
            }

            //the frames are sized for the widest reading the gauge can show, so they keep their size while the values change
            TextMetrics {
                id: minReading
                font: speedLabel.font
                text: "%1%2".arg(IOStyle.formatValue(gauge.minimumValue)).arg(gauge.unit)
            }

            TextMetrics {
                id: maxReading
                font: speedLabel.font
                text: "%1%2".arg(IOStyle.formatValue(gauge.maximumValue)).arg(gauge.unit)
            }

            Rectangle {
                id: speedLabelFrame
                anchors.centerIn: parent
                anchors.verticalCenterOffset: gauge.dual ? outerRadius*0.86 : gauge.height*0.15
                width: Math.max(Math.max(minReading.width, maxReading.width) * 1.2,outerRadius*0.6)
                height: speedLabel.height + 6
                radius: 5
                color: Material.primary
                border.color: "white"
            }

            Text {
                id: speedLabel
                anchors.centerIn: parent
                anchors.verticalCenterOffset: gauge.dual ? outerRadius*0.86 : gauge.height*0.15
                text: "%1%2".arg(IOStyle.formatValue(gauge.value)).arg(gauge.unit)
                font.pixelSize: outerRadius * (gauge.dual ? 0.14 : 0.2)
                color: "white"
                antialiasing: true
            }

            TextMetrics {
                id: minReading2
                font: speedLabel2.font
                text: "%1%2".arg(IOStyle.formatValue(gauge.minimumValue2)).arg(gauge.unit2)
            }

            TextMetrics {
                id: maxReading2
                font: speedLabel2.font
                text: "%1%2".arg(IOStyle.formatValue(gauge.maximumValue2)).arg(gauge.unit2)
            }

            Rectangle {
                id: speedLabelFrame2
                visible: gauge.dual
                anchors.centerIn: parent
                anchors.verticalCenterOffset: outerRadius*0.44
                width: Math.max(minReading2.width, maxReading2.width) * 1.15
                height: speedLabel2.height + 4
                radius: 5
                color: Material.accent
                border.color: "white"
            }

            Text {
                id: speedLabel2
                visible: gauge.dual
                anchors.centerIn: parent
                anchors.verticalCenterOffset: outerRadius*0.44
                text: "%1%2".arg(IOStyle.formatValue(gauge.value2)).arg(gauge.unit2)
                font.pixelSize: outerRadius * 0.095
                color: "white"
                antialiasing: true
            }
        }
    }
}


