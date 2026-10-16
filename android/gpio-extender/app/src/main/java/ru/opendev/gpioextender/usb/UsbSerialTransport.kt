package ru.opendev.gpioextender.usb

import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import ru.opendev.gpioextender.model.ConnectionState
import ru.opendev.gpioextender.model.UsbDeviceDescriptor

internal interface UsbSerialTransport {
    val connectionState: StateFlow<ConnectionState>
    val devices: StateFlow<List<UsbDeviceDescriptor>>
    val incomingBytes: SharedFlow<ByteArray>
    val isOpen: Boolean

    fun start()
    fun stop()
    fun refresh()
    suspend fun selectDevice(deviceId: Int)
    suspend fun write(data: ByteArray)
    suspend fun flushInput()
    suspend fun close(reason: String? = null)
}
