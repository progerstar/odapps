package ru.opendev.odtemplogger.usb

import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbInterface
import android.hardware.usb.UsbManager
import android.hardware.usb.UsbRequest
import java.io.Closeable
import java.io.IOException
import java.nio.ByteBuffer
import java.util.concurrent.atomic.AtomicBoolean
import ru.opendev.odtemplogger.protocol.SensorProtocol

fun UsbDevice.isSensorCandidate(): Boolean = vendorId == SensorProtocol.VID && productId == SensorProtocol.PID &&
    (0 until interfaceCount).any { getInterface(it).interfaceClass == UsbConstants.USB_CLASS_HID }

fun UsbDevice.displayName(): String = "${runCatching { productName }.getOrNull() ?: "USB HID"} · $deviceName"

/** Interrupt IN uses UsbRequest on API 24+, including devices with a composite HID/CDC layout. */
class AndroidHidSensor private constructor(
    private val connection: UsbDeviceConnection,
    private val usbInterface: UsbInterface,
    private val request: UsbRequest,
    private val packetSize: Int,
) : Closeable {
    private val closed = AtomicBoolean(false)

    companion object {
        fun open(manager: UsbManager, device: UsbDevice): AndroidHidSensor {
            require(device.isSensorCandidate()) { "Несовместимое USB-устройство" }
            require(SensorProtocol.supportsProductName(device.productName)) {
                "Устройство не опознано как ODTEMP: ${device.productName ?: "нет USB description"}"
            }
            val iface = (0 until device.interfaceCount).map(device::getInterface).firstOrNull { iface ->
                iface.interfaceClass == UsbConstants.USB_CLASS_HID && (0 until iface.endpointCount).any {
                    iface.getEndpoint(it).type == UsbConstants.USB_ENDPOINT_XFER_INT &&
                        iface.getEndpoint(it).direction == UsbConstants.USB_DIR_IN
                }
            } ?: throw IOException("HID Interrupt IN не найден")
            val endpoint = (0 until iface.endpointCount).map(iface::getEndpoint).first {
                it.type == UsbConstants.USB_ENDPOINT_XFER_INT && it.direction == UsbConstants.USB_DIR_IN
            }
            val connection = manager.openDevice(device) ?: throw IOException("Android не открыл USB-устройство")
            val request = UsbRequest()
            try {
                check(connection.claimInterface(iface, true)) { "HID-интерфейс занят" }
                check(request.initialize(connection, endpoint)) { "Не удалось открыть HID Interrupt IN" }
                return AndroidHidSensor(connection, iface, request, endpoint.maxPacketSize.coerceAtLeast(64))
            } catch (error: Exception) {
                request.close()
                connection.close()
                throw error
            }
        }
    }

    @Suppress("DEPRECATION")
    fun read(): ByteArray {
        if (closed.get()) throw IOException("USB закрыт")
        val buffer = ByteBuffer.allocateDirect(packetSize)
        if (!request.queue(buffer, buffer.capacity())) throw IOException("Не удалось поставить USB-чтение в очередь")
        if (closed.get()) request.cancel()
        if (connection.requestWait() !== request || closed.get()) throw IOException("USB-чтение прервано")
        val length = buffer.position()
        buffer.flip()
        return ByteArray(length).also(buffer::get)
    }

    fun feature(): ByteArray {
        val buffer = ByteArray(64)
        val length = connection.controlTransfer(0xA1, 0x01, 0x0304, usbInterface.id, buffer, buffer.size, 1000)
        if (length < 5) throw IOException("Не удалось прочитать интервал датчика")
        return buffer.copyOf(length).also { SensorProtocol.interval(it) }
    }

    fun accelerate(periodMs: Long): Long {
        val feature = feature()
        val current = SensorProtocol.interval(feature)
        if (current > periodMs || current == 0L) {
            val updated = SensorProtocol.withInterval(feature, periodMs)
            check(connection.controlTransfer(0x21, 0x09, 0x0304, usbInterface.id, updated, updated.size, 1000) == updated.size) {
                "Датчик не принял интервал измерений"
            }
            val confirmed = SensorProtocol.interval(feature())
            check(confirmed in 1..periodMs) { "Интервал датчика не подтверждён" }
            return confirmed
        }
        return current
    }

    /** May be called by the watchdog or UI to release a blocked API 24 requestWait(). */
    override fun close() {
        if (closed.compareAndSet(false, true)) {
            request.cancel()
            connection.close()
        }
    }

    /** Call only after the read loop has returned. */
    fun dispose() {
        close()
        request.close()
    }
}
