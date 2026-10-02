import QtQuick 2.10
import QtQuick.Controls 2.3
import QtQuick.Controls.Material 2.2
import QtGraphicalEffects 1.0

Rectangle {
    id: sensorNone
    property real value: 0
    property string unit: ""
    property real min: 0
    property real max: 0

    color: "transparent"

    Image {
        id: smiley
        anchors.centerIn: parent
        width: sensorNone.width*0.7
        height: width
        source: "images/sad_smiley.svg"
    }

    ColorOverlay{
        anchors.fill: smiley
        source:smiley
        color: Material.primary
        antialiasing: true
    }
}
