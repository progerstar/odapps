import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.12

import QtQuick.Layouts 1.4
import Qt.labs.platform 1.1 as Labs


import ru.opendev.QJournal 2.0
import ru.opendev.AutoStarter 2.0
import ru.opendev.ProcessWatcher 2.0

Page3Form {

    text1 {
        text: "<br><h3>"+qsTr("WatchDog Monitor")+" v%1 (%2)</h3>".arg(appversion).arg(__DATE__)+
              "<br><br>"+qsTr("Open Development LLC") + " ©2018-2021" +
              "<br><a href=\"https://unitx.pro\">unitx.pro</a>";
        onLinkActivated: {
            console.log("Open " + link);
            Qt.openUrlExternally(link);
        }
    }

    Component.onCompleted: {
        if(lang_current==="" || lang_current.toLowerCase()==="system") {
            langCombo.currentIndex = 0
        } else if(lang_current.toLowerCase().indexOf("ru")===0) {
            langCombo.currentIndex = 2
        } else {
            langCombo.currentIndex = 1
        }

        logSetupText.text = "\u2192&nbsp;<a style=\"text-decoration: none; color: " + Material.primary + ";\" href=\"cfg://logging\">" +
                qsTr("Configure") + "</a>"
        logViewText.text = "\u2192&nbsp;<a style=\"text-decoration: none; color: " + Material.primary + ";\" href=\"cfg://logview\">" +
                qsTr("Open") + "</a>"

    }

    hideSwitch.onCheckedChanged: {
        settings.write("visible",hideSwitch.checked?"false":"true","bool");
    }

    autostartSwitch.onCheckedChanged: {
        AStarter.setAutostart(autostartSwitch.checked)
    }

    softwareSwitch.onCheckedChanged: {
        settings.write("softgl",softwareSwitch.checked?"true":"false","bool");
        if(softwareSwitch.checked !== software_rendering) {
            langWarn.color = "red"
            langWarn.text = qsTr("Restart the app to change rendering method");
        } else {
            langWarn.text = ""
        }
    }

    legacySwitch.onCheckedChanged: {
        settings.write("legacy",legacySwitch.checked?"true":"false","bool");
    }

    logSwitch.onCheckedChanged: {
        settings.write("logging/enable",logSwitch.checked?"true":"false","bool");
        Journal.setPaused(logSwitch.checked===false);
    }

    logSetupText.onLinkActivated: {
        if(link=="cfg://logging"){
            logSetupDialog.open()
        } else {
            console.log("LogSetupText invalid link "+link);
        }
    }

    logViewText.onLinkActivated: {
        if((link=="cfg://logview")) {
            if(Journal.file!=="" && PWatcher.fileExists(Journal.file)) {
                if(Qt.platform.os==="windows") {
                    Qt.openUrlExternally(Qt.resolvedUrl("file:///"+Journal.file));
                } else {
                    Qt.openUrlExternally(Qt.resolvedUrl("file://"+Journal.file));
                }
            } else {
                noLogDlg.open()
            }
        }
    }

    langCombo.onActivated: {
        var selectedLanguage = (index===0) ? "" : ((index===2) ? "ru" : "en")
        var currentLanguage = lang_current.toLowerCase()
        if(currentLanguage==="system") {
            currentLanguage = ""
        } else if(currentLanguage.indexOf("ru")===0) {
            currentLanguage = "ru"
        } else if(currentLanguage.indexOf("en")===0) {
            currentLanguage = "en"
        }
        settings.write(lang_key, selectedLanguage, "string")
        if(selectedLanguage!==currentLanguage) {
            langWarn.color = "red";
            langWarn.text = qsTr("Restart the app to change language");
        } else {
            langWarn.text = "";
        }
    }

    themeCombo.onActivated: {
        if(Qt.platform.os !== "osx") {
            settings.write(theme_key, (index === 1)?"true":"false", "bool");
            langWarn.color = "red";
            langWarn.text = qsTr("Restart the app to change the theme");
        }
    }

    Labs.MessageDialog {
        id: noLogDlg
        text: qsTr("Log file not found!")
        buttons: Labs.MessageDialog.Ok
    }

    Dialog {
        id: logSetupDialog
        title: qsTr("Setup Logging")
        standardButtons: Dialog.Ok | Dialog.Cancel
        modal: true

        GridLayout {
            anchors.fill: parent
            anchors.rightMargin: 10
            id: lsdlgGrid
            columns: 2

            Label {text: qsTr("Maximum Size (Mb):")}
            SpinBox {
                id: logSizeSpin;
                Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                Layout.fillWidth: false
                from: 1
                to: 1024
                value: 64
                editable: true
            }

            Label {text: qsTr("Old files:")}
            SpinBox {
                id: logBkpSpin;
                Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                Layout.fillWidth: false
                from: 1
                to: 16
                value: 3
                editable: true
            }

            Switch {
                id: logNetSwitch
                text: qsTr("Network state")
            }

            Switch {
                id: logProcSwitch
                text: qsTr("Process state")
            }
        }

        Component.onCompleted: {
            logSizeSpin.value = parseInt(settings.read("logging/size","64"))
            logBkpSpin.value = parseInt(settings.read("logging/count","3"))
            logNetSwitch.checked = (settings.read("logging/network","false")==="true")
            logProcSwitch.checked = (settings.read("logging/process","false")==="true")
        }

        onAccepted: {
            settings.write("logging/size","%1".arg(logSizeSpin.value),"int")
            settings.write("logging/count","%1".arg(logBkpSpin.value),"int")
            settings.write("logging/network",logNetSwitch.checked ? "true" : "false","bool")
            settings.write("logging/process",logProcSwitch.checked ? "true" : "false","bool")
            Journal.setMaxSizeMB(logSizeSpin.value)
            Journal.setBackupCount(logBkpSpin.value)
        }
    }
}
