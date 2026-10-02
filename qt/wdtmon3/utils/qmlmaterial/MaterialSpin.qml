import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.12

SpinBox {
    id: control

    editable: true

    textFromValue: function(value, locale) {
        return Number(value).toString();
    }

    background: Rectangle {
        implicitWidth: 40

        color: "transparent"
        border.width: 1
        radius: 2
        border.color: control.enabled ? Material.primary : Qt.darker(Material.foreground)
    }

    up.indicator: Rectangle {
        x: control.mirrored ? 0 : parent.width - width
        height: parent.height
        implicitWidth: 40
        implicitHeight: 32
        color: "transparent"
        border.width: 0

        Text {
            color: control.up.pressed ? Material.accent : Material.primary
            anchors.fill: parent

            font.family: MaterialFont.fontName
            font.pointSize: 16
            fontSizeMode: Text.Fit
            text: MaterialFont.icon.add

            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    down.indicator: Rectangle {
        x: control.mirrored ? parent.width - width : 0
        height: parent.height
        implicitWidth: 40
        implicitHeight: 32
        color: "transparent"
        border.width: 0

        Text {
            color: control.down.pressed ? Material.accent : Material.primary
            anchors.fill: parent

            font.family: MaterialFont.fontName
            font.pointSize: 16
            fontSizeMode: Text.Fit
            text: MaterialFont.icon.remove

            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    contentItem: TextInput {
        z: 2
        text: control.textFromValue(control.value, control.locale)

        font: control.font
        color: "black"
        selectionColor: Material.accent
        selectedTextColor: "#ffffff"
        horizontalAlignment: Text.AlignHCenter
        anchors.verticalCenter: parent.verticalCenter

        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
    }
}
