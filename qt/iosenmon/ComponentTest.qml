import QtQuick 2.10
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.3


Dialog {

    Component {
        id: doublespinbox

        SpinBox{
            property real factor: Math.pow(10, decimals)
            property int  decimals: 2
            property real realValue: 0.0
            property real realFrom: 0.0
            property real realTo: 100.0
            property real realStepSize: 1.0
            property string hint: ""

            id: spinbox
            stepSize: realStepSize*factor
            value: realValue*factor
            to : realTo*factor
            from : realFrom*factor
            validator: DoubleValidator {
                bottom: Math.min(spinbox.from, spinbox.to)*spinbox.factor
                top:  Math.max(spinbox.from, spinbox.to)*spinbox.factor
            }

            textFromValue: function(value, locale) {
                return parseFloat(value*1.0/factor).toFixed(decimals);
            }

            hoverEnabled: true
            ToolTip.visible: hovered && (hint!=="")
            ToolTip.text: hint
        }

    }


    id: ds18b20_setupDialog
    title: "DS18B20 Config"
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel

    GridLayout {
        anchors.fill: parent
        anchors.rightMargin: 10
        columns: 2
        Label {
            text: "Report Interval:"
        }
        SpinBox {
            id: interval
            Layout.fillWidth: true
            from: 10
            to: 65535
            editable: true
            hoverEnabled: true
            ToolTip.visible: hovered
            ToolTip.text: "Temperature report interval (in milliseconds)"
        }
        Label {
            text: "Resolution:"
        }
        SpinBox {
            id: resolution
            Layout.fillWidth: true
            from: 9
            to: 12
            hoverEnabled: true
            ToolTip.visible: hovered
            ToolTip.text: "Sensor resolution [9-12] bits"
        }
        Label {
            text: "Threshold:"
        }
        Loader {
            sourceComponent: doublespinbox
            id: threshold
            Layout.fillWidth: true
            property real realTo: 655.35;
            property real realFrom: 0.07;
            property real realStepSize: 0.01;
            property string hint: "Sensor will report the current temperature immediately if the difference to the previous reading exceeds this value"
        }
        Label {
            text: "Minimum (normal):"
        }
        Loader {
            sourceComponent: doublespinbox
            id: norm_min
            Layout.fillWidth: true
            property int realTo: 125;
            property int realFrom: -55;
            property real realStepSize: 0.01;
            property string hint: "The minimal temperature considered as 'normal'"
        }
        Label {
            text: "Maximum (normal):"
        }
        Loader {
            sourceComponent: doublespinbox
            id: norm_max
            Layout.fillWidth: true
            property int realTo: 125;
            property int realFrom: -55;
            property real realStepSize: 0.01;
            property string hint: "The maximal temperature considered as 'normal'"
        }
        Label {
            text: "Minimum (acceptable):"
        }
        Loader {
            sourceComponent: doublespinbox
            id: accept_min
            Layout.fillWidth: true
            property int realTo: 125;
            property int realFrom: -55;
            property real realStepSize: 0.01;
            property string hint: "The minimal temperature considered as 'acceptable'"
        }
        Label {
            text: "Maximum (acceptable):"
        }
        Loader {
            sourceComponent: doublespinbox
            id: accept_max
            Layout.fillWidth: true
            property int realTo: 125;
            property int realFrom: -55;
            property real realStepSize: 0.01;
            property string hint: "The maximal temperature considered as 'acceptable'"
        }
    }
    function parse(setObj){
        interval.value = setObj.interval;
        resolution.value = setObj.resolution;
        threshold.item.realValue = setObj.threshold;
        norm_min.item.realValue = setObj.norm_min;
        norm_max.item.realValue = setObj.norm_max;
        accept_min.item.realValue = setObj.accept_min;
        accept_max.item.realValue = setObj.accept_max;
    }

    onAccepted: {
        var setObj = {
            interval: interval.value,
            resolution: resolution.value,
            threshold: threshold.item.realValue,
            norm_min: norm_min.item.realValue,
            norm_max: norm_max.item.realValue,
            accept_min: accept_min.item.realValue,
            accept_max: accept_max.item.realValue,
        };
        if (runner.sensor){
            runner.sensor.writeSettings(setObj);
        }
    }

    onClosed: {
        console.log("ds18b20_setupDialog will destroy itself");
        ds18b20_setupDialog.destroy();
    }

}
