import QtQuick 2.10
import QtQuick.Controls 2.3
import QtQuick.Controls.Material 2.2
import QtQuick.Layouts 1.3

import IOTRunner 1.0

Pane {
    id: sensorPage
    state: ""
    property alias label: sensorLabel.text

    function set_value(value) {
        if((loader.source !== '')&&(loader.item)) {
            //console.log('Updating sensor value from '+loader.item.value+' to '+value)
            loader.item.value = value;
        }
    }

    function setup(unit,min,max,ui) {
        if((loader.source !== '')&&(loader.item)) {
            loader.item.unit = unit;
            loader.item.min = min;
            loader.item.max = max;

            for(var key in ui) {
                console.log('Set UI param '+key+" to "+ui[key]);
                loader.item[key] = ui[key];
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 4

        Loader {
            id: loader
            Layout.fillWidth:  true
            Layout.fillHeight:  true
            source: "SensorNone.qml"
        }

        Text {
            id: sensorLabel
            text: ""
            font.pixelSize: 18
            font.bold: true
            color: Material.foreground
            elide: Text.ElideRight

            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }
    }




    states: [
        State {
            name: "text"
            PropertyChanges {
                target: loader
                source: "SensorText.qml"
            }
        },
        State {
            name: "gauge"
            PropertyChanges {
                target: loader
                source: "SensorGauge.qml"
            }
        },
        State {
            name: "lcd"
            PropertyChanges {
                target: loader
                source: "SensorLCD.qml"
            }
        },
        State {
            name: ""
            PropertyChanges {
                target: loader
                source: "SensorNone.qml"
            }
        }

    ]
}
