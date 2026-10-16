package ru.opendev.odtemplogger

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.SystemClock
import androidx.core.content.ContextCompat
import java.io.File
import java.util.concurrent.atomic.AtomicLong
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.delay
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import ru.opendev.odtemplogger.data.CloudClient
import ru.opendev.odtemplogger.data.CsvLog
import ru.opendev.odtemplogger.data.LoggerSettings
import ru.opendev.odtemplogger.data.SettingsStore
import ru.opendev.odtemplogger.protocol.Sample
import ru.opendev.odtemplogger.protocol.SampleGate
import ru.opendev.odtemplogger.protocol.SensorProtocol
import ru.opendev.odtemplogger.usb.AndroidHidSensor
import ru.opendev.odtemplogger.usb.displayName
import ru.opendev.odtemplogger.usb.isSensorCandidate

data class DeviceChoice(val id: Int, val name: String)
data class LoggerState(
    val settingsReady: Boolean = false,
    val settingsSaving: Boolean = false,
    val running: Boolean = false,
    val status: String = "Подключите ODTEMP через USB OTG",
    val devices: List<DeviceChoice> = emptyList(),
    val sample: Sample? = null,
    val firmware: String = "",
    val recorded: Long = 0,
    val cloudStatus: String = "Отключено",
    val logFile: String = "",
    val error: String? = null,
    val settings: LoggerSettings = LoggerSettings(),
    val recent: List<Sample> = emptyList(),
)

class LoggerController(private val context: Context) {
    private val manager = context.getSystemService(UsbManager::class.java)
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private val store = SettingsStore(context)
    val logs = CsvLog(File(context.filesDir, "logs"))
    private val mutableState = MutableStateFlow(LoggerState())
    val state = mutableState.asStateFlow()
    private var job: Job? = null
    @Volatile private var sensor: AndroidHidSensor? = null
    @Volatile private var generation = 0L
    @Volatile private var connectedDeviceId: Int? = null
    private var pendingDevice: Int? = null
    private val permissionAction = "${context.packageName}.USB_PERMISSION"

    fun initialize() {
        scope.launch {
            val settings = withContext(Dispatchers.IO) { store.load() }
            mutableState.update { it.copy(settings = settings, settingsReady = true) }
        }
        ContextCompat.registerReceiver(context, object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) {
                @Suppress("DEPRECATION")
                val device = intent.getParcelableExtra<UsbDevice>(UsbManager.EXTRA_DEVICE) ?: return
                if (pendingDevice != device.deviceId) return
                pendingDevice = null
                if (manager.hasPermission(device)) connect(device.deviceId)
                else mutableState.update { it.copy(error = "Доступ к USB отклонён. Нажмите «Подключить», чтобы повторить.") }
            }
        }, IntentFilter(permissionAction), ContextCompat.RECEIVER_NOT_EXPORTED)
        ContextCompat.registerReceiver(context, object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) {
                refresh()
                if (intent.action == UsbManager.ACTION_USB_DEVICE_DETACHED) {
                    @Suppress("DEPRECATION")
                    val device = intent.getParcelableExtra<UsbDevice>(UsbManager.EXTRA_DEVICE)
                    if (device?.deviceId == pendingDevice) pendingDevice = null
                    if (device?.deviceId == connectedDeviceId && device != null &&
                        !manager.deviceList.containsKey(device.deviceName)) {
                        sensor?.close()
                        mutableState.update { it.copy(sample = null, status = "Датчик отключён") }
                    }
                }
            }
        }, IntentFilter().apply {
            addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
        }, ContextCompat.RECEIVER_EXPORTED)
        refresh()
    }

    private fun devices() = manager.deviceList.values.filter { it.isSensorCandidate() }.sortedBy { it.deviceName }
    fun refresh() {
        mutableState.update { it.copy(devices = devices().map { device -> DeviceChoice(device.deviceId, device.displayName()) }) }
    }

    fun connect(deviceId: Int) {
        if (!state.value.settingsReady || state.value.settingsSaving) return
        val device = devices().firstOrNull { it.deviceId == deviceId } ?: return refresh()
        if (!manager.hasPermission(device)) {
            pendingDevice = deviceId
            mutableState.update { it.copy(error = null, status = "Разрешите доступ к USB-датчику") }
            val intent = Intent(permissionAction).setPackage(context.packageName)
            manager.requestPermission(device, PendingIntent.getBroadcast(context, deviceId, intent,
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE))
            return
        }
        try {
            state.value.settings.validate()
            ContextCompat.startForegroundService(context, Intent(context, LoggerService::class.java).putExtra("device", deviceId))
        } catch (error: Exception) {
            mutableState.update { it.copy(error = error.message ?: "Не удалось запустить запись") }
        }
    }

    fun saveSettings(settings: LoggerSettings, complete: (String?) -> Unit) {
        scope.launch {
            try {
                check(!state.value.running) { "Сначала остановите наблюдение" }
                check(state.value.settingsReady) { "Подождите загрузки настроек" }
                mutableState.update { it.copy(settingsSaving = true) }
                withContext(Dispatchers.IO) { store.save(settings) }
                mutableState.update { it.copy(settings = settings, error = null) }
                complete(null)
            } catch (error: Exception) { complete(error.message ?: "Не удалось сохранить настройки")
            } finally { mutableState.update { it.copy(settingsSaving = false) } }
        }
    }

    fun start(deviceId: Int) {
        if (job?.isActive == true) return
        val initial = devices().firstOrNull { it.deviceId == deviceId } ?: return
        val selectedSerial = runCatching { initial.serialNumber }.getOrNull()
        val selectedProduct = initial.productName
        val settings = state.value.settings
        val run = ++generation
        mutableState.update { it.copy(running = true, sample = null, recent = emptyList(), recorded = 0,
            status = "Подключение…", firmware = "", error = null, logFile = "",
            cloudStatus = if (settings.cloudEnabled) "Ожидание показаний" else "Отключено") }
        val previous = job
        job = scope.launch(Dispatchers.IO) {
            try {
                previous?.join()
                currentCoroutineContext().ensureActive()
                settings.validate()
                if (settings.logEnabled) {
                    val file = logs.start()
                    publish(run) { it.copy(logFile = file.name) }
                }
                while (isActive) {
                    val candidates = devices().filter { device ->
                        device.productName == selectedProduct && (selectedSerial == null ||
                            runCatching { device.serialNumber }.getOrNull() == selectedSerial)
                    }
                    val device = candidates.firstOrNull { it.deviceId == initial.deviceId }
                        ?: candidates.singleOrNull()
                    if (device == null || !manager.hasPermission(device)) {
                        publish(run) { it.copy(sample = null, status = if (device == null) "Ожидание датчика…" else
                            "Нажмите «Разрешить USB» для переподключения", cloudStatus = if (settings.cloudEnabled) "Ожидание датчика" else "Отключено") }
                        delay(1000)
                        continue
                    }
                    try {
                        readDevice(run, device, settings)
                    } catch (error: LogWriteException) { throw error
                    } catch (error: CancellationException) { throw error
                    } catch (error: Exception) {
                        publish(run) { it.copy(sample = null, status = "Связь потеряна. Повторное подключение…", error = error.message,
                            cloudStatus = if (settings.cloudEnabled) "Ожидание датчика" else "Отключено") }
                        delay(1500)
                    }
                }
            } catch (error: CancellationException) { throw error
            } catch (error: Exception) {
                publish(run) { it.copy(running = false, error = error.message, sample = null) }
                withContext(Dispatchers.Main) { context.stopService(Intent(context, LoggerService::class.java)) }
            }
        }
    }

    private suspend fun readDevice(run: Long, device: UsbDevice, settings: LoggerSettings) = coroutineScope {
        val activeSensor = AndroidHidSensor.open(manager, device)
        sensor = activeSensor
        connectedDeviceId = device.deviceId
        var cloudJob: Job? = null
        var watchdog: Job? = null
        val outgoing = Channel<Sample>(Channel.CONFLATED)
        try {
            currentCoroutineContext().ensureActive()
            val lastData = AtomicLong(SystemClock.elapsedRealtime())
            val requested = minOf(2000L, if (settings.logEnabled) settings.logPeriodMs else 2000L,
                if (settings.cloudEnabled) settings.cloudPeriodMs else 2000L)
            val interval = runCatching { activeSensor.accelerate(requested) }.getOrElse {
                publish(run) { state -> state.copy(error = "Интервал датчика не изменён: ${it.message}") }
                2000L
            }
            val staleAfter = maxOf(10_000L, interval * 3 + 2000)
            publish(run) { it.copy(status = device.displayName(), sample = null, firmware = "", recent = emptyList()) }
            val logGate = SampleGate(settings.logPeriodMs)
            val cloudGate = SampleGate(settings.cloudPeriodMs)
            val uiGate = SampleGate(200)
            val client = if (settings.cloudEnabled) CloudClient(settings) else null
            cloudJob = launch {
                for (sample in outgoing) {
                    currentCoroutineContext().ensureActive()
                    if (generation != run || SystemClock.elapsedRealtime() - lastData.get() > staleAfter) continue
                    try {
                        client!!.post(sample)
                        currentCoroutineContext().ensureActive()
                        publish(run) { it.copy(cloudStatus = "Отправлено: ${java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.getDefault()).format(java.util.Date(sample.at))}") }
                    } catch (error: CancellationException) { throw error
                    } catch (_: Exception) {
                        // Do not display response bodies or request details containing a token.
                        publish(run) { it.copy(cloudStatus = "Ошибка отправки. Повтор при следующем измерении.") }
                    }
                }
            }
            watchdog = launch {
                while (isActive) {
                    delay(1000)
                    if (!manager.deviceList.containsKey(device.deviceName) || SystemClock.elapsedRealtime() - lastData.get() > staleAfter) {
                        publish(run) { it.copy(sample = null, status = "Нет свежих показаний. Переподключение…") }
                        activeSensor.close()
                        break
                    }
                }
            }
            // Keep blocking requestWait off the dispatchers used by watchdog and network upload.
            withContext(Dispatchers.IO) {
                while (isActive) {
                    val report = activeSensor.read()
                    val sample = SensorProtocol.sample(report)
                    if (sample == null) {
                        SensorProtocol.firmware(report)?.let { fw -> publish(run) { it.copy(firmware = fw) } }
                        continue
                    }
                    currentCoroutineContext().ensureActive()
                    val now = SystemClock.elapsedRealtime()
                    lastData.set(now)
                    if (settings.logEnabled && logGate.accept(now)) {
                        try {
                            logs.append(sample)
                            publish(run) { it.copy(recorded = it.recorded + 1) }
                        } catch (error: Exception) {
                            throw LogWriteException("Ошибка записи журнала: ${error.message}", error)
                        }
                    }
                    if (client != null && cloudGate.accept(now)) outgoing.trySend(sample)
                    if (uiGate.accept(now)) publish(run) { it.copy(sample = sample, recent = (it.recent + sample).takeLast(300)) }
                }
            }
        } finally {
            activeSensor.close()
            outgoing.close()
            withContext(NonCancellable) {
                watchdog?.cancelAndJoin()
                cloudJob?.cancelAndJoin()
                activeSensor.dispose()
            }
            if (sensor === activeSensor) {
                sensor = null
                connectedDeviceId = null
            }
        }
    }

    private fun publish(run: Long, transform: (LoggerState) -> LoggerState) {
        mutableState.update { if (generation == run) transform(it) else it }
    }

    fun stop() {
        generation++
        pendingDevice = null
        job?.cancel()
        sensor?.close()
        mutableState.update { it.copy(running = false, sample = null, status = "Наблюдение остановлено") }
    }

    fun startError(message: String) {
        mutableState.update { it.copy(error = message) }
    }

    private class LogWriteException(message: String, cause: Throwable) : java.io.IOException(message, cause)
}
