import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.12

GroupBox {
    id: control
    topPadding: verticalPadding + tlabel.height

    background: Rectangle {
        y: control.verticalPadding + title.font.pixelSize - control.bottomPadding
        width: parent.width
        height: parent.height - control.verticalPadding - title.font.pixelSize + control.bottomPadding
        border.color: Material.primary
        radius: 2
    }

    label: Rectangle {
        id: tlabel
        anchors.horizontalCenter: control.horizontalCenter
        anchors.top: control.top
        //anchors.bottom: parent.top
        //anchors.bottomMargin: -height/2
        color: Material.primary
        width: control.width * 0.7
        height: title.font.pixelSize * 2
        radius: 3
        Text {
            id: title
            text: control.title
            anchors.centerIn: parent
            color: Material.background
        }
    }
}
