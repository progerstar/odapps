import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15

Item {
    id: item3
    property alias hideSwitch: hideSwitch
    property alias autostartSwitch: autostartSwitch
    property alias softwareSwitch: softwareSwitch
    property alias text1: text1
    property alias legacySwitch: legacySwitch
    property alias logSwitch: logSwitch
    property alias logSetupText : logSetupText
    property alias logViewText : logViewText
    property alias langCombo: langCombo
    property alias langWarn: langWarn
    property alias themeCombo: themeCombo

    ColumnLayout {
        anchors.fill: parent
        spacing: 30

        Label {
            id: text1
            textFormat: Text.RichText
            wrapMode: Text.WordWrap
            font.pixelSize: 16
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
                id: softwareSwitch
                text: qsTr("Software Rendering")
                Layout.columnSpan: 2

                hoverEnabled: true
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Checked: use software rendering (less resource-demanding, ugly interface)")+
                              "\n"+
                              qsTr("Unchecked: use OpenGL based rendering (resource-demanding)")
            }

            Switch {
                id: legacySwitch
                text: qsTr("Compatibility Mode")
                Layout.columnSpan: 2

                hoverEnabled: true
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Enable in order to communicate with older Watchdog Lite (v2.2 and earlier)")+
                              "\n"+
                              qsTr("Disable for all newer devices")
            }

            Switch {
                id: logSwitch
                text: qsTr("Logging")

                hoverEnabled: true
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Enable/Disable logging")
            }

            Label {
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

            Label {
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

        Item {
            Layout.fillHeight: true
        }

        RowLayout {
            Layout.fillWidth: true

            Label {
                id: langWarn
                color: "transparent"
                Layout.fillWidth: true
                text: ""
            }

            Label {
                text: qsTr("Language")
            }

            ComboBox {
                id: langCombo
                model: [qsTr("System"), qsTr("English"), qsTr("Russian")]
            }

            Label {
                text: qsTr("Theme")
                visible: (Qt.platform.os !== "osx")
            }

            ComboBox {
                id: themeCombo
                model: [qsTr("Light"), qsTr("Dark")]
            }
        }
    }
}
