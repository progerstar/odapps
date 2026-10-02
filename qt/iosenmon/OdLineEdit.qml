import QtQuick 2.10
import QtQuick.Controls 2.3
import Qt.labs.platform 1.0 as QLabs

import QtQuick.Controls.Material 2.2

TextField {
    id: lineEdit
    height: 40
    background: Rectangle {
        implicitWidth: 227
        implicitHeight: 40
        color: "white"
        border.color: parent.activeFocus ? Material.primary : Material.accent
        border.width: parent.activeFocus ? 2 : 1
    }

    placeholderText: "..."
    text: ""
    leftPadding: 8
    bottomPadding: 2
    selectByMouse: true

    MouseArea {
        id: contextArea
        anchors.fill: parent
        acceptedButtons: Qt.RightButton

        onClicked: {
            contextMenu.open()
        }
    }

    QLabs.Menu {
        id: contextMenu

        QLabs.MenuItem {
            text: qsTr("C&ut")
            shortcut: StandardKey.Cut
            enabled: lineEdit.selectedText
            onTriggered: lineEdit.cut()
        }
        QLabs.MenuItem {
            text: qsTr("&Copy")
            shortcut: StandardKey.Copy
            enabled: lineEdit.selectedText
            onTriggered: lineEdit.copy()
        }
        QLabs.MenuItem {
            text: qsTr("&Paste")
            shortcut: StandardKey.Paste
            enabled: lineEdit.canPaste
            onTriggered: lineEdit.paste()
        }

        QLabs.MenuSeparator{}

        QLabs.MenuItem {
            text: qsTr("Select &all")
            shortcut: StandardKey.SelectAll
            enabled: (lineEdit.text!=="")
            onTriggered: lineEdit.selectAll()
        }
    }
}
