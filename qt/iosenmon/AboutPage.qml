import QtQuick 2.10

import QtQuick.Controls 2.3
import QtQuick.Layouts 1.3
import Qt.labs.platform 1.0 as Labs

import ru.opendev.qjournal 1.0

AboutPageForm {
    id: aboutPage

    signal toast(string text);

    Component.onCompleted: {
        pTitle.text = "<br><h3>"+qsTr("IO Sensor Monitor")+" v%1 (%2)</h3>".arg(appversion).arg(__DATE__)+
                     "<br><br>"+qsTr("Open Development LLC")+" ©2019"+
                     "<br><a href=\"https://help.unitx.pro\">help.unitx.pro</a>";

        logSetupText.text = "\u2192&nbsp;<a style=\"text-decoration: none; color: blue;\" href=\"cfg://logging\">" +
                qsTr("Configure") + "</a>"
        logViewText.text = "\u2192&nbsp;<a style=\"text-decoration: none; color: blue;\" href=\"cfg://logview\">" +
                qsTr("Open") + "</a>"
    }

    hideSwitch.onCheckedChanged: {
        settings.write(SETTINGS_UI_VISIBLE,hideSwitch.checked?"false":"true","bool");
    }

    autostartSwitch.onCheckedChanged: {
        autoStarter.setAutostart(autostartSwitch.checked)
    }

    logSwitch.onCheckedChanged: {
        settings.write(SETTINGS_LOG_ENABLE,logSwitch.checked?"true":"false","bool");
        journal.setPaused(logSwitch.checked===false);
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
            if(journal.file!=="" && runner.fileExists(journal.file)) {
                if(Qt.platform.os==="windows") {
                    Qt.openUrlExternally(Qt.resolvedUrl("file:///"+journal.file));
                } else {
                    Qt.openUrlExternally(Qt.resolvedUrl("file://"+journal.file));
                }
            } else {
                noLogDlg.open()
            }
        }
    }

    creditsText.onLinkActivated: {
        Qt.openUrlExternally(link)
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
        }

        Component.onCompleted: {
            logSizeSpin.value = parseInt(settings.read(SETTINGS_LOG_SIZE,"64"))
            logBkpSpin.value = parseInt(settings.read(SETTINGS_LOG_COUNT,"3"))
        }

        onAccepted: {
            settings.write(SETTINGS_LOG_SIZE,"%1".arg(logSizeSpin.value),"int")
            settings.write(SETTINGS_LOG_COUNT,"%1".arg(logBkpSpin.value),"int")
            journal.setMaxSizeMB(logSizeSpin.value)
            journal.setBackupCount(logBkpSpin.value)
        }
    }
}
