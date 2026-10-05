pragma Singleton
import QtQuick 2.10

QtObject {
    property color sensorNormal: "#43a047"
    property color sensorAccept: "#ffb300"
    property color sensorCritical: "#e53935"
    property color sensorUnknown: "#bdbdbd"

    property color pageColor: "#f3f5f8"
    property color cardColor: "white"
    property color cardBorder: "#e1e5ea"
    property color textSecondary: "#6b7480"
    property int cardRadius: 8

    //always the same number of decimals, otherwise 24.20 -> "24.2" and 24.00 -> "24" make the readouts jump
    readonly property int valueDecimals: 2
    function formatValue(value) {
        var scale = Math.pow(10, valueDecimals)
        //rounding first keeps tiny negative values from turning into "-0.00"
        return (Math.round(Number(value) * scale) / scale).toFixed(valueDecimals)
    }
}
