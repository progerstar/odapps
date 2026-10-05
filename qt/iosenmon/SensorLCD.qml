import QtQuick 2.10
import QtQuick.Controls.Material 2.2

Rectangle {
    id: control
    property real value: 0
    property string unit: ""
    property real min: 0
    property real max: 0

    implicitWidth: content.width + 8
    implicitHeight: content.height + 8

    color: Material.background
    border.color: Material.accent
    border.width: 1
    radius: 2

    Text {
        id: content
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: 4
        font {
            family: LCDFont.font.name
            pointSize: 16
            bold: true
        }
        color: Material.primary
        text: "<b>%1</b> %2".arg(IOStyle.formatValue(value)).arg(unit)
    }
}
