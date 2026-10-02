import QtQuick 2.10
import QtQuick.Controls 2.3

import QtQuick.Controls.Material 2.2

ToolButton {
    id: mtool
    property color normalColor: "#f5f5f5"
    property color pressedColor: Material.color(Material.DeepOrange)

    font.family: MaterialFont.fontName
    font.pointSize: 22
    text: "..."

    contentItem: Text {
        id: micon
        text: parent.text
        font: parent.font
        opacity: enabled ? 1.0 : 0.3
        color: parent.down ? pressedColor : normalColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideMiddle
    }
}
