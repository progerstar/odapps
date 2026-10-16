package ru.opendev.gpioextender

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.After
import org.junit.Rule
import org.junit.Test
import ru.opendev.gpioextender.data.DeviceSettings
import ru.opendev.gpioextender.model.ConnectionState
import ru.opendev.gpioextender.model.DeviceType
import ru.opendev.gpioextender.model.UsbDeviceDescriptor
import ru.opendev.gpioextender.usb.UsbSerialTransport

@OptIn(ExperimentalCoroutinesApi::class)
class GpioViewModelTest {
    @get:Rule
    val mainDispatcherRule = MainDispatcherRule()
    private val createdViewModels = mutableListOf<GpioViewModel>()

    @After
    fun stopBackgroundPolling() {
        createdViewModels.forEach { it.setControlVisible(false) }
        createdViewModels.clear()
    }

    @Test
    fun `permission denial attach and detach states reach ui`() = runTest(mainDispatcherRule.dispatcher) {
        val fake = FakeTransport(this)
        val viewModel = createViewModel(fake, FakeSettings())
        runCurrent()
        val device = fake.device

        fake.connectionState.value = ConnectionState.PermissionRequired(device)
        runCurrent()
        assertTrue(viewModel.uiState.value.connectionState is ConnectionState.PermissionRequired)

        fake.connectionState.value = ConnectionState.PermissionDenied(device)
        runCurrent()
        assertTrue(viewModel.uiState.value.connectionState is ConnectionState.PermissionDenied)

        fake.connect()
        advanceTimeBy(41)
        runCurrent()
        viewModel.setControlVisible(false)
        runCurrent()
        assertEquals("Fake Extender 1", viewModel.uiState.value.deviceInfo)
        assertEquals(listOf(true, false, true, false, true), viewModel.uiState.value.v1Inputs)

        fake.detach()
        runCurrent()
        assertTrue(viewModel.uiState.value.connectionState is ConnectionState.Error)
        viewModel.setControlVisible(false)
        runCurrent()
    }

    @Test
    fun `changing saved type closes resets and reconnects with v2 handshake`() = runTest(mainDispatcherRule.dispatcher) {
        val fake = FakeTransport(this)
        val settings = FakeSettings()
        val viewModel = createViewModel(fake, settings)
        runCurrent()
        fake.connect()
        advanceTimeBy(41)
        runCurrent()
        viewModel.setControlVisible(false)
        runCurrent()
        assertTrue(fake.writes.contains("~I"))

        viewModel.selectDeviceType(DeviceType.EXTENDER_2)
        runCurrent()
        assertEquals(DeviceType.EXTENDER_2, viewModel.uiState.value.deviceType)
        assertEquals(1, fake.closeCount)
        assertEquals(1, fake.refreshCount)
        assertEquals(null, viewModel.uiState.value.deviceInfo)

        fake.connect()
        advanceTimeBy(41)
        runCurrent()
        viewModel.setControlVisible(false)
        runCurrent()
        assertTrue(fake.writes.contains("~E0\r\n"))
        assertEquals("Fake Extender 2", viewModel.uiState.value.deviceInfo)
        viewModel.setControlVisible(false)
        runCurrent()
    }

    @Test
    fun `manual exchanges cannot run in parallel`() = runTest(mainDispatcherRule.dispatcher) {
        val fake = FakeTransport(this)
        val viewModel = createViewModel(fake, FakeSettings())
        runCurrent()
        fake.connect()
        advanceTimeBy(41)
        runCurrent()
        fake.responseDelayMillis = 100
        fake.maxActiveWrites = 0

        viewModel.sendConsoleCommand("I")
        viewModel.sendConsoleCommand("A")
        advanceTimeBy(250)
        runCurrent()
        viewModel.setControlVisible(false)
        runCurrent()
        assertEquals(1, fake.maxActiveWrites)
        assertTrue(viewModel.uiState.value.consoleEntries.any { it.text.contains("Fake Extender 1") })
    }

    @Test
    fun `changing type cancels an old command before reconnecting`() = runTest(mainDispatcherRule.dispatcher) {
        val fake = FakeTransport(this)
        val viewModel = createViewModel(fake, FakeSettings())
        runCurrent()
        viewModel.setControlVisible(false)
        fake.connect()
        advanceUntilIdle()

        fake.responseDelayMillis = 5_000
        viewModel.sendConsoleCommand("I")
        runCurrent()
        assertEquals(1, fake.activeWrites)

        viewModel.selectDeviceType(DeviceType.EXTENDER_2)
        runCurrent()
        assertEquals(1, fake.closeCount)
        assertEquals(0, fake.activeWrites)

        fake.responseDelayMillis = 0
        fake.connect()
        advanceUntilIdle()
        advanceTimeBy(5_000)
        runCurrent()

        assertEquals("Fake Extender 2", viewModel.uiState.value.deviceInfo)
        assertEquals("Старая команда закрыла новую USB-сессию", 1, fake.closeCount)
    }

    @Test
    fun `polling waits after each cycle and dht is read every fifth interval`() = runTest(mainDispatcherRule.dispatcher) {
        val fake = FakeTransport(this).apply { v2Modes = "DIIIIIIIII" }
        val settings = FakeSettings(DeviceType.EXTENDER_2)
        val viewModel = createViewModel(fake, settings, pollMillis = 100, dhtMillis = 500)
        runCurrent()
        viewModel.setControlVisible(false)
        fake.connect()
        advanceUntilIdle()
        assertEquals("Инициализация должна прочитать все пины", 1, fake.commandCount("G2"))
        viewModel.setControlVisible(true)
        runCurrent()

        val dhtAfterInitialization = fake.commandCount("G1")
        val digitalAfterInitialization = fake.commandCount("G2")
        advanceTimeBy(499)
        runCurrent()
        val dhtWasNotPolledEarly = fake.commandCount("G1") == dhtAfterInitialization
        val digitalWasPolled = fake.commandCount("G2") > digitalAfterInitialization

        advanceTimeBy(1)
        runCurrent()
        val dhtWasPolledAtFifthInterval = fake.commandCount("G1") == dhtAfterInitialization + 1

        fake.responseDelayMillis = 200
        val beforeSlowPoll = fake.commandCount("G2")
        advanceTimeBy(1_000)
        runCurrent()
        val slowPollDidNotAccumulate = fake.commandCount("G2") - beforeSlowPoll <= 2
        viewModel.setControlVisible(false)
        runCurrent()
        assertTrue("DHT был опрошен раньше пятого интервала", dhtWasNotPolledEarly)
        assertTrue(
            "Обычный GPIO не опрашивался: G2=${fake.commandCount("G2")}, команды=${fake.writes}",
            digitalWasPolled,
        )
        assertTrue(
            "DHT не был опрошен на пятом интервале: G1=${fake.commandCount("G1")}, команды=${fake.writes}",
            dhtWasPolledAtFifthInterval,
        )
        assertTrue("Медленные циклы опроса накопились", slowPollDidNotAccumulate)
    }

    private fun createViewModel(
        fake: FakeTransport,
        settings: FakeSettings,
        pollMillis: Long = 1_000,
        dhtMillis: Long = 5_000,
    ) = GpioViewModel(
        transportFactory = { fake },
        settings = settings,
        ioDispatcher = mainDispatcherRule.dispatcher,
        pollIntervalMillis = pollMillis,
        dhtIntervalMillis = dhtMillis,
        nowMillis = { 123L },
    ).also(createdViewModels::add)

    private class FakeSettings(initial: DeviceType = DeviceType.EXTENDER_1) : DeviceSettings {
        private val state = MutableStateFlow(initial)
        override val deviceType: Flow<DeviceType> = state
        override suspend fun setDeviceType(type: DeviceType) {
            state.value = type
        }
    }

    private class FakeTransport(private val scope: CoroutineScope) : UsbSerialTransport {
        val device = UsbDeviceDescriptor(7, "GPIO Extender test description", hasPermission = true)
        override val connectionState = MutableStateFlow<ConnectionState>(ConnectionState.Disconnected)
        override val devices = MutableStateFlow(listOf(device))
        override val incomingBytes = MutableSharedFlow<ByteArray>(extraBufferCapacity = 64)
        override var isOpen = false
        val writes = mutableListOf<String>()
        var closeCount = 0
        var refreshCount = 0
        var responseDelayMillis = 0L
        var activeWrites = 0
        var maxActiveWrites = 0
        var v2Modes = "IIIIIIIIII"

        override fun start() = Unit
        override fun stop() = Unit
        override fun refresh() {
            refreshCount++
        }
        override suspend fun selectDevice(deviceId: Int) = connect()
        override suspend fun write(data: ByteArray) {
            val wire = data.decodeToString()
            writes += wire
            activeWrites++
            maxActiveWrites = maxOf(maxActiveWrites, activeWrites)
            try {
                if (responseDelayMillis > 0) delay(responseDelayMillis)
                val body = wire.trim().removePrefix("~")
                val response = if (wire.endsWith("\r\n")) {
                    when {
                        body == "E0" -> "~EOK\r\n"
                        body == "I" -> "~IFake Extender 2\r\n"
                        body == "M" -> "~M$v2Modes\r\n"
                        body.startsWith("G") && body == "G1" && v2Modes.startsWith('D') -> "~G23.4C,56.7%\r\n"
                        body.startsWith("G") -> "~G0\r\n"
                        body.startsWith("D") -> "~D${body.drop(1)}\r\n"
                        else -> "~${body.first()}OK\r\n"
                    }
                } else {
                    when (body.first()) {
                        'I' -> "~IFake Extender 1"
                        'A' -> "~A10101"
                        'S', 'R', 'P' -> "~$body"
                        else -> ""
                    }
                }
                if (response.isNotEmpty()) incomingBytes.emit(response.encodeToByteArray())
            } finally {
                activeWrites--
            }
        }
        override suspend fun flushInput() = Unit
        override suspend fun close(reason: String?) {
            closeCount++
            isOpen = false
            connectionState.value = ConnectionState.Disconnected
        }

        fun connect() {
            isOpen = true
            connectionState.value = ConnectionState.Connected(device)
        }

        fun detach() {
            isOpen = false
            connectionState.value = ConnectionState.Error("Отключено")
        }

        fun commandCount(body: String): Int = writes.count { it.trim().removePrefix("~") == body }
    }
}
