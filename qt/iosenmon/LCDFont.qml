pragma Singleton
import QtQuick 2.10

QtObject {
    property FontLoader font: FontLoader {
        id: lcdFont
        source : "qrc:/fonts/DigitalDream.ttf"
    }
}
