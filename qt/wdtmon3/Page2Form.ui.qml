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
    id: item1
    property alias t1Group: t1Group
    property alias t2Group: t2Group
    property alias t3Group: t3Group
    property alias channelGroup: channelGroup
    property alias tempGroup: tempGroup
    property alias p10spinBox: p10spinBox
    property alias p8Box: p8Box
    property alias p9Box: p9Box
    property alias p7Box: p7Box
    property alias p6Box: p6Box
    property alias t5Box: t5Box
    property alias t4Box: t4Box
    property alias t3Box: t3Box
    property alias t2Box: t2Box
    property alias t1Box: t1Box
    property alias writebutton: writebutton
    property alias readbutton: readbutton

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
                    id: t1Group
                    Layout.fillWidth: true

                    //title: qsTr("Timer")
                    RowLayout {
                        id: t1Layout
                        anchors.fill: parent

                        Label {
                            id: timerLabel1
                            text: qsTr("PC will be restarted if there has been<br>no signal from the app for")
                        }

                        ComboBox {
                            id: t1Box
                            Layout.fillWidth: true
                            model: ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15"]
                            textRole: qsTr("")
                            displayText: currentText + " " + qsTr("min")
                        }
                    }
                }

                MaterialCard {
                    id: t2Group
                    Layout.fillWidth: true

                    //title: qsTr("Reset")
                    RowLayout {
                        id: t2Layout
                        anchors.fill: parent

                        Label {
                            id: rstLabel1
                            Layout.columnSpan: 2
                            text: qsTr("When restarting the PC, hold the reset button for")
                        }

                        ComboBox {
                            id: t2Box
                            Layout.fillWidth: true
                            model: ["0", "100", "200", "300", "400", "500", "600", "700", "800", "900", "1000", "1100", "1200", "1300", "1400", "1500"]
                            textRole: qsTr("")
                            displayText: currentText + " " + qsTr("ms")
                        }
                    }
                }

                MaterialCard {
                    id: t3Group
                    Layout.fillWidth: true

                    //title: qsTr("Hard Reset")
                    GridLayout {
                        id: t3Layout
                        columns: 4
                        anchors.fill: parent

                        Label {
                            id: hrstLabel1
                            Layout.columnSpan: 3
                            text: qsTr("Hard reset sequence: hold the power button for")
                        }

                        ComboBox {
                            id: t3Box
                            Layout.fillWidth: true
                            model: ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"]
                            textRole: qsTr("")
                            displayText: currentText + " " + qsTr("s")
                        }

                        Label {
                            id: hrstLabel3
                            text: qsTr("release, wait")
                        }

                        ComboBox {
                            id: t4Box
                            model: ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15"]
                            textRole: qsTr("")
                            displayText: currentText + " " + qsTr("s")
                        }

                        Label {
                            id: hrstLabel4
                            text: qsTr(", press the button for")
                        }

                        ComboBox {
                            id: t5Box
                            model: ["0", "100", "200", "300", "400", "500", "600", "700", "800", "900", "1000", "1100", "1200", "1300", "1400", "1500"]
                            textRole: qsTr("")
                            displayText: currentText + " " + qsTr("ms")
                            Layout.fillWidth: true
                        }
                    }
                }

                RowLayout {
                    id: rowLayout2
                    Layout.fillWidth: true

                    ColumnLayout {
                        id: columnLayout
                        Layout.minimumWidth: 270
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        MaterialCard {
                            id: channelGroup
                            Layout.fillWidth: true
                            z: -4

                            //title: qsTr("Advanced")
                            GridLayout {
                                id: gridLayout
                                columns: 2
                                anchors.fill: parent

                                Label {
                                    id: label5
                                    text: qsTr("CH1")
                                }

                                ComboBox {
                                    id: p6Box
                                    Layout.fillWidth: true
                                    model: [qsTr("OFF"), qsTr("RESET"), qsTr(
                                            "POWER"), qsTr("OUT OPENED"), qsTr(
                                            "OUT CLOSED")]
                                    textRole: qsTr("")
                                }

                                Label {
                                    id: label6
                                    text: qsTr("CH2")
                                }

                                ComboBox {
                                    id: p7Box
                                    Layout.fillWidth: true
                                    model: [qsTr("OFF"), qsTr("RESET"), qsTr(
                                            "POWER"), qsTr("OUT OPENED"), qsTr(
                                            "OUT CLOSED")]
                                    textRole: qsTr("")
                                }
                            }
                        }

                        Row {
                            id: rowLayout
                            Layout.fillWidth: true
                            spacing: 4

                            MaterialButton {
                                id: readbutton
                                text: qsTr("Read")
                                micon: MaterialFont.icon.refresh

                                //Layout.fillWidth: true
                                width: parent.width / 2 - 2
                            }

                            MaterialButton {
                                id: writebutton
                                text: qsTr("Write")
                                micon: MaterialFont.icon.task_alt

                                //Layout.fillWidth: true
                                width: parent.width / 2 - 2
                            }
                        }
                    }

                    MaterialCard {
                        id: tempGroup
                        Layout.fillWidth: true
                        //title: qsTr("Extended")
                        z: -3

                        GridLayout {
                            id: gridLayout1
                            anchors.fill: parent
                            columns: 2

                            Label {
                                id: label7
                                text: qsTr("Reset Limit")
                            }

                            ComboBox {
                                id: p8Box
                                Layout.fillWidth: true
                                textRole: qsTr("")
                                model: ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15"]
                            }

                            Label {
                                id: label8
                                text: qsTr("IN Channel")
                            }

                            ComboBox {
                                id: p9Box
                                Layout.fillWidth: true
                                model: [qsTr("OFF"), qsTr("INPUT"), qsTr(
                                        "OUTPUT"), qsTr("TEMP")]
                                textRole: qsTr("")
                            }

                            Label {
                                id: label9
                                text: qsTr("Temperature\nThreshold")
                            }

                            SpinBox {
                                id: p10spinBox
                                Layout.fillWidth: true
                                to: 125
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
