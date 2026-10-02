import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.12

Pane {
    property alias color: bg.color
    property alias edge: bg.border.color
    //Material.elevation: 5

    background: Rectangle {
        id: bg
        color: darktheme ? Material.background : Qt.lighter(Material.background)
        border.width: 1
        border.color: Material.accent
        radius: 5
    }
}
