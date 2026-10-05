import QtQuick 2.10
import QtQuick.Layouts 1.3
import QtQuick.Controls 2.3
import QtQuick.Controls.Material 2.2

import IOTRunner 1.0
import HidSensorInterface 1.0


ListView {
    id: sensorView
    signal display(string type, string sn)

    model: runner.sensorModel()
    spacing: 5
    focus: true

    clip: true

    delegate: Item {
        id: wrapper

        width: sensorView.width
        height: rect.height

        function stateColor(st) {
            switch(runner.stateString(st)) {
            case "normal": return IOStyle.sensorNormal;
            case "acceptable": return IOStyle.sensorAccept;
            case "critical": return IOStyle.sensorCritical;
            default: return IOStyle.sensorUnknown;
            }
        }

        function aliasText() {
            return runner.alias(sserial)
        }

        Rectangle {
            id: rect
            width: sensorView.width
            height: delegatelayout.height + 14
            color: clickable.containsMouse ? Qt.darker(IOStyle.cardColor, 1.03) : IOStyle.cardColor
            border.width: 1
            border.color: IOStyle.cardBorder
            radius: IOStyle.cardRadius

            //state indicator
            Rectangle {
                id: stripe
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.margins: 1
                width: 6
                radius: IOStyle.cardRadius
                color: wrapper.stateColor(sstate)
            }

            GridLayout {
                id: delegatelayout
                anchors.left: stripe.right
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 8
                anchors.rightMargin: 4

                columnSpacing: 1
                rowSpacing: 1
                columns: 2

                Label {
                    id: nameField
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    font.bold: true
                    text: wrapper.aliasText()
                }

                MaterialToolButton {
                    id: editTool
                    text: MaterialFont.icon.edit
                    font.pointSize: 14
                    height: nameField.height
                    normalColor: IOStyle.textSecondary
                    z:10

                    onClicked: {
                        aliasEdit.text = runner.alias(sserial)
                        aliasEdit.selectAll()
                        aliasEdit.visible = true;
                        aliasEdit.forceActiveFocus();
                    }
                }

                Label {
                    id: valueField
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    font.pixelSize: 18
                    text: "%1  %2%3".arg(stype).arg(IOStyle.formatValue(sdata)).arg(sunit)
                }
            }

            MouseArea {
                id: clickable
                anchors.fill: rect
                acceptedButtons: Qt.LeftButton
                hoverEnabled: true
                enabled: !editTool.hovered
                z:1
                onClicked: {
                    sensorView.display(stype,sserial)
                }
            }

            //the alias is shared by the sensors of one device - keep every delegate in sync
            Connections {
                target: runner
                onAliasChanged: nameField.text = wrapper.aliasText()
            }

            TextField {
                id: aliasEdit
                anchors.fill: parent
                visible: false
                background: Rectangle {
                    border.width: 1
                    border.color: Material.accent
                    color: IOStyle.cardColor
                    radius: IOStyle.cardRadius
                }
                color: Material.accent
                horizontalAlignment: TextInput.AlignHCenter
                z:5

                Keys.onEscapePressed: {
                    visible = false
                }

                onEditingFinished: {
                    if(visible) {
                        visible = false;
                        runner.setAlias(sserial,text);
                    }
                }
            }
        }
    }
}
