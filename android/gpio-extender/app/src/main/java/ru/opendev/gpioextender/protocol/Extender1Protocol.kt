package ru.opendev.gpioextender.protocol

import java.nio.charset.StandardCharsets
import ru.opendev.gpioextender.model.DeviceType

internal class Extender1Protocol : DeviceProtocol {
    override val deviceType: DeviceType = DeviceType.EXTENDER_1

    override fun command(body: String, timeoutMillis: Long): ProtocolCommand {
        val clean = sanitizeCommandInput(body)
        val framing = when (clean.first()) {
            'A' -> ResponseFraming.Fixed(byteCount = 7, prefix = "A")
            'S', 'R' -> ResponseFraming.Fixed(byteCount = 3, prefix = clean.first().toString())
            'P' -> ResponseFraming.Fixed(byteCount = 7, prefix = "P")
            'I' -> ResponseFraming.Quiet(prefix = "I")
            'B' -> ResponseFraming.None
            else -> ResponseFraming.Quiet(prefix = clean.first().toString())
        }
        return ProtocolCommand(
            body = clean,
            wireBytes = "~$clean".toByteArray(StandardCharsets.US_ASCII),
            responseFraming = framing,
            timeoutMillis = timeoutMillis,
        )
    }

    override fun normalizeConsoleCommand(input: String): ProtocolCommand = command(input)

    override fun normalizeResponse(raw: String): String = raw
        .trimEnd('\r', '\n', '\u0000')
        .removePrefix("~")

    fun parseInputs(response: String): List<Boolean> {
        val normalized = normalizeResponse(response)
        require(INPUT_RESPONSE.matches(normalized)) { "Некорректный ответ входов: $response" }
        return normalized.drop(1).map { it == '1' }
    }

    fun isAcknowledged(commandBody: String, response: String): Boolean =
        normalizeResponse(response) == sanitizeCommandInput(commandBody)

    fun parseInfo(response: String): String {
        val normalized = normalizeResponse(response)
        require(normalized.startsWith('I') && normalized.length > 1) { "Некорректный ответ I" }
        return normalized.drop(1)
    }

    private companion object {
        val INPUT_RESPONSE = Regex("A[01]{5}")
    }
}
