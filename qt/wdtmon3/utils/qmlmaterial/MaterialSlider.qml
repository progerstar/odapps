import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.12

Slider {
    id: control
    value: 0.5
    property alias bg: activestrip.color
    property alias color: ball.color

    background: Rectangle {
        id: strip
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: 200
        implicitHeight: 4
        width: control.availableWidth
        height: implicitHeight
        radius: 2
        color: darktheme ? Material.background : Qt.lighter(Material.background)

        Rectangle {
            id: activestrip
            width: control.visualPosition * parent.width
            height: parent.height
            color: Material.primary
            radius: 2
        }
    }

    handle: Rectangle {
        id: ball
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: 26
        implicitHeight: 26
        radius: 13
        color: control.pressed ? Material.accent : Material.primary
    }
}
