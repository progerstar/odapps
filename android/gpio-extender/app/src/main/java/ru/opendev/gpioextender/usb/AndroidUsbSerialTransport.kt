package ru.opendev.gpioextender.usb

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbManager
import android.os.Build
import androidx.core.content.ContextCompat
import com.hoho.android.usbserial.driver.CdcAcmSerialDriver
import com.hoho.android.usbserial.driver.UsbSerialPort
import com.hoho.android.usbserial.driver.UsbSerialProber
import java.io.IOException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import ru.opendev.gpioextender.model.ConnectionState
import ru.opendev.gpioextender.model.GPIO_EXTENDER_LEGACY_PRODUCT_ID
import ru.opendev.gpioextender.model.GPIO_EXTENDER_PRODUCT_ID
import ru.opendev.gpioextender.model.GPIO_EXTENDER_VENDOR_ID
import ru.opendev.gpioextender.model.UsbDeviceDescriptor

internal class AndroidUsbSerialTransport(
    context: Context,
    private val scope: CoroutineScope,
) : UsbSerialTransport {
    private val appContext = context.applicationContext
    private val usbManager = appContext.getSystemService(Context.USB_SERVICE) as UsbManager
    private val portMutex = Mutex()

    private val _connectionState = MutableStateFlow<ConnectionState>(ConnectionState.Disconnected)
    override val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    private val _devices = MutableStateFlow<List<UsbDeviceDescriptor>>(emptyList())
    override val devices: StateFlow<List<UsbDeviceDescriptor>> = _devices.asStateFlow()

    private val _incomingBytes = MutableSharedFlow<ByteArray>(
        extraBufferCapacity = 128,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )
    override val incomingBytes: SharedFlow<ByteArray> = _incomingBytes.asSharedFlow()

    private var port: UsbSerialPort? = null
    private var usbConnection: UsbDeviceConnection? = null
    private var readJob: Job? = null
    private var currentDeviceId: Int? = null
    private var receiversRegistered = false
    private var selectedDeviceId: Int? = null

    override val isOpen: Boolean
        get() = port?.isOpen == true && _connectionState.value is ConnectionState.Connected

    private val permissionReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action != USB_PERMISSION_ACTION) return
            val device = intent.usbDevice() ?: return
            if (!device.isGpioExtender()) return
            refreshDeviceList()
            if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) {
                scope.launch { connect(device.deviceId, requestPermission = false) }
            } else {
                selectedDeviceId = null
                _connectionState.value = ConnectionState.PermissionDenied(device.toDescriptor(false))
            }
        }
    }

    private val attachDetachReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val device = intent.usbDevice() ?: return
            if (!device.isGpioExtender()) return
            when (intent.action) {
                UsbManager.ACTION_USB_DEVICE_ATTACHED -> {
                    refreshDeviceList()
                    if (currentDeviceId == null) chooseAutomaticConnection()
                }
                UsbManager.ACTION_USB_DEVICE_DETACHED -> {
                    refreshDeviceList()
                    if (device.deviceId == currentDeviceId || device.deviceId == selectedDeviceId) {
                        scope.launch {
                            close("GPIO Extender отключён от USB")
                            chooseAutomaticConnection()
                        }
                    }
                }
            }
        }
    }

    override fun start() {
        if (!receiversRegistered) {
            registerReceiver(permissionReceiver, IntentFilter(USB_PERMISSION_ACTION), exported = false)
            registerReceiver(
                attachDetachReceiver,
                IntentFilter().apply {
                    addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
                    addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
                },
                exported = true,
            )
            receiversRegistered = true
        }
        refreshDeviceList()
        chooseAutomaticConnection()
    }

    override fun stop() {
        if (receiversRegistered) {
            runCatching { appContext.unregisterReceiver(permissionReceiver) }
            runCatching { appContext.unregisterReceiver(attachDetachReceiver) }
            receiversRegistered = false
        }
        readJob?.cancel()
        readJob = null
        val oldPort = port
        val oldConnection = usbConnection
        port = null
        usbConnection = null
        currentDeviceId = null
        selectedDeviceId = null
        runCatching { oldPort?.close() }
        runCatching { oldConnection?.close() }
        _connectionState.value = ConnectionState.Disconnected
    }

    override fun refresh() {
        refreshDeviceList()
        if (currentDeviceId == null) chooseAutomaticConnection()
    }

    override suspend fun selectDevice(deviceId: Int) {
        selectedDeviceId = deviceId
        connect(deviceId, requestPermission = true)
    }

    private fun chooseAutomaticConnection() {
        val matching = matchingDevices()
        when {
            matching.isEmpty() -> _connectionState.value = ConnectionState.Disconnected
            matching.size == 1 -> {
                selectedDeviceId = matching.single().deviceId
                scope.launch { connect(matching.single().deviceId, requestPermission = true) }
            }
            else -> _connectionState.value = ConnectionState.SelectionRequired(_devices.value)
        }
    }

    private suspend fun connect(deviceId: Int, requestPermission: Boolean) {
        portMutex.withLock {
            val device = matchingDevices().firstOrNull { it.deviceId == deviceId }
            if (device == null) {
                closeLocked("Выбранное USB-устройство больше не подключено")
                return
            }
            val permission = usbManager.hasPermission(device)
            val descriptor = device.toDescriptor(permission)
            if (!permission) {
                _connectionState.value = ConnectionState.PermissionRequired(descriptor)
                if (requestPermission) requestPermission(device)
                return
            }
            if (isOpen && currentDeviceId == deviceId) return

            closeLocked(null)
            selectedDeviceId = deviceId
            _connectionState.value = ConnectionState.Connecting(descriptor.copy(hasPermission = true))
            try {
                val fallbackDriver = if (CdcAcmSerialDriver.probe(device)) {
                    CdcAcmSerialDriver(device)
                } else {
                    null
                }
                val driver = UsbSerialProber.getDefaultProber().probeDevice(device)
                    ?: fallbackDriver
                    ?: throw IOException("Совместимый CDC ACM интерфейс не найден")
                val selectedPort = driver.ports.firstOrNull()
                    ?: throw IOException("CDC ACM порт не найден")
                val selectedConnection = usbManager.openDevice(device)
                    ?: throw IOException("Android не открыл USB-устройство")

                try {
                    withContext(Dispatchers.IO) {
                        selectedPort.open(selectedConnection)
                        selectedPort.setParameters(
                            BAUD_RATE,
                            UsbSerialPort.DATABITS_8,
                            UsbSerialPort.STOPBITS_1,
                            UsbSerialPort.PARITY_NONE,
                        )
                        selectedPort.setFlowControl(UsbSerialPort.FlowControl.NONE)
                        runCatching { selectedPort.dtr = true }
                        runCatching { selectedPort.rts = true }
                    }
                } catch (error: Throwable) {
                    runCatching { selectedPort.close() }
                    selectedConnection.close()
                    throw error
                }

                port = selectedPort
                usbConnection = selectedConnection
                currentDeviceId = deviceId
                selectedDeviceId = deviceId
                _connectionState.value = ConnectionState.Connected(descriptor.copy(hasPermission = true))
                startReadLoop(selectedPort)
            } catch (error: Throwable) {
                closeLocked(error.message ?: "Не удалось открыть CDC ACM")
            }
        }
    }

    override suspend fun write(data: ByteArray) {
        val activePort = port ?: throw IOException("CDC ACM порт не открыт")
        withContext(Dispatchers.IO) { activePort.write(data, WRITE_TIMEOUT_MILLIS) }
    }

    override suspend fun flushInput() {
        val activePort = port ?: return
        withContext(Dispatchers.IO) {
            runCatching { activePort.purgeHwBuffers(true, false) }
        }
    }

    override suspend fun close(reason: String?) {
        portMutex.withLock { closeLocked(reason) }
    }

    private suspend fun closeLocked(reason: String?) {
        readJob?.cancel()
        readJob = null
        val oldPort = port
        val oldConnection = usbConnection
        port = null
        usbConnection = null
        currentDeviceId = null
        selectedDeviceId = null
        withContext(Dispatchers.IO) {
            runCatching { oldPort?.close() }
            runCatching { oldConnection?.close() }
        }
        _connectionState.value = if (reason.isNullOrBlank()) {
            ConnectionState.Disconnected
        } else {
            ConnectionState.Error(reason)
        }
    }

    private fun startReadLoop(activePort: UsbSerialPort) {
        readJob?.cancel()
        readJob = scope.launch(Dispatchers.IO) {
            val buffer = ByteArray(READ_BUFFER_SIZE)
            try {
                while (currentCoroutineContext().isActive && activePort.isOpen) {
                    val count = activePort.read(buffer, READ_TIMEOUT_MILLIS)
                    if (count > 0) _incomingBytes.emit(buffer.copyOf(count))
                }
            } catch (error: Throwable) {
                if (currentCoroutineContext().isActive) {
                    scope.launch { close("Ошибка чтения USB: ${error.message ?: "I/O"}") }
                }
            }
        }
    }

    private fun requestPermission(device: UsbDevice) {
        val intent = Intent(USB_PERMISSION_ACTION).setPackage(appContext.packageName)
        val pendingIntent = PendingIntent.getBroadcast(
            appContext,
            device.deviceId,
            intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE,
        )
        usbManager.requestPermission(device, pendingIntent)
    }

    private fun refreshDeviceList() {
        _devices.value = matchingDevices()
            .sortedBy(UsbDevice::getDeviceId)
            .map { it.toDescriptor(usbManager.hasPermission(it)) }
    }

    private fun matchingDevices(): List<UsbDevice> = usbManager.deviceList.values
        .filter { it.isGpioExtender() }

    private fun UsbDevice.isGpioExtender(): Boolean =
        vendorId == GPIO_EXTENDER_VENDOR_ID &&
            (productId == GPIO_EXTENDER_PRODUCT_ID || productId == GPIO_EXTENDER_LEGACY_PRODUCT_ID)

    private fun UsbDevice.toDescriptor(permission: Boolean): UsbDeviceDescriptor = UsbDeviceDescriptor(
        deviceId = deviceId,
        productName = runCatching { productName }.getOrNull().orEmpty().ifBlank { "GPIO Extender" },
        manufacturerName = runCatching { manufacturerName }.getOrNull(),
        serialNumber = if (permission) runCatching { serialNumber }.getOrNull() else null,
        hasPermission = permission,
    )

    private fun registerReceiver(receiver: BroadcastReceiver, filter: IntentFilter, exported: Boolean) {
        ContextCompat.registerReceiver(
            appContext,
            receiver,
            filter,
            if (exported) ContextCompat.RECEIVER_EXPORTED else ContextCompat.RECEIVER_NOT_EXPORTED,
        )
    }

    @Suppress("DEPRECATION")
    private fun Intent.usbDevice(): UsbDevice? = if (Build.VERSION.SDK_INT >= 33) {
        getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
    } else {
        getParcelableExtra(UsbManager.EXTRA_DEVICE)
    }

    private companion object {
        const val USB_PERMISSION_ACTION = "ru.opendev.gpioextender.USB_PERMISSION"
        const val BAUD_RATE = 115_200
        const val READ_BUFFER_SIZE = 8 * 1024
        const val READ_TIMEOUT_MILLIS = 200
        const val WRITE_TIMEOUT_MILLIS = 1_000
    }
}
