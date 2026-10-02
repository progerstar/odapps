import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.12

Label {
    id: control
    padding: 2
    property alias edge: bg.border.color

    color: Material.primary
    wrapMode: Text.WordWrap

    font {
        pixelSize: 16
    }

    background: Rectangle {
        id: bg
        border.width: 1
        border.color: "transparent"
        color: darktheme ? Material.background : Qt.lighter(Material.background)
    }
}
