import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.12

GroupBox {
    id: control
    property alias check: check
    property alias enabled: check.checked

    topPadding: verticalPadding + check.height

    background: Rectangle {
        y: check.height / 2
        width: control.width
        height: control.height - check.height / 2
        color: darktheme ? Material.background : Qt.lighter(Material.background)
        border.color: Material.primary
        radius: 5
    }

    label: CheckBox {
        id: check
        anchors.horizontalCenter: control.horizontalCenter
        anchors.top: control.top

        background: Rectangle {
            color: Material.background
            radius: 5
        }
    }
}
