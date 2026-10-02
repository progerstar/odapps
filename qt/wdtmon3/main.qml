import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.4
import QtQuick.Extras 1.4
import Qt.labs.platform 1.1
import QtQuick.Controls.Material 2.12
import QtQuick.Controls.Universal 2.12

import QlChannelSerial 1.0
import PingProcess 1.0
import ru.opendev.AutoStarter 2.0
import ru.opendev.ProcessWatcher 2.0
import ru.opendev.SingleApp 2.0
import ru.opendev.QJournal 2.0
import Settings 1.0
import ru.opendev.qmlsystray 1.0

ApplicationWindow {

    id: mainwindow
    width: 580
    height: 580
    maximumWidth: width
    minimumWidth: width
    title: qsTr("USB WatchDog Monitor v%1").arg(appversion)
    property bool first_ack: true
    property bool wait_write: false
    property bool network_ok: true
    property bool process_ok: true
    property string wdg_last_ping: qsTr("n/a")

    Material.theme: darktheme ? Material.Dark : Material.Light;
    Material.accent: Material.Indigo;
    //Material.background: darktheme ? "#030303" : "#fdfdfd";

    onClosing: {
        if(trayIcon.available) {
            close.accepted = false;
            visible = false;
        }
    }

    Component.onCompleted: {
        if(trayIcon.available) {
            mainwindow.visible = (settings.read("visible", "true") === "true");
        } else {
            mainwindow.visible = true;
        }

        page3.hideSwitch.checked = (settings.read("visible","true")==="true") ? false : true;
        AStarter.checkAutostart();
        page3.autostartSwitch.checked = AStarter.autostartSet();
        page3.softwareSwitch.checked = (settings.read("softgl", "false") === "true");
        page3.legacySwitch.checked = (settings.read("legacy", "false") === "true");
        page3.logSwitch.checked = (settings.read("logging/enable", "false") === "true");

        page1.checkBox_l.checked = (settings.read("ledoff", "false") === "false");
        page1.checkBox_p.checked = (settings.read("timerstop", "false") === "true");

        // Compatibility //
        if(settings.read("ping/url", "_default_compability_value_") === "_default_compability_value_") {
            console.log("Converting old URL setting to ping/url")
            if((settings.read("url", "localhost") === "localhost") || (settings.read("url", "localhost") === "")) {
                settings.write("ping/on","false", "bool");
                settings.write("ping/url", "127.0.0.1");
            } else {
                settings.write("ping/on", "true", "bool");
                settings.write("ping/url", settings.read("url", "127.0.0.1"));
            }
            settings.remove("url");
        }

        if((settings.read("ping/on","false") === "true") && (settings.read("ping/url","") !== "") ) {
            page1.urlGroup.enabled = true;
            page1.urlField.text = settings.read("ping/url","");
        } else {
            page1.urlGroup.enabled = false;
            page1.urlField.text = qsTr("disabled");
        }

        page1.monitorField.text = settings.read("monitor", "");
        if((settings.read("monitoron", "false") === "true") && (settings.read("monitor", "") !== "")) {
            page1.monitorGroup.enabled = true;
        }
        page1.manualControl.enabled = (settings.read("manualon", "false") === "true");

        ////Compability with Lite2////
        if(settings.read("legacy", "false") === "true") {
            if(parseInt(settings.read("t1", "5")) > 9) {
                serialCommand("~W9\n");
            } else {
                serialCommand("~W%1\n".arg(settings.read("t1","5")))
            }
        }
    }

    QlChannelSerial{
        id:serial
        onDataReady: {
            console.log("<<"+data);
            processSerialData(data);
            indicator_timer.running = true;
        }
    }

    function serialCommand(cmd) {
        urlmaintimer.running = false;
        if(!serial.isOpen() || (serial.name !== page1.portsCombo.currentText)) {
            serial.open(page1.portsCombo.currentText);
        }

        if(cmd === "~T1") {
            Journal.log(qsTr("Reset requested"));
        } else if(cmd === "~T2") {
            Journal.log(qsTr("Hard reset requested"));
        } else if(cmd === "~M1") {
            Journal.log(qsTr("Remote reset reqested"));
        } else if(cmd === "~M2") {
            Journal.log(qsTr("Remote hard reset requested"));
        } else if(cmd === "~S1") {
            Journal.log(qsTr("Requested channel 1 close"));
        } else if(cmd === "~S2") {
            Journal.log(qsTr("Requested channel 2 close"));
        } else if(cmd === "~R1") {
            Journal.log(qsTr("Requested channel 1 open"));
        } else if(cmd === "~R2") {
            Journal.log(qsTr("Requested channel 2 open"));
        } else if(cmd === "~D") {
            Journal.log(qsTr("Requested firmware upgrade"));
        }

        if((settings.read("legacy", "false") !== "true") && (cmd[0]==='~') && (cmd[1]==='W')) {
            wait_write = true;
            wrtbtn_timer.restart();
        }

        if (serial.isOpen()) {
            /*if(!page1.statusIndicator.isActive()) {
                page1.statusIndicator.start();
            }*/

            serial.writeString(cmd);
            console.log(">>"+cmd)
            if(cmd === "~T2") {
                serial.close();
            }
        } else {
            /*if(page1.statusIndicator.isActive()) {
                page1.statusIndicator.stop();
            }*/

            console.log("!!Cannot write to " + serial.name);
        }

        urlmaintimer.running = true
    }

    function processSerialData(data) {
        var commands = data.split("\n");
        for(var i=0;i<commands.length;++i) {
            processSerialCommand(commands[i]);
        }
    }

    function processSerialCommand(data) {
        if(data.length < 2) {
            //console.log("Reply "+data+" too short")
            return;
        }

        if (data[0] === "~") {
            if (data[1] === "A") {
                page1.statusIndicator.bg = Material.color(Material.LightGreen);
                trayIcon.iconSource = "wdtmon3_g.png"
                if(first_ack || (page1.wdg_type === "")) {
                    if(first_ack === false) {
                        page1.wdg_type = "upd";
                    }

                    first_ack = false;
                    serialCommand("~L" + (page1.checkBox_l.checked ? "1" : "0") + "~P" + (page1.checkBox_p.checked ? "1" : "0") + "~I");
                }
                wdg_last_ping = Qt.formatDateTime(new Date(), "hh:mm:ss");
            }
            else if (data[1] === "F")
            {
                page2.t1Group.enabled = true
                page2.t2Group.enabled = true

                if(data.length===13) {
                    //PRO2
                    page2.t3Group.enabled = true
                    page2.t1Box.currentIndex = parseInt(data[2], 16);
                    page2.t2Box.currentIndex = parseInt(data[3], 16);

                    page2.t3Box.currentIndex = ((parseInt(data[4], 16) > 11) ? 10 : parseInt(data[4], 16));
                    page2.t4Box.currentIndex = parseInt(data[5], 16);
                    page2.t5Box.currentIndex = parseInt(data[6], 16);

                    page2.channelGroup.enabled = true
                    page2.p6Box.currentIndex = parseInt(data[7], 16);
                    page2.p7Box.currentIndex = parseInt(data[8], 16);
                    page2.tempGroup.enabled = true
                    page2.p8Box.currentIndex = parseInt(data[9], 16);
                    page2.p9Box.currentIndex = parseInt(data[10], 16);
                    page2.p10spinBox.value = parseInt(data.slice(11, 13), 16);

                    if(page1.wdg_type !== "T") {
                        page2.writebutton.enabled = true
                    }

                    if(wait_write) {
                        wrtbtn_timer.stop();
                        wait_write = false;
                        page2.writebutton.text = "<font color=\"green\">"+ qsTr("OK") + "</font>"
                        wrtbtn_timer.start();
                    }
                }
                else if(data.length===4) {
                    //LITE
                    page2.t1Box.currentIndex = parseInt(data[2], 16);
                    page2.t2Box.currentIndex = parseInt(data[3], 16);

                    page2.t3Group.enabled = false
                    page2.t3Box.currentIndex = -1;
                    page2.t4Box.currentIndex = -1;
                    page2.t5Box.currentIndex = -1;

                    page2.channelGroup.enabled = false
                    page2.p6Box.currentIndex = 1;
                    page2.p7Box.currentIndex = -1;

                    page2.tempGroup.enabled = false
                    page2.p8Box.currentIndex = -1;
                    page2.p9Box.currentIndex = -1;

                    page2.writebutton.enabled = true

                    if(wait_write) {
                        wrtbtn_timer.stop();
                        wait_write = false;
                        page2.writebutton.text = "<font color=green>"+ qsTr("OK") + "</font>"
                        wrtbtn_timer.start();
                    }
                }
                else {
                    console.log("Unknown settings");
                    page2.writebutton.enabled = false
                }
            }
            else if (data[1] === "L") {
                page1.checkBox_l.checked = (data[2]==="1")?true:false;
            }
            else if (data[1] === "P") {
                page1.checkBox_p.checked = (data[2]==="1")?true:false;
            }
            else if (data[1] === "I") {
                console.log(data.slice(2, 6));
                page1.wdg_version = data.slice(2, 5);
                page1.wdg_version_full = data.slice(2, 18);
                if (data[5] === "U") {
                    let is_toic = (parseFloat(page1.wdg_version) >= 4.0);
                    if(page1.wdg_type !== "upd") {
                        if(is_toic === true) {
                            Journal.log(qsTr("Watchdog ARU P2 (TOIC) v%1 connected").arg(data.slice(2,6)))
                        } else {
                            Journal.log(qsTr("Watchdog Pro2 v%1 connected").arg(data.slice(2,6)))
                        }
                    }
                    page1.wdg_channel = "2";
                    page1.remote_btn_m1.enabled = !is_toic;
                    page1.remote_btn_m2.enabled = !is_toic;
                } else if(data[5] === "L") {
                    if(page1.wdg_type !== "upd") {
                        Journal.log(qsTr("Watchdog Lite2 v%1 connected").arg(data.slice(2,6)))
                    }
                    page1.wdg_channel = "1";
                    page1.remote_btn_m1.enabled = true
                    page1.remote_btn_m2.enabled = false
                } else if(data[5] === 'K') {
                    if(page1.wdg_type !== "upd") {
                        Journal.log(qsTr("Watchdog Khadas v%1 connected").arg(data.slice(2,6)))
                    }
                    page1.wdg_channel = "0";
                    page1.remote_btn_m1.enabled = false
                    page1.remote_btn_m2.enabled = false
                } else {
                    page1.wdg_channel = qsTr("n/a");
                }

                if((data[5] === "U") && (parseFloat(page1.wdg_version) >= 4.0)) {
                    page1.wdg_type = 'T';
                    page2.writebutton.enabled = false;
                } else {
                    page1.wdg_type = data[5];
                }
            }
            else if(data[1] === 'i') {
                var values = data.slice(2).split(";")
                if(values.length===2) {
                    page1.wdg_mcu_vdda = parseInt(values[0])/1000.0
                    page1.wdg_mcu_temp = parseInt(values[1])
                }
            }
            else if (data[1] === "G") {
                if ((data[2] === "E") || (data[2] === "F")){
                    page1.get_in_label.text = qsTr("n/a");
                }else if (data[2] === "I"){
                    page1.get_in_label.text = qsTr("IN")+": "+data[5];
                }else if (data[2] === "O"){
                    page1.get_in_label.text = qsTr("OUT")+": "+data[5];
                }else{
                    page1.get_in_label.text = qsTr("TEMP")+": %1\u2103".arg(parseInt(data.slice(2, 6), 10)/10)
                }
            }
            else if (data[1] === "S") {
                if(data[2] === "1") {
                    page1.ch1Switch.checked = true;
                    page1.ch1Switch.text = qsTr("CH1 Closed");
                }
                else if(data[2] === "2") {
                    page1.ch2Switch.checked = true;
                    page1.ch2Switch.text = qsTr("CH2 Closed");
                }
            }
            else if (data[1] === "R") {
                if(data[2] === "1"){
                    page1.ch1Switch.checked = false;
                    page1.ch1Switch.text = qsTr("CH1 Open");
                }
                else if(data[2] === "2"){
                    page1.ch2Switch.checked = false;
                    page1.ch2Switch.text = qsTr("CH2 Open");
                }
            }
        }
    }

    Timer {
        id:wrtbtn_timer
        interval: 800;
        running: false
        repeat: false
        onTriggered: {
            if(wait_write===true) {
                page2.writebutton.text = "<font color=\"red\">"+qsTr("Failed") + "</font>"
                wrtbtn_timer.start()
            } else {
                page2.writebutton.text = qsTr("Write");
            }
            wait_write = false;
        }
    }

    Timer {
        id: indicator_timer
        interval: 1500;
        running: false;
        repeat: false
        onTriggered: {
            page1.statusIndicator.bg = Material.color(Material.Grey);
            trayIcon.iconSource = "wdtmon3.png"
            serial.close();
        }
    }

    function wdgPing() {
        page1.statusIndicator.bg = Material.color(Material.Orange);
        trayIcon.iconSource = "wdtmon3_y.png"
        if(!serial.isOpen() || (serial.name !== page1.portsCombo.currentText)) {
            serial.open(page1.portsCombo.currentText);
        }
        if (serial.isOpen()) {
            if(page1.wdg_type === "U") {
                serial.writeString("~U~i~G");
            } else if(page1.wdg_type === "T") {
                serial.writeString("~U~G");
            } else {
                serial.writeString("~U");
            }
        } else {
            wdg_last_ping = qsTr("n/a")
            page1.wdg_type = ""
            page1.wdg_version = "0.0"
            page1.wdg_version_full = qsTr("n/a")
            page1.wdg_channel = qsTr("n/a")
            page1.get_in_label.text = qsTr("n/a")
        }
    }

    function networkResult(res) {
        if(res===0) {
            if((network_ok===false)&&(settings.read("logging/network","false")==="true"))
            {
                Journal.log(qsTr("Network access to %1 restored").arg(page1.urlField.text))
            }
            network_ok = true;
        } else {
            if((network_ok===true)&&(settings.read("logging/network","false")==="true"))
            {
                Journal.log(qsTr("Network address %1 is unreachable").arg(page1.urlField.text))
            }
            network_ok = false;
        }
    }

    function processResult(res) {
        if(res===0) {
            if((process_ok===false)&&(settings.read("logging/process","false")==="true"))
            {
                Journal.log(qsTr("Process %1 is running normally").arg(page1.monitorField.text))
            }
            process_ok = true;
        } else {
            if((process_ok===true)&&(settings.read("logging/process","false")==="true"))
            {
                Journal.log(qsTr("Process %1 died or is hanging").arg(page1.monitorField.text))
            }
            process_ok = false;
        }
    }

    PingProcess {
        id: process
        timeout: parseInt(settings.read("ping/timeout","1000"))

        onFinished: {
            var res = exitCode();
            networkResult(res)
            //console.log("ping res: "+output());
            if (res===0) {
                if(page1.monitorGroup.enabled && (page1.monitorField.text !== "")) {
                    PWatcher.start(page1.monitorField.text);
                } else {
                    wdgPing();
                }
            }
        }
    }

    Timer {
        id: urlmaintimer
        interval: 5000;
        repeat: true;
        running: true
        onTriggered: {
            //console.log("start ping "+page1.urlField.text);
            if(page1.urlGroup.enabled) {
                console.log("Ping "+page1.urlField.text);
                process.start(page1.urlField.text);
            } else if(page1.monitorGroup.enabled && (page1.monitorField.text!=="")) {
                PWatcher.start(page1.monitorField.text);
            } else {
                console.log("restart timer")
                wdgPing();
            }
        }
    }

    Connections {
        target: PWatcher
        function onFinished(result) {
            processResult(result)
            if(result === 0) {
                wdgPing()
            }
        }
    }

    StackLayout {
        id: swipeView
        anchors.fill: parent
        width: parent.width
        currentIndex: tabBar.currentIndex
        onCurrentIndexChanged: {
            if (swipeView.currentIndex == 1) {
                if(settings.read("legacy","false")==="true") {
                    page2.t1Box.currentIndex = parseInt(settings.read("t1","5"))

                    page2.readbutton.enabled=false
                    page2.t2Group.enabled = false
                    page2.t2Box.currentIndex = 2
                    page2.t3Group.enabled = false
                    page2.t3Box.currentIndex = -1
                    page2.t4Box.currentIndex = -1
                    page2.t5Box.currentIndex = -1

                    page2.channelGroup.enabled = false
                    page2.p6Box.currentIndex = 1
                    page2.tempGroup.enabled = false
                    page2.writebutton.enabled = true
                } else {
                    page2.readbutton.enabled=true
                    serialCommand("~F");
                }
            }
        }

        Page1 {
            id: page1
        }

        Page2 {
            id: page2
        }

        Page3 {
            id: page3
        }
    }

    header: TabBar {
        id: tabBar
        currentIndex: swipeView.currentIndex
        TabButton {
            text: qsTr("Main")
        }
        TabButton {
            text: qsTr("Settings")
        }
        TabButton {
            text: qsTr("Application")
        }
    }

    footer: Rectangle {
        height: footerLayout.height
        color: Material.background

        RowLayout {
            id: footerLayout
            width: parent.width - 10
            anchors.horizontalCenter: parent.horizontalCenter

            Label {
                text: (((page1.wdg_type === "U") && (parseFloat(page1.wdg_version)>=1.8)) || ((page1.wdg_type === "L") && (parseFloat(page1.wdg_version)>=3.3))) ?
                          qsTr("MCU: %1\u2103, %2V").arg(page1.wdg_mcu_temp).arg(page1.wdg_mcu_vdda) : ""
                font.pointSize: 10
                color: darktheme ? Material.color(Material.Grey) : Material.primary
            }

            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                text: qsTr("Ping: %1").arg(wdg_last_ping)
                font.pointSize: 10
                color: darktheme ? Material.color(Material.Grey) : Material.primary
            }
        }



    }

    Settings {
        id: settings
    }

    Shortcut {
        sequence: (Qt.platform.os==="osx") ? "Meta+Q" : "Ctrl+Q";
        context: Qt.ApplicationShortcut
        onActivated: Qt.quit()
    }

    QmlSystray {
        id: trayIcon
        visible: true
        iconSource: "wdtmon3.png"

        onIconClicked: {
            if(!mainwindow.visible) {
                mainwindow.visible = true
                mainwindow.raise();
            } else {
                mainwindow.visible = false
            }
        }

        Component.onCompleted: {
            trayIcon.addAction(qsTr("Hide"), "hide");
            trayIcon.addAction(qsTr("Show"), "show");
            trayIcon.addSeparator();
            trayIcon.addAction(qsTr("Test Reset"), "T1");
            trayIcon.addAction(qsTr("Test Power"), "T2");
            trayIcon.addSeparator();
            trayIcon.addAction(qsTr("Export Settings"), "ES");
            trayIcon.addAction(qsTr("Import Settings"), "IS");
            trayIcon.addAction(qsTr("View log"), "L");
            trayIcon.addSeparator();
            trayIcon.addAction(qsTr("Firmware Update"), "D");
            trayIcon.addAction(qsTr("About"), "A");
            trayIcon.addAction(qsTr("Quit"), "Q");
        }

        onTriggered: {
            if(id==="hide") {
                mainwindow.visible = false
            } else if(id==="show") {
                mainwindow.visible = true
            } else if(id==="T1") {
                serialCommand("~T1");
            } else if(id==="T2") {
                serialCommand("~T2");
            } else if(id==="ES") {
                fileDialog.ifexport = true;
                fileDialog.open()
            } else if(id==="IS") {
                fileDialog.ifexport = false;
                fileDialog.open()
            } else if(id==="D") {
                serialCommand("~D");
            } else if(id==="A") {
                mainwindow.visible = true
                tabBar.currentIndex = 2
            } else if(id==="L") {
                if(Journal.file !== "") {
                    if(Qt.platform.os==="windows"){
                        Qt.openUrlExternally(Qt.resolvedUrl("file:///"+Journal.file));
                    } else {
                        Qt.openUrlExternally(Qt.resolvedUrl("file://"+Journal.file));
                    }
                }
            } else if(id==="Q") {
                Qt.quit()
            } else if(id==="@osx_dock") {
                mainwindow.visible = !mainwindow.visible
            }
        }
    }

    Connections {
        target: AppServer
        function onIncomingMessage(msg) {
            console.log("Got message from local socket: "+msg)
            if(msg==="show") {
                mainwindow.visible = true;
                mainwindow.raise()
            }
        }
    }

    FileDialog {
        id: fileDialog
        property bool ifexport: true
        fileMode: ifexport ? FileDialog.SaveFile : FileDialog.OpenFile

        folder: StandardPaths.standardLocations(StandardPaths.HomeLocation)[0]
        defaultSuffix: "ini"

        onAccepted: {
            fileDialog.close()
            if(ifexport) {
                if(settings.exportSettings(currentFile) !== true) {
                    warnCloseDialog.text = qsTr("Settings export failed")
                    warnCloseDialog.appclose = false
                    warnCloseDialog.open()
                }
            } else {
                if(settings.importSettings(currentFile)===true) {
                    warnCloseDialog.text = qsTr("Settings have been imported. The application will be closed")
                    warnCloseDialog.appclose = true
                } else {
                    warnCloseDialog.text = qsTr("Settings import failed")
                    warnCloseDialog.appclose = false
                }
                warnCloseDialog.open()
            }
        }

        onRejected: {
            fileDialog.close()
        }
    }

    MessageDialog {
        property bool appclose: false

        id: warnCloseDialog
        title: qsTr("Settings Import")
        text: ""
        buttons: MessageDialog.Ok

        onOkClicked: {
            if(appclose) {
                Qt.quit()
            }
        }
    }
}
