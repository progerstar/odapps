import QtQuick 2.10
import QtQuick.Controls.Material 2.2

Rectangle {
    id: sensorText
    property real value: 0
    property string unit: ""
    property real min: 0
    property real max: 0

    color: "transparent"

    Text {
        anchors.centerIn: parent
        color: Material.primary
        font.pixelSize: 32
        text: "<b>%1</b> %2".arg(parseFloat(sensorText.value.toFixed(2))).arg(sensorText.unit)
    }
}

