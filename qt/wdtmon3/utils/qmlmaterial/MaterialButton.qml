import QtQuick 2.15
import QtQuick.Controls 2.5
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.4

Button {
    id: tool
    property color bgColor: Material.primary
    property color normalColor: "white"
    property color pressedColor: Material.accent
    property string micon: ""

    background: Rectangle {
        implicitWidth: 100
        implicitHeight: 40
        opacity: enabled ? 1 : 0.3
        border.color: tool.down ? Material.accent : tool.bgColor
        border.width: 1
        radius: 4
        color: tool.bgColor
    }

    contentItem: RowLayout {
        Text {
            Layout.fillHeight: true
            text: tool.micon
            font.family: MaterialFont.fontName
            font.pointSize: 22
            color: tool.down ? pressedColor : normalColor
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideMiddle
        }

        Text {
            Layout.fillHeight: true
            text: tool.text
            font: tool.font
            color: tool.down ? pressedColor : normalColor
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
}
