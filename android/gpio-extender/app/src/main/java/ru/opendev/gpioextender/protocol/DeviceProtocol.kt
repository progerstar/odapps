package ru.opendev.gpioextender.protocol

import ru.opendev.gpioextender.model.DeviceType

internal sealed interface ResponseFraming {
    data class Fixed(val byteCount: Int, val prefix: String) : ResponseFraming
    data class Line(val prefix: String) : ResponseFraming
    data class Quiet(val prefix: String) : ResponseFraming
    data object None : ResponseFraming
}

internal data class ProtocolCommand(
    val body: String,
    val wireBytes: ByteArray,
    val responseFraming: ResponseFraming,
    val timeoutMillis: Long = 2_000,
) {
    val displayText: String = "~$body"
}

internal interface DeviceProtocol {
    val deviceType: DeviceType
    fun command(body: String, timeoutMillis: Long = 2_000): ProtocolCommand
    fun normalizeConsoleCommand(input: String): ProtocolCommand
    fun normalizeResponse(raw: String): String
}

internal fun sanitizeCommandInput(input: String): String {
    val withoutEnding = input.trim().removePrefix("~").trimEnd('\r', '\n')
    require(withoutEnding.isNotBlank()) { "Введите команду" }
    require(withoutEnding.all { it.code in 0x20..0x7E }) {
        "Команда должна содержать только печатные ASCII-символы"
    }
    require('\r' !in withoutEnding && '\n' !in withoutEnding) {
        "Команда должна занимать одну строку"
    }
    return withoutEnding
}
