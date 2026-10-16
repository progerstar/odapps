package ru.opendev.gpioextender.model

internal const val GPIO_EXTENDER_VENDOR_ID = 0x0483
internal const val GPIO_EXTENDER_PRODUCT_ID = 0xA26A
internal const val GPIO_EXTENDER_LEGACY_PRODUCT_ID = 0x5740

internal enum class DeviceType(val title: String) {
    EXTENDER_1("GPIO Extender 1"),
    EXTENDER_2("GPIO Extender 2"),
}

internal data class UsbDeviceDescriptor(
    val deviceId: Int,
    val productName: String,
    val manufacturerName: String? = null,
    val serialNumber: String? = null,
    val hasPermission: Boolean = false,
) {
    val displayName: String
        get() = buildString {
            append(productName.ifBlank { "GPIO Extender" })
            serialNumber?.takeIf(String::isNotBlank)?.let { append(" · ").append(it) }
            append(" · USB #").append(deviceId)
        }
}

internal sealed interface ConnectionState {
    data object Disconnected : ConnectionState
    data object Searching : ConnectionState
    data class SelectionRequired(val devices: List<UsbDeviceDescriptor>) : ConnectionState
    data class PermissionRequired(val device: UsbDeviceDescriptor) : ConnectionState
    data class PermissionDenied(val device: UsbDeviceDescriptor) : ConnectionState
    data class Connecting(val device: UsbDeviceDescriptor) : ConnectionState
    data class Connected(val device: UsbDeviceDescriptor) : ConnectionState
    data class Error(val message: String) : ConnectionState
}

internal enum class PinMode(
    val code: Char,
    val title: String,
    private val supportedPins: Set<Int>,
) {
    INPUT('I', "Вход", (1..10).toSet()),
    OUTPUT('O', "Выход", (1..10).toSet()),
    DHT22('D', "DHT22", (1..10).toSet()),
    DHT11('d', "DHT11", (1..10).toSet()),
    PWM('P', "PWM", setOf(2, 3, 6, 7)),
    ANALOG('A', "ADC", setOf(1, 2, 3, 4, 5, 6, 7, 9)),
    UART_TX('T', "USART TX", setOf(2)),
    UART_RX('R', "USART RX", setOf(6)),
    WS2812('W', "WS2812", setOf(4)),
    CAPTURE('C', "Capture", setOf(4, 5, 9)),
    UNCHANGED('-', "Без изменения", setOf(8, 10));

    fun supports(pin: Int): Boolean = pin in supportedPins

    companion object {
        fun fromCode(code: Char): PinMode? = entries.firstOrNull { it.code == code }
        fun availableFor(pin: Int): List<PinMode> = entries.filter { it.supports(pin) }
    }
}

internal data class PinUiState(
    val number: Int,
    val mode: PinMode = PinMode.INPUT,
    val digitalValue: Boolean? = null,
    val analogValue: Int? = null,
    val pwmDuty: Int? = null,
    val captureFrequencyHz: Double? = null,
    val captureDutyPercent: Double? = null,
    val uartText: String? = null,
    val temperatureC: Double? = null,
    val humidityPercent: Double? = null,
    val rawValue: String? = null,
    val error: String? = null,
)

internal enum class ConsoleDirection { COMMAND, RESPONSE, ERROR, TIMEOUT }

internal data class ConsoleEntry(
    val id: Long,
    val timestampMillis: Long,
    val direction: ConsoleDirection,
    val text: String,
)

internal data class UiState(
    val deviceType: DeviceType = DeviceType.EXTENDER_1,
    val connectionState: ConnectionState = ConnectionState.Disconnected,
    val devices: List<UsbDeviceDescriptor> = emptyList(),
    val deviceInfo: String? = null,
    val v1Outputs: List<Boolean?> = List(5) { null },
    val v1Inputs: List<Boolean?> = List(5) { null },
    val pins: List<PinUiState> = List(10) { PinUiState(it + 1) },
    val pwmFrequencyHz: Int = 1_000,
    val consoleEntries: List<ConsoleEntry> = emptyList(),
    val controlVisible: Boolean = true,
    val operationInProgress: Boolean = false,
    val userMessage: String? = null,
)
