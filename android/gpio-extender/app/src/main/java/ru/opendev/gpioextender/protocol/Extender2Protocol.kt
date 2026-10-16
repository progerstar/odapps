package ru.opendev.gpioextender.protocol

import java.nio.charset.StandardCharsets
import ru.opendev.gpioextender.model.DeviceType
import ru.opendev.gpioextender.model.PinMode
import ru.opendev.gpioextender.model.PinUiState

internal class Extender2Protocol : DeviceProtocol {
    override val deviceType: DeviceType = DeviceType.EXTENDER_2

    override fun command(body: String, timeoutMillis: Long): ProtocolCommand {
        val clean = sanitizeCommandInput(body)
        require(clean.toByteArray(StandardCharsets.US_ASCII).size <= MAX_BODY_BYTES) {
            "Команда превышает размер CDC-пакета"
        }
        return ProtocolCommand(
            body = clean,
            wireBytes = "~$clean\r\n".toByteArray(StandardCharsets.US_ASCII),
            responseFraming = ResponseFraming.Line(clean.first().toString()),
            timeoutMillis = timeoutMillis,
        )
    }

    override fun normalizeConsoleCommand(input: String): ProtocolCommand = command(input)

    override fun normalizeResponse(raw: String): String = raw
        .trim('\r', '\n', '\u0000', ' ')
        .removePrefix("~")

    fun parseInfo(response: String): String {
        val normalized = normalizeResponse(response)
        require(normalized.startsWith('I') && normalized.length > 1) { "Некорректный ответ I" }
        return normalized.drop(1)
    }

    fun parseModes(response: String): List<PinMode> {
        val normalized = normalizeResponse(response)
        require(normalized.length == 11 && normalized.first() in charArrayOf('M', 'D')) {
            "Некорректный ответ режимов: $response"
        }
        return normalized.drop(1).mapIndexed { index, code ->
            val mode = requireNotNull(PinMode.fromCode(code)) { "Неизвестный режим $code" }
            require(mode.supports(index + 1)) { "Режим $code недоступен на пине ${index + 1}" }
            mode
        }.also(::validateCaptureConflict)
    }

    fun encodeModes(modes: List<PinMode>): String {
        require(modes.size == PIN_COUNT) { "Требуется ровно 10 режимов" }
        modes.forEachIndexed { index, mode ->
            require(mode.supports(index + 1)) { "${mode.title} недоступен на пине ${index + 1}" }
        }
        validateCaptureConflict(modes)
        return modes.joinToString(separator = "") { it.code.toString() }
    }

    fun parsePinValue(pin: PinUiState, response: String): PinUiState {
        val normalized = normalizeResponse(response)
        require(normalized.startsWith('G')) { "Ожидался ответ G" }
        if (normalized.startsWith("GError:")) {
            return pin.copy(error = normalized.removePrefix("GError:"), rawValue = normalized.drop(1))
        }
        val value = normalized.drop(1).trim()
        return when (pin.mode) {
            PinMode.INPUT, PinMode.OUTPUT -> {
                require(value == "0" || value == "1") { "Некорректное цифровое значение" }
                pin.copy(digitalValue = value == "1", rawValue = value, error = null)
            }
            PinMode.ANALOG -> {
                val adc = value.toIntOrNull()
                require(adc != null && adc in 0..4095) { "Некорректное значение ADC" }
                pin.copy(analogValue = adc, rawValue = value, error = null)
            }
            PinMode.PWM -> {
                val duty = validatePwmDuty(value)
                pin.copy(pwmDuty = duty, rawValue = value, error = null)
            }
            PinMode.CAPTURE -> {
                val match = CAPTURE_RESPONSE.matchEntire(value)
                    ?: error("Некорректное значение Capture")
                pin.copy(
                    captureFrequencyHz = match.groupValues[1].toDouble(),
                    captureDutyPercent = match.groupValues[2].toDouble(),
                    rawValue = value,
                    error = null,
                )
            }
            PinMode.UART_RX -> pin.copy(uartText = value, rawValue = value, error = null)
            PinMode.DHT22, PinMode.DHT11 -> {
                val match = DHT_RESPONSE.matchEntire(value) ?: error("Некорректный ответ DHT")
                pin.copy(
                    temperatureC = match.groupValues[1].toDouble(),
                    humidityPercent = match.groupValues[2].toDouble(),
                    rawValue = value,
                    error = null,
                )
            }
            else -> pin.copy(rawValue = value, error = null)
        }
    }

    fun isSuccessful(response: String, command: Char): Boolean {
        val normalized = normalizeResponse(response)
        return normalized == "${command}OK" || normalized.startsWith("${command}OK:")
    }

    companion object {
        const val PIN_COUNT = 10
        const val PWM_MIN_FREQUENCY = 1
        const val PWM_MAX_FREQUENCY = 20_000
        const val PWM_MIN_DUTY = 0
        const val PWM_MAX_DUTY = 100
        private const val MAX_BODY_BYTES = 61

        private val CAPTURE_RESPONSE = Regex("(\\d+(?:\\.\\d+)?),(\\d+(?:\\.\\d+)?)")
        private val DHT_RESPONSE = Regex("(-?\\d+(?:\\.\\d+)?)C,(-?\\d+(?:\\.\\d+)?)%")

        fun connectorPinToProtocol(pin: Int): Int {
            require(pin in 1..PIN_COUNT) { "Номер пина должен быть от 1 до 10" }
            return if (pin == PIN_COUNT) 0 else pin
        }

        fun validatePwmDuty(value: String): Int {
            require(value.matches(Regex("(?:0|[1-9]\\d?|100)"))) { "Скважность должна быть от 0 до 100" }
            return value.toInt()
        }

        fun validatePwmFrequency(value: String): Int {
            require(value.matches(Regex("[1-9]\\d*"))) { "Частота должна быть целым числом" }
            val frequency = value.toIntOrNull()
            require(frequency != null && frequency in PWM_MIN_FREQUENCY..PWM_MAX_FREQUENCY) {
                "Частота должна быть от 1 до 20000 Гц"
            }
            return frequency
        }

        fun validateCaptureConflict(modes: List<PinMode>) {
            require(!(modes[3] == PinMode.CAPTURE && modes[4] == PinMode.CAPTURE)) {
                "Capture нельзя одновременно включить на пинах 4 и 5"
            }
        }
    }
}
