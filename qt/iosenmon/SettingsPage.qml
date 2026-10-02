import QtQuick 2.10
import QtQuick.Layouts 1.3
import QtQuick.Controls 2.3
import QtQuick.Controls.Material 2.2

Pane {
    id: settingsPage

    signal changed()
    signal saved()

    function readSettings() {
        dbLocInput.text = settings.read(SETTINGS_DB_LOC,"");
        dbAgeSlider.init(parseInt(settings.read(SETTINGS_DB_AGE,"1296000")));
        siTempSelector.checked = (settings.read(SETTINGS_UNITS_TEMP,"C")==="F");
        langCombo.currentIndex = (lang_current==="ru") ? 1 : 0;
        softwareSwitch.checked = (settings.read(SETTINGS_SOFTGL,"false")==="true");
        rescanSpin.value = parseInt(settings.read(SETTINGS_RESCAN_TIMEOUT,"5000"))
        httpSwitch.checked = (settings.read(SETTINGS_HTTP_ENABLED,"false")==="true");
        httpPortSpin.value = parseInt(settings.read(SETTINGS_HTTP_PORT,"34242"));
    }

    function writeSettings() {
        settings.write(SETTINGS_DB_LOC,dbLocInput.text,"string");
        settings.write(SETTINGS_DB_AGE,dbAgeSlider.time,"int");
        settings.write(SETTINGS_UNITS_TEMP,siTempSelector.currentRole,"string");
        settings.write(SETTINGS_RESCAN_TIMEOUT,rescanSpin.value,"int");
        settings.write(SETTINGS_HTTP_ENABLED,httpSwitch.checked?"true":"false","bool");
        settings.write(SETTINGS_HTTP_PORT,"%1".arg(httpPortSpin.value),"int");

        langWarn.text = "";
        settings.write(lang_key,(langCombo.currentIndex===1) ? "ru" : "en","string");
        if(((lang_current==="en")&&(langCombo.currentIndex===1)) ||
                ((lang_current==="ru")&&(langCombo.currentIndex===0))) {
            langWarn.color = "red";
            langWarn.text += qsTr("Restart the app to change language");
        }

        settings.write(SETTINGS_SOFTGL,softwareSwitch.checked ? "true":"false","bool");
        if(softwareSwitch.checked !== software_rendering) {
            langWarn.color = "red";
            langWarn.text += "<br>"+ qsTr("Restart the app to change rendering method");
        }

        runner.settingsUpdated();
        settingsPage.saved();
    }

    Flickable {
        clip: true
        anchors.fill: parent
        contentHeight: pane.height

        Pane {
            id: pane
            width: parent.width

            ColumnLayout {
                id: paneLayout
                anchors.fill: parent

                GroupBox {
                    Layout.fillWidth: true
                    id: dbBox
                    title: qsTr("Database")

                    GridLayout {
                        id: dbBoxLayout
                        anchors.fill: parent
                        columns: 2

                        Label {
                            text: qsTr("File")+":"
                        }

                        OdLineEdit {
                            id: dbLocInput
                            placeholderText: qsTr("database file")
                            text: ""
                            Layout.fillWidth: true

                            onTextChanged: {
                                settingsPage.changed()
                            }
                        }

                        Label {
                            text: qsTr("Max. Age")+":"
                        }

                        TimeSlider {
                            id: dbAgeSlider
                            timeFrom: 3600
                            timeTo: 31536000
                            Layout.fillWidth: true

                            onTimeChanged: {
                                settingsPage.changed()
                            }
                        }
                    }
                }

                GroupBox {
                    Layout.fillWidth: true
                    id: siBox
                    title: qsTr("Units")

                    RowLayout {
                        id: siBoxLayout
                        anchors.fill: parent
                        spacing: 5

                        Label {
                            text: qsTr("Temperature")+":"
                        }

                        RadioSwitch {
                            id: siTempSelector
                            textLeft: qsTr("Celsius")
                            textRight: qsTr("Fahrenheit")
                            roleLeft: "C"
                            roleRight: "F"
                            Layout.fillWidth: true

                            onCurrentRoleChanged: {
                                settingsPage.changed()
                            }
                        }

                        Text {
                            id: ph1
                            text: ""
                            Layout.fillWidth: true
                        }
                    }
                }

                GroupBox {
                    Layout.fillWidth: true
                    id: uiBox
                    title: qsTr("Interface")

                    GridLayout {
                        id: uiBoxLayout
                        anchors.fill: parent
                        columnSpacing: 5
                        rowSpacing: 2
                        columns: 3

                        Label {
                            text: qsTr("Language")
                            Layout.alignment: Qt.AlignRight
                        }

                        ComboBox {
                            id: langCombo
                            model: [qsTr("English"), qsTr("Russian")]

                            onCurrentIndexChanged: {
                                settingsPage.changed()
                            }

                            Layout.fillWidth: true
                        }

                        Switch {
                            id: softwareSwitch
                            text: qsTr("Software<br>rendering")
                            Layout.alignment: Qt.AlignRight

                            hoverEnabled: true
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Checked: use software rendering (less resource-demanding, ugly interface)")+
                                          "\n"+
                                          qsTr("Unchecked: use OpenGL based rendering (resource-demanding)")

                            onToggled: {
                                settingsPage.changed()
                            }
                        }

                        Label {
                            text: qsTr("Rescan interval")+":"
                            Layout.alignment: Qt.AlignRight
                        }

                        SpinBox {
                            id: rescanSpin
                            from: 500
                            to: 3600000
                            stepSize: 500

                            editable: true

                            textFromValue: function(value, locale) {
                                return "%1 ".arg(value)+qsTr("ms")
                            }

                            valueFromText: function(text, locale) {
                                return parseInt(text.split(' ')[0])
                            }

                            onValueChanged: {
                                settingsPage.changed()
                            }

                            Layout.fillWidth: true
                        }

                        Text {
                            id: ph2
                            text: ""
                            Layout.fillWidth: true
                        }
                    }
                }

                GroupBox {
                    Layout.fillWidth: true
                    id: webBox
                    title: qsTr("Web Interface")

                    RowLayout {
                        id: webBoxLayout

                        Switch {
                            id: httpSwitch
                            text: qsTr("Server")

                            onToggled: {
                                settingsPage.changed()
                            }
                        }

                        Label {
                            text: qsTr("Port")+":"
                        }

                        SpinBox {
                            id: httpPortSpin
                            from: 1024
                            to: 65535
                            editable: true
                            enabled: httpSwitch.checked

                            textFromValue: function(value, locale) {
                                return "%1".arg(value)
                            }

                            onValueChanged: {
                                settingsPage.changed()
                            }
                        }
                    }
                }

                Text {
                    id: langWarn
                    color: "transparent"
                    Layout.fillWidth: true
                    text: ""
                }
            }
        }

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AlwaysOn
        }
    }
}
