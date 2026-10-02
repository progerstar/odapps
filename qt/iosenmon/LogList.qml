import QtQuick 2.10
import QtQuick.Layouts 1.3
import QtQuick.Controls 2.3
import QtQuick.Controls.Material 2.2

import IOTRunner 1.0
import HidSensorInterface 1.0


ListView {
    id: logView

    model: runner.logModel()
    spacing: 0
    Layout.fillWidth: true
    Layout.fillHeight: true
    Layout.margins: 2
    clip: true

    Rectangle {
        anchors.fill: parent
        z: -1
        color: IOStyle.cardColor
        border.color: IOStyle.cardBorder
        radius: IOStyle.cardRadius
    }

    FontMetrics {
        id: metric;
    }

    delegate: Item {
        id: wrapper

        width: logView.width
        height: rect.height

        Rectangle {
            id: rect
            width: logView.width
            height: delegatelayout.height+4
            border.width: 0
            radius: 0
            color: (index % 2 === 0) ? IOStyle.cardColor : IOStyle.pageColor

            RowLayout {
                id: delegatelayout
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 4
                anchors.rightMargin: 14
                anchors.topMargin: 2
                anchors.bottomMargin: 2

                spacing: 2

                Label {
                    id: idField
                    Layout.minimumWidth: Math.round(4 * metric.averageCharacterWidth)
                    color: IOStyle.textSecondary
                    text: "%1".arg(index + 1)
                }

                Label {
                    id: timeField
                    Layout.fillWidth: true
                    text: ltime
                }

                Label {
                    id: dataField
                    font.bold: true
                    text: ldata
                    Layout.minimumWidth: Math.round(logView.width*0.3)
                }
            }
        }
    }

    ScrollBar.vertical: ScrollBar {
        width: 10
    }
}
