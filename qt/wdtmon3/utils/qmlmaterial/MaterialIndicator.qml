import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.12

Label {
    id: control
    property int speed: 500
    property alias bg: bgrect.color
    property string tooltip: ""

    function isActive() {
        return animation.running;
    }

    function start() {
        opacity = 1.0;
        animation.start();
    }

    function stop() {
        animation.stop();
        opacity = 1.0;
    }

    font.family: MaterialFont.fontName
    font.pointSize: 22
    color: Material.primary
    padding: 8

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
    }

    ToolTip.visible: control.tooltip ? area.containsMouse : false
    ToolTip.text: control.tooltip

    background: Rectangle {
        id: bgrect
        radius: width/2
        color: "transparent"
        border.width: 2
        border.color: Material.accent
    }

    SequentialAnimation on opacity {
        id: animation
        running: false
        loops: SequentialAnimation.Infinite // <- cannot update values
        /*onStopped: {
            if(control.animate) {
                start();
            }
        }*/

        PropertyAnimation {
            easing.type: Easing.InQuart;
            to: 0.0
            duration: control.speed
        }
        PropertyAnimation {
            easing.type: Easing.OutQuart;
            to: 1.0
            duration: control.speed
        }

    }
}
