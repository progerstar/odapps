import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.12

/*
Rectangle {
    id: wrapper

    property alias text: control.text
    property alias echoMode: control.echoMode
    property alias font: control.font
    property alias control: control
    property bool menu: false
    property alias menuArea: fieldMenu

    implicitWidth: 200
    height: control.height + 2
    color: "transparent"
    border.color: control.enabled ? Material.primary : Qt.darker(Material.foreground)

    signal editingFinished();

    TextField {
        id: control

        anchors.left: parent.left
        anchors.leftMargin: 5
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        background: Rectangle {
            color: "transparent"
        }

        selectByMouse: menu;

        MouseArea {
            id: fieldMenu
            anchors.fill: parent
            acceptedButtons: menu ? Qt.RightButton : Qt.NoButton;
        }

        onEditingFinished: wrapper.editingFinished();
    }
}*/

TextField {
    id: control
    property alias menuArea: menuArea

    /*background: Rectangle {
        color: "transparent"
    }*/

    selectByMouse: true;

    MouseArea {
        id: menuArea
        anchors.fill: parent
        acceptedButtons: Qt.RightButton;
    }
}
