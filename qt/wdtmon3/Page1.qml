import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.12

import ru.opendev.systemprocess 1.0
import ru.opendev.QJournal 2.0

Page1Form {

    function typeName(wtyp) {
        if(wtyp === "U") {
            return "Pro2";
        } else if(wtyp === "L") {
            return "Lite";
        } else if(wtyp === "K") {
            return "Khadas";
        } else if(wtyp === "T") {
            return "ARU (TOIC)";
        }

        return qsTr("n/a");
    }

    Component.onCompleted: {
        if (Qt.platform.os==="windows") {
            monitorField.placeholderText = "explorer.exe";
        } else if(Qt.platform.os==="osx") {
            monitorField.placeholderText = "kernel_task";
        } else {
            monitorField.placeholderText = "nautilus";
        }

        statusIndicator.bg = Material.color(Material.DeepOrange);
        pingTimeoutSlider.value = parseInt(settings.read("ping/timeout","1000"));

        remote_btn_m1.enabled = false;
        remote_btn_m2.enabled = false;

        console.log("Available ports: "+serial.channels());
        console.log("Restoring port: "+settings.read("port"));

        var avail_ports = serial.channels();
        var saved_port = settings.read("port");
        var port_restored = false;
        for (let i = 0; i < avail_ports.length; i++) {
            if(avail_ports[i]===saved_port) {
                port_restored = true
                portsCombo.model = [ avail_ports[i] ]
                break;
            }
        }
        if (!port_restored) {
            portsCombo.model = serial.channels();
            if(serial.channels().length === 0) {
                Journal.log(qsTr("No connected watchdogs detected"));
                rescanTimer.running = true;
            }
        }
    }

    scanbtn.onClicked: {
        portsCombo.model = serial.channels();
        if(serial.channels().length !== 0) {
            rescanTimer.running = false;
        }
    }

    urlGroup.check.onCheckStateChanged: {
        if(!urlGroup.enabled) {
            settings.write("ping/on","false","bool");
            urlField.text = qsTr("disabled");
        } else {
            settings.write("ping/on","true","bool");
            urlField.text = settings.read("ping/url","127.0.0.1");
        }
    }

    urlField.menuArea.onClicked: {
        urlContextMenu.open()
    }

    urlField.onEditingFinished: {
        if(urlGroup.enabled && (urlField.text !== "")) {
            settings.write("ping/on","true","bool");
            settings.write("ping/url", urlField.text);
        }
    }

    monitorGroup.check.onCheckStateChanged: {
        settings.write("monitoron", monitorGroup.enabled ? "true" : "false", "bool");
    }

    monitorField.onTextChanged: {
        settings.write("monitor",monitorField.text);
    }

    monitorField.menuArea.onClicked: {
        monitorContextMenu.open()
    }

    processListTool.onClicked: {
        selectProcessDialog.prepare();
        selectProcessDialog.open();
    }

    manualControl.check.onCheckStateChanged: {
        settings.write("manualon", manualControl.enabled ? "true" : "false", "bool");
    }

    checkBox_l.onClicked: {
        serialCommand("~L" + ((checkBox_l.checked)?"1":"0"));
        settings.write("ledoff",checkBox_l.checked?"false":"true","bool");
    }

    checkBox_p.onClicked: {
        serialCommand("~P" + ((checkBox_p.checked)?"1":"0"));
        settings.write("timerstop",checkBox_p.checked?"true":"false","bool");
    }

    portsCombo.onCurrentTextChanged: {
        urlmaintimer.running = false;
        serial.open(page1.portsCombo.currentText);
        first_ack=true
        wdg_type=""
        if (serial.isOpen()) {
            console.log("Opened port "+page1.portsCombo.currentText);
            settings.write("port", page1.portsCombo.currentText);
            if(settings.read("legacy","false")==="true") {
                if(parseInt(settings.read("t1","5"))>9) {
                    serialCommand("~W9\n~I");
                } else {
                    serialCommand("~W%1\n~I".arg(settings.read("t1","5")))
                }
            }
        } else {
            console.log("Cannot open port "+page1.portsCombo.currentText)
        }

        urlmaintimer.running = true
    }

    remote_btn_m1.onClicked: {
        if(wdg_type==="U") {
            serialCommand("~M1");
        } else {
            serialCommand("~T1");
        }
    }

    remote_btn_m2.onClicked: {
        if(wdg_type==="U") {
            serialCommand("~M2");
        }
    }

    ch1Switch.onCheckedChanged: {
        if (ch1Switch.checked) {
            serialCommand("~S1");
        } else {
            serialCommand("~R1");
        }
    }

    ch2Switch.onCheckedChanged: {
        if (ch2Switch.checked) {
            serialCommand("~S2");
        } else {
            serialCommand("~R2");
        }
    }

    label_info {
        text: (wdg_type==="") ? qsTr("Disconnected") : (qsTr("%1 v%2, %3 chn")
                                                        .arg(typeName(wdg_type)).arg(wdg_version_full).arg(wdg_channel))
    }

    pingTimeoutLabel {
        color: urlGroup.enabled ? Material.foreground : Material.background
    }

    pingTimeoutSlider {
        bg: urlGroup.enabled ? Material.primary : Material.background
        color: urlGroup.enabled ? Material.accent : Material.background
    }

    pingTimeoutText {
        text: "%1".arg(pingTimeoutSlider.value)+qsTr("ms")
        color: urlGroup.enabled ? Material.foreground : Material.background
    }

    pingTimeoutSlider.onValueChanged: {
        settings.write("ping/timeout","%1".arg(pingTimeoutSlider.value),"int");
        process.timeout = pingTimeoutSlider.value;
    }


    TextFieldMenu {
        id: urlContextMenu
        textField: urlField
    }

    TextFieldMenu {
        id: monitorContextMenu
        textField: monitorField
    }

    Timer {
        id: rescanTimer
        running: false
        repeat: true
        interval: 10000
        onTriggered: {
            console.log("Rescan triggered")
            scanbtn.onClicked()
        }
    }

    Dialog {
        id: selectProcessDialog
        modal: true
        title: qsTr("Select a Process")

        x: (parent.width - width) / 2
        y: 10
        width: Math.round(parent.width*0.9)
        height: parent.height-20

        function prepare() {
            bHolder.running = true;
            procList.sysProcModel.reload();
        }

        BusyIndicator {
            id: bHolder
            running: true
            anchors.centerIn: parent
            width: parent.width/2
            height: parent.height/2
        }

        SystemProcessList {
            id: procList
            anchors.fill: parent
            clip: true
            tableBackground: darktheme ? Qt.lighter(Material.background) : Qt.darker(Material.background)

            sysProcModel.onReloaded: {
                console.log("Process model reloaded");
                bHolder.running = false;
            }

            onSelectProcess: {
                monitorField.text = name;
                selectProcessDialog.close()
            }
        }

        standardButtons: Dialog.Cancel
    }
}
