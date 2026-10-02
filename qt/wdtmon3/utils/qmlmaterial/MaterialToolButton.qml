import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.12

ToolButton {
    id: mtool
    property color bgColor: Material.primary
    property color normalColor: "white"
    property color pressedColor: Material.accent
    property string tooltip: ""

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

    background: Rectangle {
        color: mtool.bgColor
        opacity: mtool.enabled ? 1 : 0.3
        radius: width/2
    }

    hoverEnabled: true
    ToolTip.visible: mtool.tooltip ? hovered : false
    ToolTip.text: mtool.tooltip
}
