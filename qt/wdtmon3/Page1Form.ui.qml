import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
import QtQuick.Controls.Universal 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.15
import QtQuick.Extras 1.4
import Qt.labs.calendar 1.0
import Qt.labs.platform 1.1

Item {
    id: mainpage
    property alias urlField: urlField
    property alias monitorField: monitorField
    property alias processListTool: processListTool

    property alias label_info: label_info
    property alias ch2Switch: ch2Switch
    property alias ch1Switch: ch1Switch
    property alias checkBox_l: checkBox_l
    property alias checkBox_p: checkBox_p
    property alias remote_btn_m1: remote_btn_m1
    property alias remote_btn_m2: remote_btn_m2
    property alias get_in_label: get_in_label
    property alias pingTimeoutLabel: pingTimeoutLabel
    property alias pingTimeoutSlider: pingTimeoutSlider
    property alias pingTimeoutText: pingTimeoutText

    property alias portsCombo: portsCombo
    property alias statusIndicator: statusIndicator
    property alias scanbtn: scanbtn

    property alias urlGroup: urlGroup
    property alias monitorGroup: monitorGroup
    property alias manualControl: manualControl

    property string wdg_type: ""
    property string wdg_version: "0.0"
    property string wdg_version_full: qsTr("n/a")
    property string wdg_channel: qsTr("n/a")
    property int wdg_mcu_temp: 0
    property real wdg_mcu_vdda: 0.0

    Flickable {
        id: flickable
        anchors.fill: parent
        clip: true

        contentHeight: pane.height

        Pane {
            id: pane
            width: parent.width

            ColumnLayout {
                id: columnLayout1
                anchors.fill: parent

                MaterialCard {
                    id: groupBox1
                    Layout.fillWidth: true

                    RowLayout {
                        id: rowLayout
                        anchors.fill: parent
                        spacing: 5

                        MaterialComboBox {
                            id: portsCombo
                            Layout.fillWidth: true
                        }

                        MaterialButton {
                            id: scanbtn
                            text: qsTr("Scan")
                            //tooltip: qsTr("Scan")
                            //text: MaterialFont.icon.refresh
                        }

                        MaterialIndicator {
                            text: MaterialFont.icon.link
                            color: Material.foreground
                            id: statusIndicator
                            bg: "transparent"
                            speed: 1000
                            Material.elevation: 3
                        }
                    }
                }

                RowLayout {
                    id: watcherGroup
                    Layout.fillWidth: true

                    MaterialCheckGroup {
                        id: urlGroup
                        Layout.fillWidth: true
                        implicitHeight: monitorGroup.height

                        check.text: qsTr("Network Monitoring")

                        /*label: CheckBox {
                            id: urlCheckBox
                            checked: false
                            text: qsTr("Network Monitoring")
                        }*/

                        MaterialInput {
                            anchors.fill: parent
                            id: urlField
                            text: qsTr("disabled")
                            enabled: urlGroup.enabled
                        }

                        /*RowLayout {
                            anchors.fill: parent
                            TextField {
                                id: urlField
                                Layout.fillWidth: true

                                background: Rectangle {
                                    implicitWidth: 227
                                    implicitHeight: 40
                                    color: urlField.activeFocus ? "transparent" : "white"
                                    border.color: urlField.activeFocus ? "transparent" : Material.accent
                                }

                                text: qsTr("disabled")
                                leftPadding: 8
                                bottomPadding: 2
                                enabled: urlCheckBox.checked
                                selectByMouse: true

                                MouseArea {
                                    id: urlFieldMouse
                                    anchors.fill: parent
                                    acceptedButtons: Qt.RightButton
                                }
                            }
                        }*/
                    }

                    MaterialCheckGroup {
                        id: monitorGroup
                        Layout.fillWidth: true
                        check.text: qsTr("Process Monitoring")

                        /*label: CheckBox {
                            id: monitorCheckBox
                            checked: false
                            text: qsTr("Process Monitoring")
                        }*/

                        RowLayout {
                            anchors.fill: parent

                            MaterialInput {
                                id: monitorField
                                Layout.fillWidth: true
                                text: qsTr("")
                                enabled: monitorGroup.enabled
                            }

                            /*TextField {
                                id: monitorField
                                Layout.fillWidth: true
                                background: Rectangle {
                                    implicitWidth: 227
                                    implicitHeight: 40
                                    color: monitorField.activeFocus ? "transparent" : "white"
                                    border.color: monitorField.activeFocus ? "transparent" : Material.accent
                                }

                                text: qsTr("")
                                leftPadding: 8
                                bottomPadding: 2
                                enabled: monitorCheckBox.checked
                                selectByMouse: true

                                MouseArea {
                                    id: monFieldMouse
                                    anchors.fill: parent
                                    acceptedButtons: Qt.RightButton
                                }
                            }*/

                            MaterialToolButton {
                                id: processListTool
                                text: MaterialFont.icon.list
                                //enabled: monitorCheckBox.checked

                                tooltip: qsTr("Select from the list of currently running processes")
                            }
                        }
                    }
                }

                MaterialCheckGroup {
                    id: manualControl
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    check.text: qsTr("Manual Control")

                    GridLayout {
                        id: gridLayout
                        anchors.fill: parent
                        columns: 3
                        enabled: manualControl.enabled

                        MaterialLabel {
                            id: label_info
                            text: ""
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.columnSpan: 2
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        MaterialLabel {
                            id: get_in_label
                            text: qsTr("n/a")
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                        }

                        MaterialItem {
                            Layout.fillWidth: true
                            Layout.minimumWidth: remote_btn_m1.width + 2

                            MaterialButton {
                                anchors.centerIn: parent
                                id: remote_btn_m1
                                text: qsTr("Remote Reset")
                            }
                        }

                        MaterialItem {
                            Layout.fillWidth: true
                            Layout.minimumWidth: remote_btn_m2.width + 2

                            MaterialButton {
                                anchors.centerIn: parent
                                id: remote_btn_m2
                                text: qsTr("Remote Power")
                            }
                        }

                        MaterialItem {
                            Layout.fillWidth: true
                            Layout.minimumWidth: checkBox_l.width + 2

                            Switch {
                                id: checkBox_l
                                anchors.centerIn: parent
                                text: qsTr("Green LED")

                                hoverEnabled: true
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Checked: Green led on the device is enabled")
                                              + "\n" + qsTr(
                                                  "Unchecked: green led is disabled")
                            }
                        }

                        MaterialItem {
                            Layout.fillWidth: true
                            Layout.minimumWidth: ch1Switch.width + 2
                            //height: ch1Switch.height + 2
                            Switch {
                                id: ch1Switch
                                anchors.centerIn: parent
                                text: qsTr("CH1 Open")

                                checked: false
                            }
                        }

                        MaterialItem {
                            Layout.fillWidth: true
                            Layout.minimumWidth: ch2Switch.width + 2
                            //height: ch2Switch.height + 2

                            Switch {
                                id: ch2Switch
                                anchors.centerIn: parent
                                text: qsTr("CH2 Open")
                            }
                        }

                        MaterialItem {
                            Layout.fillWidth: true
                            Layout.minimumWidth: checkBox_p.width+2
                            //height: checkBox_p.height+2

                            Switch {
                                id: checkBox_p
                                text: qsTr("Suspend")
                                anchors.centerIn: parent

                                hoverEnabled: true
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Checked: Timer suspended, device is inactive")
                                              + "\n" + qsTr(
                                                  "Unchecked: Timer is running, device is active (the default)")
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.columnSpan: 3

                            Label {
                                id: pingTimeoutLabel
                                text: qsTr("Ping Timeout:")
                            }

                            MaterialSlider {
                                id: pingTimeoutSlider
                                Layout.fillWidth: true

                                from: 200
                                to: 2500
                                value: 1000
                                stepSize: 10
                            }

                            Label {
                                id: pingTimeoutText
                                text: qsTr("ms")
                            }
                        }
                    }
                }
            }
        }

        ScrollBar.vertical: ScrollBar {
        }
    }
}
