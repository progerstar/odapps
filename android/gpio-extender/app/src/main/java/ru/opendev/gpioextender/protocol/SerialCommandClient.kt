package ru.opendev.gpioextender.protocol

import java.io.IOException
import java.nio.charset.StandardCharsets
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.withTimeoutOrNull
import ru.opendev.gpioextender.model.DeviceType
import ru.opendev.gpioextender.usb.UsbSerialTransport

internal class SerialTimeoutException(message: String) : IOException(message)

internal class SerialCommandClient(
    private val transport: UsbSerialTransport,
    scope: CoroutineScope,
    private val protocolProvider: () -> DeviceProtocol,
) {
    private val transactionMutex = Mutex()
    private val received = Channel<ByteArray>(capacity = Channel.UNLIMITED)
    private var echoEnabled = false

    init {
        transport.incomingBytes
            .onEach { received.send(it.copyOf()) }
            .launchIn(scope)
    }

    suspend fun resetSession() = transactionMutex.withLock {
        echoEnabled = protocolProvider().deviceType == DeviceType.EXTENDER_2
        while (received.tryReceive().isSuccess) Unit
    }

    suspend fun transact(command: ProtocolCommand): String? = transactionMutex.withLock {
        if (!transport.isOpen) throw IOException("GPIO Extender не подключён")
        transport.flushInput()
        while (received.tryReceive().isSuccess) Unit
        transport.write(command.wireBytes)

        if (command.responseFraming is ResponseFraming.None) return@withLock null

        val response = try {
            withTimeout(command.timeoutMillis) {
                when (val framing = command.responseFraming) {
                    is ResponseFraming.Fixed -> readFixed(framing)
                    is ResponseFraming.Line -> readLine(command, framing, echoEnabled)
                    is ResponseFraming.Quiet -> readUntilQuiet(framing)
                    ResponseFraming.None -> null
                }
            }
        } catch (_: TimeoutCancellationException) {
            transport.close("Таймаут ответа на ${command.displayText}")
            throw SerialTimeoutException("Таймаут ответа на ${command.displayText}")
        }
        updateEchoState(command, response)
        response
    }

    suspend fun transactBody(body: String, timeoutMillis: Long = 2_000): String? =
        transact(protocolProvider().command(body, timeoutMillis))

    private suspend fun readFixed(framing: ResponseFraming.Fixed): String {
        val data = ArrayList<Byte>(framing.byteCount)
        while (data.size < framing.byteCount) {
            received.receive().forEach(data::add)
        }
        val raw = data.take(framing.byteCount).toByteArray().toString(StandardCharsets.US_ASCII)
        val normalized = protocolProvider().normalizeResponse(raw)
        if (!normalized.startsWith(framing.prefix)) {
            throw IOException("Неожиданный ответ: $raw")
        }
        return normalized
    }

    private suspend fun readLine(
        command: ProtocolCommand,
        framing: ResponseFraming.Line,
        skipEcho: Boolean,
    ): String {
        val buffer = StringBuilder()
        while (true) {
            buffer.append(received.receive().toString(StandardCharsets.US_ASCII))
            var newline = buffer.indexOf("\n")
            while (newline >= 0) {
                val rawLine = buffer.substring(0, newline).trimEnd('\r')
                buffer.delete(0, newline + 1)
                val normalized = protocolProvider().normalizeResponse(rawLine)
                val isEcho = skipEcho && normalized == command.body
                if (!isEcho && normalized.startsWith(framing.prefix)) return normalized
                newline = buffer.indexOf("\n")
            }
        }
    }

    private fun updateEchoState(command: ProtocolCommand, response: String?) {
        if (protocolProvider().deviceType != DeviceType.EXTENDER_2 || response != "EOK") return
        when (command.body) {
            "E0" -> echoEnabled = false
            "E1" -> echoEnabled = true
        }
    }

    private suspend fun readUntilQuiet(framing: ResponseFraming.Quiet): String {
        val buffer = StringBuilder()
        while (true) {
            buffer.append(received.receive().toString(StandardCharsets.US_ASCII))
            val normalized = protocolProvider().normalizeResponse(buffer.toString())
            if (!normalized.startsWith(framing.prefix)) {
                buffer.clear()
                continue
            }
            while (true) {
                val next = withTimeoutOrNull(QUIET_GAP_MILLIS) { received.receive() } ?: break
                buffer.append(next.toString(StandardCharsets.US_ASCII))
            }
            return protocolProvider().normalizeResponse(buffer.toString())
        }
    }

    private companion object {
        const val QUIET_GAP_MILLIS = 40L
    }
}
