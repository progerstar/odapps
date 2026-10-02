import QtQuick 2.10
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.3
import QtQuick.Controls.Material 2.2

Rectangle {
    id: radioSwitch
    property string textLeft: "Off"
    property var roleLeft: 0
    property string textRight: "On"
    property var roleRight: 1
    readonly property string currentText: control.checked ? textRight : textLeft
    readonly property var currentRole: control.checked ? roleRight : roleLeft
    property alias checked: control.checked

    border.width: 0
    color: "transparent"

    height: layout.height+2

    RowLayout {
        id: layout
        width: parent.width
        anchors.margins: 1
        spacing: 4

        Text {
            text: radioSwitch.textLeft
            color: Material.accent
        }
        Switch {
            id: control
        }
        Text {
            text: radioSwitch.textRight
            color: Material.accent
        }
    }
}
