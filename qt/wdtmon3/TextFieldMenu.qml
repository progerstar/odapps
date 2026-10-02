import QtQuick 2.15
import QtQuick.Controls 2.15
import Qt.labs.platform 1.1

Menu {
    id: contextMenu
    property TextField textField

    MenuItem {
        text: qsTr("C&ut")
        shortcut: StandardKey.Cut
        enabled: textField.selectedText
        onTriggered: textField.cut()
    }
    MenuItem {
        text: qsTr("&Copy")
        shortcut: StandardKey.Copy
        enabled: textField.selectedText
        onTriggered: textField.copy()
    }
    MenuItem {
        text: qsTr("&Paste")
        shortcut: StandardKey.Paste
        enabled: textField.canPaste
        onTriggered: textField.paste()
    }

    MenuSeparator{}

    MenuItem {
        text: qsTr("Select &all")
        shortcut: StandardKey.SelectAll
        enabled: (textField.text!=="")
        onTriggered: textField.selectAll()
    }
}
