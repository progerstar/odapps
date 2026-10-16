package ru.opendev.gpioextender.protocol

import kotlinx.coroutines.async
import kotlinx.coroutines.delay
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import ru.opendev.gpioextender.model.ConnectionState
import ru.opendev.gpioextender.model.UsbDeviceDescriptor
import ru.opendev.gpioextender.usb.UsbSerialTransport

@OptIn(ExperimentalCoroutinesApi::class)
class SerialCommandClientTest {
    @Test
    fun `v1 fixed response can arrive in short packets`() = runTest {
        val fake = FakeTransport(this) { bytes ->
            if (bytes.decodeToString() == "~A") {
                incoming.emit("~A10".encodeToByteArray())
                incoming.emit("101".encodeToByteArray())
            }
        }
        val protocol = Extender1Protocol()
        val client = SerialCommandClient(fake, backgroundScope) { protocol }
        runCurrent()

        assertEquals("A10101", client.transact(protocol.command("A")))
    }

    @Test
    fun `v2 echoed line cannot satisfy pending command`() = runTest {
        val fake = FakeTransport(this) { bytes ->
            val wire = bytes.decodeToString()
            incoming.emit((wire + "~EOK\r\n").encodeToByteArray())
        }
        val protocol = Extender2Protocol()
        val client = SerialCommandClient(fake, backgroundScope) { protocol }
        runCurrent()
        client.resetSession()

        assertEquals("EOK", client.transact(protocol.command("E0")))
    }

    @Test
    fun `v2 accepts response equal to command after echo is disabled`() = runTest {
        val protocol = Extender2Protocol()
        val fake = FakeTransport(this) { bytes ->
            when (bytes.decodeToString().trim()) {
                "~E0" -> {
                    incoming.emit("~E0\r\n".encodeToByteArray())
                    incoming.emit("~EOK\r\n".encodeToByteArray())
                }
                "~G0" -> incoming.emit("~G0\r\n".encodeToByteArray())
            }
        }
        val client = SerialCommandClient(fake, backgroundScope) { protocol }
        runCurrent()
        client.resetSession()

        assertEquals("EOK", client.transact(protocol.command("E0")))
        assertEquals("G0", client.transact(protocol.command("G0")))
    }

    @Test
    fun `no-response reboot command completes after write`() = runTest {
        val fake = FakeTransport(this)
        val protocol = Extender1Protocol()
        val client = SerialCommandClient(fake, backgroundScope) { protocol }
        runCurrent()

        assertNull(client.transact(protocol.command("B")))
        assertEquals(listOf("~B"), fake.writes)
    }

    @Test
    fun `timeout closes transport so late response cannot leak`() = runTest {
        val fake = FakeTransport(this)
        val protocol = Extender2Protocol()
        val client = SerialCommandClient(fake, backgroundScope) { protocol }
        runCurrent()

        val request = async { runCatching { client.transact(protocol.command("G1", 100)) }.exceptionOrNull() }
        advanceTimeBy(101)
        runCurrent()
        assertTrue(request.await() is SerialTimeoutException)
        assertEquals(1, fake.closeCount)
    }

    @Test
    fun `concurrent callers are serialized by one transaction mutex`() = runTest {
        val fake = FakeTransport(this) { bytes ->
            activeWrites++
            maxActiveWrites = maxOf(maxActiveWrites, activeWrites)
            val command = bytes.decodeToString().trim().removePrefix("~")
            launch {
                delay(100)
                incoming.emit("~${command.first()}OK\r\n".encodeToByteArray())
                activeWrites--
            }
        }
        val protocol = Extender2Protocol()
        val client = SerialCommandClient(fake, backgroundScope) { protocol }
        runCurrent()

        val first = async { client.transact(protocol.command("L1")) }
        val second = async { client.transact(protocol.command("F")) }
        advanceTimeBy(250)
        runCurrent()
        assertEquals("LOK", first.await())
        assertEquals("FOK", second.await())
        assertEquals(1, fake.maxActiveWrites)
    }

    private class FakeTransport(
        val scope: kotlinx.coroutines.CoroutineScope,
        private val onWrite: suspend FakeTransport.(ByteArray) -> Unit = {},
    ) : UsbSerialTransport {
        override val connectionState = MutableStateFlow<ConnectionState>(
            ConnectionState.Connected(UsbDeviceDescriptor(1, "Fake", hasPermission = true)),
        )
        override val devices = MutableStateFlow(listOf(UsbDeviceDescriptor(1, "Fake", hasPermission = true)))
        override val incomingBytes = MutableSharedFlow<ByteArray>(extraBufferCapacity = 16)
        override var isOpen: Boolean = true
        val writes = mutableListOf<String>()
        var closeCount = 0
        var activeWrites = 0
        var maxActiveWrites = 0

        val incoming: MutableSharedFlow<ByteArray> get() = incomingBytes

        override fun start() = Unit
        override fun stop() = Unit
        override fun refresh() = Unit
        override suspend fun selectDevice(deviceId: Int) = Unit
        override suspend fun write(data: ByteArray) {
            writes += data.decodeToString()
            onWrite(data)
        }
        override suspend fun flushInput() = Unit
        override suspend fun close(reason: String?) {
            closeCount++
            isOpen = false
            connectionState.value = ConnectionState.Error(reason.orEmpty())
        }
    }
}
