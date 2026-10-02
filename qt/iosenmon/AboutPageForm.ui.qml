import QtQuick 2.10
import QtQuick.Layouts 1.3
import QtQuick.Controls 2.3

Item {
    id: item3
    property alias hideSwitch: hideSwitch
    property alias autostartSwitch: autostartSwitch
    property alias pTitle: pTitle
    property alias logSwitch: logSwitch
    property alias logSetupText : logSetupText
    property alias logViewText : logViewText
    property alias creditsText: creditsText

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Text {
            id: pTitle
            wrapMode: Text.WordWrap
            font.pixelSize: 15
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }

        GridLayout {
            Layout.alignment: Qt.AlignHCenter
            columns: 4
            Switch {
                id: autostartSwitch
                text: qsTr("Autostart")
                Layout.columnSpan: 2

                hoverEnabled: true
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Checked: start the application automatically on boot")
            }

            Switch {
                id: hideSwitch
                text: qsTr("Hide on Startup")
                Layout.columnSpan: 2

                hoverEnabled: true
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Checked: start the application minimized to system tray")
            }

            Switch {
                id: logSwitch
                text: qsTr("Logging")
                Layout.columnSpan: 2

                hoverEnabled: true
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Enable/Disable logging")
            }

            Text {
                id: logSetupText
                text: ""
                textFormat: Text.RichText
                visible: logSwitch.checked

                MouseArea {
                    anchors.fill: parent
                    cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
                    acceptedButtons: Qt.NoButton
                }
            }

            Text {
                id: logViewText
                text: ""
                textFormat: Text.RichText
                visible: logSwitch.checked

                MouseArea {
                    anchors.fill: parent
                    cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
                    acceptedButtons: Qt.NoButton
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 5
            spacing: 5

            Flickable {
                id: credFlickable
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                contentHeight: cpane.height

                Pane {
                    id: cpane
                    width: parent.width - 5

                    Text {
                        id: creditsText
                        text: "<b>"+qsTr("3rd party software used:")+"</b><br>"+
                              "<ul><li> <a href=\"https://github.com/signal11/hidapi\">hidapi</a> by Signal 11 Software (BSD License)</li>"+
                              "<li> <a href=\"https://gist.github.com/jonmcclung/bae669101d17b103e94790341301c129\">Android-like Toast</a> implementation (by John McClung)</li>"+
                              "</ul>"
                        textFormat: Text.RichText
                        wrapMode: Text.WordWrap
                        width: cpane.width - 4
                        padding: 2
                    }
                }
            }
        }
    }
}
