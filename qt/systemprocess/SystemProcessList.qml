import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.4

import QtQuick.Controls.Material 2.12

import ru.opendev.systemprocess 1.0

ListView {
    id: procList

    property alias sysProcModel: sysProcModel
    property color itemBackground: Material.background
    property color tableBackground: "white"
    signal selectProcess(var name)

    model: SystemProcessModel {
        id: sysProcModel
    }

    spacing: 5
    delegate: Item {
        id: wrapper
        width: procList.width
        height: procView.height

        Rectangle {
            id: procView
            width: procList.width - vBar.width
            height: gridLayout.height+10
            border.width: 1
            border.color: Material.primary
            color: itemBackground
            radius: 1

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    selectProcess(pname)
                }
            }

            GridLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 5
                anchors.rightMargin: 2
                anchors.topMargin: 5
                anchors.bottomMargin: 2
                columns: 4
                rowSpacing: 2
                columnSpacing: 2
                id: gridLayout

                RowLayout {
                    Layout.columnSpan: 4
                    Layout.fillWidth: true
                    spacing: 0

                    Label {
                        id: procName
                        textFormat: Text.RichText
                        text: "<b>%1</b>".arg(pname)
                        Layout.fillWidth: true
                        Layout.minimumHeight: 30
                        verticalAlignment: Text.AlignVCenter
                        padding: 2

                        background: Rectangle {
                            color: tableBackground
                        }
                    }

                    Rectangle {
                        color: tableBackground
                        Layout.minimumHeight: 30
                        width: 30


                        Rectangle {
                            id: statusIndicator
                            color: (pstat===SystemProcessModel.NotResponding) ? Material.color(Material.Red) : Material.color(Material.Green)
                            visible: (pstat!==SystemProcessModel.Unknown)
                            border.width: 1
                            border.color: Material.accent
                            radius: 0
                            width: 20
                            height: 20
                            anchors.centerIn: parent

                            MouseArea {
                                id: pstatArea
                                anchors.fill: parent
                                hoverEnabled: true
                            }

                            ToolTip.visible: visible && pstatArea.containsMouse
                            ToolTip.text: (pstat===SystemProcessModel.NotResponding) ?
                                              qsTr("Stopped / Dead") : qsTr("Running")
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    Layout.minimumHeight: 30
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: qsTr("Processes:")
                    background: Rectangle {
                        color: tableBackground
                    }
                }

                Label {
                    Layout.minimumWidth: 100
                    Layout.minimumHeight: 30
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: (pcount < 0) ? qsTr("inv") : "<b>%1</b>".arg(pcount)
                    background: Rectangle {
                        color: tableBackground
                    }
                }


                Label {
                    Layout.fillWidth: true
                    Layout.minimumHeight: 30
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: (pmem<0) ? "" : qsTr("Memory:")
                    background: Rectangle {
                        color: tableBackground
                    }
                }

                Label {
                    Layout.minimumWidth: 100
                    Layout.minimumHeight: 30
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: (pmem<0) ? "" : "<b>%1</b>".arg(pmem)
                    background: Rectangle {
                        color: tableBackground
                    }
                }

                Label {
                    Layout.columnSpan: 4
                    Layout.fillWidth: true
                    Layout.minimumHeight: 30

                    text: ppath
                    wrapMode: Text.WrapAnywhere
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter

                    background: Rectangle {
                        color: tableBackground
                    }
                }
            }
        }
    }

    focus: true

    ScrollBar.vertical: ScrollBar {
        id: vBar
    }
}
