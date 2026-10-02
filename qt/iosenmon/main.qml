import QtQuick 2.10
import QtQuick.Window 2.3
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.3

import Qt.labs.platform 1.0 as Labs
import QtQuick.Extras 1.4

import ru.opendev.QMLSettings 1.0
import ru.opendev.AutoStarter 1.0
import ru.opendev.qmlsystray 1.0
import ru.opendev.qjournal 1.0

import QtQuick.Controls.Material 2.2
import QtQuick.Controls.Universal 2.1

import IOTRunner 1.0
import HidSensorInterface 1.0

ApplicationWindow {

    id: mainwindow
    width: 640
    height: 480
    //maximumHeight: height
    //maximumWidth: width
    //minimumHeight: height
    //minimumWidth: width
    title: qsTr("IOT Sensor Monitor v%1").arg(appversion)

    color: IOStyle.pageColor

    Material.theme: Material.Light;
    Material.primary: Material.Indigo;
    Material.accent: Material.Teal;
    //Material.foreground: "black";
    //Material.background: "#f5f5f5";

    onClosing: {
        if(trayIcon.available) {
            close.accepted = false;
            visible = false;
        }
    }

    Component.onCompleted: {
        if(trayIcon.available) {
            mainwindow.visible = (settings.read(SETTINGS_UI_VISIBLE,"true")==="true");
        } else {
            mainwindow.visible = true;
        }

        page4.hideSwitch.checked = (settings.read(SETTINGS_UI_VISIBLE,"true")==="true")?false:true;
        autoStarter.checkAutostart();
        page4.autostartSwitch.checked = autoStarter.autostartSet();
        page4.logSwitch.checked = (settings.read(SETTINGS_LOG_ENABLE,"false")==="true");

        rescanTimer.interval = parseInt(settings.read(SETTINGS_RESCAN_TIMEOUT,"5000"));
        runner.scan()
    }

    function showSensor(obj) {
        updateSensorView(obj)
        drawer.close()
        swipeView.currentIndex = 0
    }

    //(re)configure the sensor page without navigating away
    function updateSensorView(obj) {
        page1.state = ""
        page1.label = ""
        if(obj) {
            switch(obj.uiClass()) {
            case HidSensorInterface.LCD:
                page1.state = "lcd";
                break;
            case HidSensorInterface.Gauge:
                page1.state = "gauge";
                break;
            case HidSensorInterface.Text:
            default:
                page1.state = "text";
                break;
            }
            page1.label = runner.alias(obj.serial());
            page1.setup(obj.unit(),obj.minimumValue,obj.maximumValue,obj.setupUI());
            page1.set_value(obj.value);
        }
    }

    ToastManager {
        id: toast
        width: Math.min(300,Math.round(mainwindow.width*0.3))
    }

    header: ToolBar {
        contentHeight: drawerButton.implicitHeight+2

        background: Rectangle {
            color: Material.primary
        }

        MaterialToolButton {
            id: drawerButton
            text: MaterialFont.icon.menu
            onClicked: {
                if(drawer.visible) {
                    drawer.close()
                } else {
                    drawer.open()
                }
            }

            hoverEnabled: true
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Sensor list")
        }

        RowLayout {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter

            /*
            MaterialToolButton {
                id: dfuButton
                visible: (runner.sensor !== null)
                text: MaterialFont.icon.build
                onClicked: {
                    runner.sensor.upgrade();
                }

                hoverEnabled: true
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Update firmware of the current sensor")
            }
            */

            MaterialToolButton {
                id: configButton
                visible: ((swipeView.currentIndex === 0) && (page1.state !== ''))
                text: MaterialFont.icon.build
                onClicked: {
                    var cfgDialogString = runner.sensor.settingsDialog();
                    if(cfgDialogString === "") {
                        toast.show(qsTr("This sensor has no configurable settings"));
                        return;
                    }
                    //console.log(runner.sensor.type()+' settings dialog: '+cfgDialogString)
                    var cfgDialog = Qt.createQmlObject(cfgDialogString,page1,'CustomSettingsDialog.qml')
                    var currentCfg = runner.sensor.readSettings();
                    console.log(runner.sensor.type()+'-'+runner.sensor.serial()+' config: '+JSON.stringify(currentCfg));
                    cfgDialog.parse(currentCfg);
                    cfgDialog.width = page1.width*0.8;
                    cfgDialog.height = page1.height
                    cfgDialog.x = page1.width*0.1
                    cfgDialog.open();
                }

                hoverEnabled: true
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Configure the current sensor")
            }

            MaterialToolButton {
                id: exportTool
                visible: (swipeView.currentIndex === 1)
                text: MaterialFont.icon.archive

                hoverEnabled: true
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Export to file")

                onClicked: {
                    if(runner.sensor !== null) {
                        if(Qt.platform.os==="osx") {
                            exportDBDialog.folder = settings.read(SETTINGS_DB_SAVEPATH,Labs.StandardPaths.standardLocations(Labs.StandardPaths.HomeLocation)[0])
                            console.log('ExportDialog folder set to '+exportDBDialog.folder);
                            console.log('Suggest file '+runner.suggestLogPath());
                            exportDBDialog.currentFile = Qt.resolvedUrl("file://"+runner.suggestLogPath());
                        }
                        exportDBDialog.open()
                    }
                }
            }

            MaterialToolButton {
                id: saveTool
                visible: false
                text: MaterialFont.icon.save

                hoverEnabled: true
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Save Changes")

                onClicked: {
                    page3.writeSettings()
                }
            }

            MaterialToolButton {
                id: scanButton
                text: MaterialFont.icon.search
                onClicked: {
                    runner.scan()
                }

                hoverEnabled: true
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Rescan connected sensors")
            }
        }
    }

    Timer {
        id: rescanTimer
        repeat: true
        running: true
        interval: 5000

        onTriggered: {
            runner.scan()
        }
    }

    Timer {
        id: saveToolFlash
        repeat: true
        running: false
        interval: 250

        onTriggered: {
            if(!saveTool.hovered) {
                saveTool.visible = !saveTool.visible
            } else {
                saveTool.visible = true
            }
        }
    }

    Drawer {
        id: drawer
        y: header.height
        width: mainwindow.width * 0.45
        height: mainwindow.height - header.height

        ColumnLayout {
            anchors.fill: parent

            SensorList {
                id: sensorView
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 2
            }

            RowLayout {
                Layout.fillWidth: true

                MaterialToolButton {
                    id: logTool
                    text: MaterialFont.icon.library_books
                    normalColor: Material.primary

                    hoverEnabled: true
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Open Log View")

                    onClicked: {
                        drawer.close()
                        swipeView.currentIndex = 1
                    }
                }

                MaterialToolButton {
                    id: settingsTool
                    text: MaterialFont.icon.settings
                    normalColor: Material.primary

                    hoverEnabled: true
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Settings")

                    onClicked: {
                        drawer.close()
                        page3.readSettings();
                        //will emit the changed signal - reset
                        saveToolFlash.running = false;
                        saveTool.visible = false;
                        swipeView.currentIndex = 2;
                    }
                }

                MaterialToolButton {
                    id: aboutTool
                    text: MaterialFont.icon.info
                    normalColor: Material.primary

                    hoverEnabled: true
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("About")

                    onClicked: {
                        drawer.close()
                        swipeView.currentIndex = 3
                    }
                }

                MaterialToolButton {
                    id: quitTool
                    text: MaterialFont.icon.exit_to_app
                    normalColor: Material.primary

                    hoverEnabled: true
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Quit")

                    onClicked: {
                        Qt.quit();
                    }
                }
            }
        }
    }

    StackLayout {
        id: swipeView
        anchors.fill: parent
        width: parent.width
        currentIndex: 0

        SensorPage
        {
            id: page1
        }

        LogPage
        {
            id: page2
        }

        SettingsPage
        {
            id: page3

            onChanged: {
                if(swipeView.currentIndex === 2) {
                    console.log('Settings have been changed!!!')
                    saveToolFlash.running = true
                    saveTool.normalColor = Material.color(Material.DeepOrange)
                }
            }
            onSaved: {
                //units might have changed
                rescanTimer.interval = parseInt(settings.read(SETTINGS_RESCAN_TIMEOUT,"5000"))
                if(runner.sensor) {
                    mainwindow.updateSensorView(runner.sensor)
                }
                saveToolFlash.running = false
                saveTool.visible = false
                saveTool.normalColor = "#f5f5f5"
            }
        }

        AboutPage
        {
            id: page4

            onToast: {
                toast.show(text);
            }
        }

        onCurrentIndexChanged: {
            if(saveToolFlash.running && (currentIndex!= 2)) {
                saveToolFlash.running = false
                saveTool.visible = true
                saveSettingsDialog.targetIndex = currentIndex;
                currentIndex = 2;
                saveSettingsDialog.open();
            }
        }
    }

    Settings {
        id: settings
    }

    QmlSystray {
        id: trayIcon
        visible: true
        iconSource: "iosenmon.png"

        onIconClicked: {
            if(!mainwindow.visible) {
                mainwindow.visible = true
                mainwindow.raise();
            } else {
                mainwindow.visible = false
            }
        }

        Component.onCompleted: {
            trayIcon.addAction(qsTr("Hide"),"hide");
            trayIcon.addAction(qsTr("Show"),"show");
            trayIcon.addSeparator();
            trayIcon.addAction(qsTr("View log"),"L");
            trayIcon.addSeparator();
            trayIcon.addAction(qsTr("Firmware Update"),"D");
            trayIcon.addAction(qsTr("About"),"A");
            trayIcon.addAction(qsTr("Quit"),"Q");
        }

        onTriggered: {
            if(id==="hide") {
                mainwindow.visible = false
            } else if(id==="show") {
                mainwindow.visible = true
            } else if(id==="D") {
                upgradeConfirm.open()
            } else if(id==="A") {
                mainwindow.visible = true
                drawer.close()
                swipeView.currentIndex = 3
            } else if(id==="L") {
                if(journal.file !== "") {
                    if(Qt.platform.os==="windows"){
                        Qt.openUrlExternally(Qt.resolvedUrl("file:///"+journal.file));
                    } else {
                        Qt.openUrlExternally(Qt.resolvedUrl("file://"+journal.file));
                    }
                }
            } else if(id==="Q") {
                Qt.quit()
            } else if(id==="@osx_dock") {
                mainwindow.visible = !mainwindow.visible
            }
        }
    }

    Dialog {
        id: upgradeConfirm
        title: qsTr("Firmware Upgrade")
        standardButtons: Dialog.Ok | Dialog.Cancel
        modal: true
        anchors.centerIn: parent
        width: Math.min(mainwindow.width * 0.9, 460)

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: runner.sensor ? qsTr("Are you sure you want to update the firmware?")+
                                  "<br>"+
                                  qsTr("Sensor to be updated: ")+"<br>%1 (%2)<br>".arg(runner.sensor.serial()).arg(runner.sensor.type())+
                                  qsTr("This is a potentially dangerous operation that may render the device inoperable.")+
                                  "<br>"+
                                  qsTr("Please read the sensor manual carefully before updating the firmware!") :
                                  qsTr("Cannot update the firmware - please select a device first.")
        }

        onOpened: {
            standardButtons = runner.sensor ? Dialog.Ok | Dialog.Cancel : Dialog.Cancel
        }

        onAccepted: {
            close();
            if(runner.sensor) {
                runner.sensor.upgrade();
            }
        }

        onRejected: {
            close();
        }
    }

    Dialog {
        id: saveSettingsDialog
        property int targetIndex: 0
        title: qsTr("Settings changed")
        standardButtons: Dialog.Ok | Dialog.Discard
        modal: true
        anchors.centerIn: parent

        Text {
            text: qsTr("Settings have been changed")+"<br>"
                  +qsTr("Save changes?")
        }

        onAccepted:  {
            page3.writeSettings()
        }

        onDiscarded: {
            close();
        }

        onClosed: {
            saveToolFlash.running = false
            saveTool.visible = false
            saveTool.normalColor = "#f5f5f5"
            swipeView.currentIndex = targetIndex
        }
    }

    Labs.FileDialog {
        id: exportDBDialog

        folder: Labs.StandardPaths.standardLocations(Labs.StandardPaths.HomeLocation)[0]
        fileMode: Labs.FileDialog.SaveFile
        defaultSuffix: "txt"

        onVisibleChanged: {
            if(visible) {
                if(Qt.platform.os==="windows") {
                    exportDBDialog.folder = Qt.resolvedUrl("file:///"+settings.read(SETTINGS_DB_SAVEPATH,Labs.StandardPaths.standardLocations(Labs.StandardPaths.HomeLocation)[0])+'/')
                    console.log('ExportDialog folder set to '+exportDBDialog.folder);
                    console.log('Suggest file '+runner.suggestLogPath());
                    exportDBDialog.currentFile = "file:///"+runner.suggestLogPath();
                } else if(Qt.platform.os!=="osx") {
                    exportDBDialog.folder = Qt.resolvedUrl("file://"+settings.read(SETTINGS_DB_SAVEPATH,Labs.StandardPaths.standardLocations(Labs.StandardPaths.HomeLocation)[0])+'/')
                    exportDBDialog.currentFile = Qt.resolvedUrl("file://"+runner.suggestLogPath());
                }
                console.log('Suggest filename '+exportDBDialog.currentFile)
            }
        }

        onAccepted: {
            exportDBDialog.close()
            runner.logExport(currentFile)
        }

        onRejected: {
            exportDBDialog.close()
        }
    }

    Connections {
        target: appServer
        onIncomingMessage: {
            console.log("Got message from local socket: "+msg)
            if(msg==="show") {
                mainwindow.visible = true;
                mainwindow.raise()
            }
        }
    }

    Connections {
        target: sensorView
        onDisplay: {
            console.log('Drawer requested '+type+'-'+sn);
            runner.select(type,sn);
        }
    }

    Connections {
        target: runner
        onSensorChanged: {
            mainwindow.showSensor(obj)
        }
        onDataChanged: {
            //console.log('Current sensor data changed to '+value);
            page1.set_value(value);
        }
        onStateChanged: {

        }
        onError: {
            toast.show(descr)
        }
        onAliasChanged: {
            if ((runner.sensor !== null) && (runner.sensor.serial()===serial)) {
                page1.label = runner.alias(serial);
            }
        }
    }

    Shortcut {
        sequence: StandardKey.Quit
        context: Qt.ApplicationShortcut
        onActivated: mainwindow.close()
    }
}

