import QtQuick 2.10
import QtQuick.Controls 2.3
import QtQuick.Controls.Material 2.2
import QtQuick.Layouts 1.3

import IOTRunner 1.0

Pane {
    id: logPage

    ColumnLayout {
        id: paneLayout
        anchors.fill: parent
        anchors.margins: 4

        Label {
            id: logTableName
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: runner.logModel().name
        }

        LogList {
            id: logView
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
