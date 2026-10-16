package ru.opendev.gpioextender

import android.app.Application
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import java.io.IOException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import ru.opendev.gpioextender.data.DeviceSettings
import ru.opendev.gpioextender.model.ConnectionState
import ru.opendev.gpioextender.model.ConsoleDirection
import ru.opendev.gpioextender.model.ConsoleEntry
import ru.opendev.gpioextender.model.DeviceType
import ru.opendev.gpioextender.model.PinMode
import ru.opendev.gpioextender.model.PinUiState
import ru.opendev.gpioextender.model.UiState
import ru.opendev.gpioextender.protocol.DeviceProtocol
import ru.opendev.gpioextender.protocol.Extender1Protocol
import ru.opendev.gpioextender.protocol.Extender2Protocol
import ru.opendev.gpioextender.protocol.SerialCommandClient
import ru.opendev.gpioextender.protocol.SerialTimeoutException
import ru.opendev.gpioextender.usb.AndroidUsbSerialTransport
import ru.opendev.gpioextender.usb.UsbSerialTransport

internal class GpioViewModel(
    transportFactory: (kotlinx.coroutines.CoroutineScope) -> UsbSerialTransport,
    private val settings: DeviceSettings,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
    private val pollIntervalMillis: Long = 1_000,
    dhtIntervalMillis: Long = 5_000,
    private val nowMillis: () -> Long = System::currentTimeMillis,
) : ViewModel() {
    private val extender1 = Extender1Protocol()
    private val extender2 = Extender2Protocol()
    private val transport = transportFactory(viewModelScope)
    private val _uiState = MutableStateFlow(UiState())
    val uiState: StateFlow<UiState> = _uiState.asStateFlow()
    private val client = SerialCommandClient(transport, viewModelScope, ::currentProtocol)
    private val pollMutex = Mutex()
    private val dhtEveryPolls = (dhtIntervalMillis / pollIntervalMillis).toInt().coerceAtLeast(1)

    private var settingsLoaded = false
    private var activeConnectedDeviceId: Int? = null
    private var sessionReady = false
    private var sessionJob: Job? = null
    private var pollingJob: Job? = null
    private var operationSupervisor: Job? = null
    private var consoleSequence = 0L
    private var pollCycle = 0

    init {
        observeSettings()
        observeTransport()
    }

    private fun observeSettings() {
        viewModelScope.launch {
            settings.deviceType.distinctUntilChanged().collect { type ->
                if (!settingsLoaded) {
                    _uiState.update { it.copy(deviceType = type) }
                    settingsLoaded = true
                    transport.start()
                } else if (_uiState.value.deviceType != type) {
                    switchDeviceType(type)
                }
            }
        }
    }

    private fun observeTransport() {
        viewModelScope.launch {
            transport.devices.collectLatest { devices ->
                _uiState.update { it.copy(devices = devices) }
            }
        }
        viewModelScope.launch {
            transport.connectionState.collect { connection ->
                _uiState.update { it.copy(connectionState = connection) }
                val connected = connection as? ConnectionState.Connected
                if (connected == null) {
                    activeConnectedDeviceId = null
                    sessionReady = false
                    sessionJob?.cancel()
                    sessionJob = null
                    stopPolling()
                    stopSessionOperations()
                } else if (connected.device.deviceId != activeConnectedDeviceId) {
                    activeConnectedDeviceId = connected.device.deviceId
                    startSession()
                }
            }
        }
    }

    private suspend fun switchDeviceType(type: DeviceType) {
        sessionReady = false
        activeConnectedDeviceId = null
        sessionJob?.cancel()
        stopPolling()
        stopSessionOperations()
        transport.close()
        _uiState.update {
            UiState(
                deviceType = type,
                connectionState = ConnectionState.Disconnected,
                devices = it.devices,
                controlVisible = it.controlVisible,
                pwmFrequencyHz = it.pwmFrequencyHz,
            )
        }
        transport.refresh()
    }

    private fun startSession() {
        sessionJob?.cancel()
        startSessionOperations()
        sessionReady = false
        pollCycle = 0
        sessionJob = viewModelScope.launch(ioDispatcher) {
            _uiState.update { it.copy(operationInProgress = true, userMessage = null) }
            try {
                client.resetSession()
                when (_uiState.value.deviceType) {
                    DeviceType.EXTENDER_1 -> initializeExtender1()
                    DeviceType.EXTENDER_2 -> initializeExtender2()
                }
                sessionReady = true
                restartPollingIfNeeded()
            } catch (error: Throwable) {
                if (error is kotlinx.coroutines.CancellationException) throw error
                showMessage(error.message ?: "Ошибка инициализации устройства")
            } finally {
                _uiState.update { it.copy(operationInProgress = false) }
            }
        }
    }

    private suspend fun initializeExtender1() {
        val info = requireNotNull(client.transact(extender1.command("I", 1_500)))
        val inputs = requireNotNull(client.transact(extender1.command("A")))
        _uiState.update {
            it.copy(
                deviceInfo = extender1.parseInfo(info),
                v1Inputs = extender1.parseInputs(inputs),
                v1Outputs = List(5) { null },
            )
        }
    }

    private suspend fun initializeExtender2() {
        val echoResponse = requireNotNull(client.transact(extender2.command("E0", 1_000)))
        check(extender2.isSuccessful(echoResponse, 'E')) { "Устройство не подтвердило отключение echo" }
        val info = requireNotNull(client.transact(extender2.command("I", 1_500)))
        val modeResponse = requireNotNull(client.transact(extender2.command("M")))
        val modes = extender2.parseModes(modeResponse)
        _uiState.update { state ->
            state.copy(
                deviceInfo = extender2.parseInfo(info),
                pins = modes.mapIndexed { index, mode -> PinUiState(index + 1, mode) },
            )
        }
        readExtender2Pins(includeDht = true)
    }

    private fun currentProtocol(): DeviceProtocol = when (_uiState.value.deviceType) {
        DeviceType.EXTENDER_1 -> extender1
        DeviceType.EXTENDER_2 -> extender2
    }

    fun selectDeviceType(type: DeviceType) {
        if (type == _uiState.value.deviceType) return
        viewModelScope.launch { settings.setDeviceType(type) }
    }

    fun selectUsbDevice(deviceId: Int) {
        viewModelScope.launch(ioDispatcher) { transport.selectDevice(deviceId) }
    }

    fun retryUsb() = transport.refresh()

    fun setControlVisible(visible: Boolean) {
        if (_uiState.value.controlVisible == visible) return
        _uiState.update { it.copy(controlVisible = visible) }
        restartPollingIfNeeded()
    }

    private fun restartPollingIfNeeded() {
        if (!_uiState.value.controlVisible || !sessionReady || !transport.isOpen) {
            stopPolling()
            return
        }
        if (pollingJob?.isActive == true) return
        pollingJob = viewModelScope.launch(ioDispatcher) {
            while (true) {
                delay(pollIntervalMillis)
                pollCycle++
                pollOnce(includeDht = pollCycle % dhtEveryPolls == 0)
            }
        }
    }

    private fun stopPolling() {
        pollingJob?.cancel()
        pollingJob = null
    }

    fun refreshValues() {
        launchSessionTask { pollOnce(includeDht = true, reportErrors = true) }
    }

    private suspend fun pollOnce(includeDht: Boolean, reportErrors: Boolean = false) {
        if (!pollMutex.tryLock()) return
        try {
            if (!transport.isOpen || !sessionReady) return
            when (_uiState.value.deviceType) {
                DeviceType.EXTENDER_1 -> {
                    val response = requireNotNull(client.transact(extender1.command("A")))
                    _uiState.update { it.copy(v1Inputs = extender1.parseInputs(response)) }
                }
                DeviceType.EXTENDER_2 -> readExtender2Pins(includeDht)
            }
        } catch (error: Throwable) {
            if (error is kotlinx.coroutines.CancellationException) throw error
            if (reportErrors) showMessage(error.message ?: "Не удалось обновить значения")
        } finally {
            pollMutex.unlock()
        }
    }

    private suspend fun readExtender2Pins(includeDht: Boolean) {
        val snapshot = _uiState.value.pins
        for (pin in snapshot) {
            if (!transport.isOpen) break
            if (pin.mode in setOf(PinMode.WS2812, PinMode.UNCHANGED, PinMode.UART_TX)) continue
            if (pin.mode in setOf(PinMode.DHT22, PinMode.DHT11) && !includeDht) continue
            val protocolPin = Extender2Protocol.connectorPinToProtocol(pin.number)
            try {
                val timeout = if (pin.mode in setOf(PinMode.DHT22, PinMode.DHT11)) 3_000L else 2_000L
                val response = requireNotNull(client.transact(extender2.command("G$protocolPin", timeout)))
                updatePin(pin.number) { current -> extender2.parsePinValue(current, response) }
            } catch (error: Throwable) {
                if (error is kotlinx.coroutines.CancellationException) throw error
                if (!transport.isOpen) break
                updatePin(pin.number) { it.copy(error = error.message ?: "Ошибка чтения") }
            }
        }
    }

    fun setExtender1Output(index: Int, value: Boolean) {
        require(index in 0..4)
        launchOperation {
            val body = "${if (value) 'S' else 'R'}${index + 1}"
            val response = requireNotNull(client.transact(extender1.command(body)))
            check(extender1.isAcknowledged(body, response)) { "Устройство не подтвердило команду" }
            _uiState.update { state ->
                state.copy(v1Outputs = state.v1Outputs.toMutableList().also { it[index] = value })
            }
        }
    }

    fun setExtender2Output(pin: Int, value: Boolean) {
        launchOperation {
            val protocolPin = Extender2Protocol.connectorPinToProtocol(pin)
            val response = requireNotNull(client.transact(extender2.command("S$protocolPin:${if (value) 1 else 0}")))
            check(extender2.isSuccessful(response, 'S')) { "Устройство отклонило значение" }
            updatePin(pin) { it.copy(digitalValue = value, rawValue = if (value) "1" else "0", error = null) }
        }
    }

    fun setPwmFrequency(text: String) {
        runCatching { Extender2Protocol.validatePwmFrequency(text.trim()) }
            .onSuccess { frequency -> _uiState.update { it.copy(pwmFrequencyHz = frequency, userMessage = null) } }
            .onFailure { showMessage(it.message ?: "Некорректная частота") }
    }

    fun setPwm(pin: Int, duty: Int) {
        launchOperation {
            val validDuty = Extender2Protocol.validatePwmDuty(duty.toString())
            val protocolPin = Extender2Protocol.connectorPinToProtocol(pin)
            val frequency = _uiState.value.pwmFrequencyHz
            val init = requireNotNull(client.transact(extender2.command("P$protocolPin:$frequency")))
            check(extender2.isSuccessful(init, 'P')) { "Устройство отклонило частоту PWM" }
            val set = requireNotNull(client.transact(extender2.command("S$protocolPin:$validDuty")))
            check(extender2.isSuccessful(set, 'S')) { "Устройство отклонило скважность PWM" }
            updatePin(pin) { it.copy(pwmDuty = validDuty, rawValue = validDuty.toString(), error = null) }
        }
    }

    fun setPinMode(pin: Int, mode: PinMode) {
        require(pin in 1..10)
        val oldPins = _uiState.value.pins
        val requested = oldPins.map { if (it.number == pin) it.copy(mode = mode) else it }
        val modes = requested.map(PinUiState::mode)
        val encoded = runCatching { extender2.encodeModes(modes) }
            .getOrElse {
                showMessage(it.message ?: "Недопустимая конфигурация")
                return
            }
        _uiState.update { it.copy(pins = requested) }
        launchOperation(onFailure = { _uiState.update { state -> state.copy(pins = oldPins) } }) {
            val response = requireNotNull(client.transact(extender2.command("D$encoded")))
            val actualModes = extender2.parseModes(response)
            _uiState.update { state ->
                state.copy(
                    pins = actualModes.mapIndexed { index, actual ->
                        val previous = state.pins[index]
                        if (previous.mode == actual) previous.copy(error = null)
                        else PinUiState(index + 1, actual)
                    },
                )
            }
            readExtender2Pins(includeDht = false)
        }
    }

    fun saveConfiguration() {
        launchOperation(successMessage = "Конфигурация сохранена во flash") {
            val response = requireNotNull(client.transact(extender2.command("F")))
            check(extender2.isSuccessful(response, 'F')) { "Устройство не сохранило конфигурацию" }
        }
    }

    fun sendUart(pin: Int, text: String) {
        if (text.isEmpty()) {
            showMessage("Введите данные USART")
            return
        }
        launchOperation {
            val protocolPin = Extender2Protocol.connectorPinToProtocol(pin)
            val response = requireNotNull(client.transact(extender2.command("S$protocolPin:$text")))
            check(extender2.isSuccessful(response, 'S')) { "Передача USART не подтверждена" }
        }
    }

    fun sendConsoleCommand(input: String) {
        val protocol = currentProtocol()
        val command = runCatching { protocol.normalizeConsoleCommand(input) }
            .getOrElse {
                addConsole(ConsoleDirection.ERROR, it.message ?: "Некорректная команда")
                return
            }
        addConsole(ConsoleDirection.COMMAND, command.displayText)
        val job = launchSessionTask {
            try {
                val response = client.transact(command)
                addConsole(
                    ConsoleDirection.RESPONSE,
                    response?.let { "~$it" } ?: "Ответ не предусмотрен протоколом",
                )
            } catch (error: SerialTimeoutException) {
                addConsole(ConsoleDirection.TIMEOUT, error.message ?: "Таймаут")
            } catch (error: Throwable) {
                if (error is kotlinx.coroutines.CancellationException) throw error
                addConsole(ConsoleDirection.ERROR, error.message ?: "Ошибка обмена")
            }
        }
        if (job == null) addConsole(ConsoleDirection.ERROR, "GPIO Extender не подключён")
    }

    fun clearConsole() {
        _uiState.update { it.copy(consoleEntries = emptyList()) }
    }

    fun dismissMessage() {
        _uiState.update { it.copy(userMessage = null) }
    }

    private fun launchOperation(
        successMessage: String? = null,
        onFailure: () -> Unit = {},
        block: suspend () -> Unit,
    ) {
        val job = launchSessionTask {
            _uiState.update { it.copy(operationInProgress = true, userMessage = null) }
            try {
                if (!transport.isOpen) throw IOException("GPIO Extender не подключён")
                block()
                successMessage?.let(::showMessage)
            } catch (error: Throwable) {
                if (error is kotlinx.coroutines.CancellationException) throw error
                onFailure()
                showMessage(error.message ?: "Операция не выполнена")
            } finally {
                _uiState.update { it.copy(operationInProgress = false) }
            }
        }
        if (job == null) showMessage("GPIO Extender не подключён")
    }

    private fun startSessionOperations() {
        stopSessionOperations()
        operationSupervisor = SupervisorJob(viewModelScope.coroutineContext[Job])
    }

    private fun stopSessionOperations() {
        operationSupervisor?.cancel()
        operationSupervisor = null
    }

    private fun launchSessionTask(block: suspend () -> Unit): Job? {
        val supervisor = operationSupervisor?.takeIf(Job::isActive) ?: return null
        return viewModelScope.launch(supervisor + ioDispatcher) { block() }
    }

    private fun updatePin(number: Int, transform: (PinUiState) -> PinUiState) {
        _uiState.update { state ->
            state.copy(pins = state.pins.map { if (it.number == number) transform(it) else it })
        }
    }

    private fun showMessage(message: String) {
        _uiState.update { it.copy(userMessage = message) }
    }

    private fun addConsole(direction: ConsoleDirection, text: String) {
        val entry = ConsoleEntry(++consoleSequence, nowMillis(), direction, text)
        _uiState.update { state ->
            state.copy(consoleEntries = (state.consoleEntries + entry).takeLast(MAX_CONSOLE_ENTRIES))
        }
    }

    override fun onCleared() {
        transport.stop()
    }

    companion object {
        private const val MAX_CONSOLE_ENTRIES = 500

        fun factory(application: Application): ViewModelProvider.Factory {
            val app = application as GpioExtenderApplication
            return object : ViewModelProvider.Factory {
                @Suppress("UNCHECKED_CAST")
                override fun <T : ViewModel> create(modelClass: Class<T>): T = GpioViewModel(
                    transportFactory = { scope -> AndroidUsbSerialTransport(app, scope) },
                    settings = app.deviceSettings,
                ) as T
            }
        }
    }
}
