package ru.opendev.odtemplogger.protocol

data class Sample(val temperature: Double, val humidity: Double?, val at: Long = System.currentTimeMillis())

/** Wire layout matches main.go/processDataReport in the desktop logger. */
object SensorProtocol {
    const val VID = 0x0483
    const val PID = 0xA26A

    fun supportsProductName(name: String?): Boolean = name != null &&
        (name.contains("ODTEMP", ignoreCase = true) || name.equals("UNITX USB NST1001", ignoreCase = true))

    fun sample(report: ByteArray, length: Int = report.size, at: Long = System.currentTimeMillis()): Sample? {
        if (length > report.size || length !in listOf(3, 5) || report[0] != 1.toByte()) return null
        fun raw(offset: Int): Short = ((report[offset].toInt() and 255) or
            ((report[offset + 1].toInt() and 255) shl 8)).toShort()
        val temperature = raw(1)
        val humidity = if (length == 5) raw(3) else null
        // Firmware initializes both channels to INT16_MIN until the first valid conversion.
        if (temperature == Short.MIN_VALUE || humidity == Short.MIN_VALUE) return null
        return Sample(temperature.toDouble() / 100, humidity?.toDouble()?.div(100), at)
    }

    fun firmware(report: ByteArray): String? {
        if (report.size < 2 || report[0] != 3.toByte()) return null
        val length = report[1].toInt() and 255
        if (length == 0 || length > report.size - 2) return null
        return report.copyOfRange(2, 2 + length).toString(Charsets.UTF_8).trimEnd('\u0000')
    }

    fun interval(report: ByteArray): Long {
        require(report.size >= 5 && report[0] == 4.toByte()) { "Неверный feature report" }
        return (1..4).fold(0L) { value, i -> value or ((report[i].toLong() and 255) shl (8 * (i - 1))) }
    }

    fun withInterval(report: ByteArray, milliseconds: Long): ByteArray {
        interval(report)
        require(milliseconds in 1..0xffffffffL)
        return report.copyOf().also { copy ->
            for (i in 1..4) copy[i] = (milliseconds shr (8 * (i - 1))).toByte()
        }
    }
}

/** Only actual incoming measurements advance a recorder; silence never repeats an old value. */
class SampleGate(private val periodMs: Long) {
    private var nextAt: Long? = null
    init { require(periodMs > 0) }
    fun accept(monotonicMs: Long): Boolean {
        if (nextAt?.let { monotonicMs < it } == true) return false
        nextAt = monotonicMs + periodMs
        return true
    }
}
