package ru.opendev.gpioextender.protocol

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import ru.opendev.gpioextender.model.PinMode
import ru.opendev.gpioextender.model.PinUiState

class DeviceProtocolTest {
    private val v1 = Extender1Protocol()
    private val v2 = Extender2Protocol()

    @Test
    fun `v1 commands and short replies have no line ending`() {
        assertEquals("~A", v1.command("A").wireBytes.decodeToString())
        assertEquals("~S3", v1.normalizeConsoleCommand("~S3\r\n").wireBytes.decodeToString())
        assertEquals(listOf(true, false, true, false, true), v1.parseInputs("~A10101"))
        assertTrue(v1.isAcknowledged("R4", "~R4"))
        assertEquals("GPIO Extender 1", v1.parseInfo("~IGPIO Extender 1"))
    }

    @Test
    fun `v2 commands use crlf and responses tolerate echo marker`() {
        assertEquals("~G1\r\n", v2.command("G1").wireBytes.decodeToString())
        assertEquals("~E0\r\n", v2.normalizeConsoleCommand(" E0 ").wireBytes.decodeToString())
        assertTrue(v2.isSuccessful("~SOK\r\n", 'S'))
        assertEquals("GPIO-Extender-2", v2.parseInfo("~IGPIO-Extender-2\r\n"))
    }

    @Test
    fun `connector pin ten is encoded as zero`() {
        assertEquals(1, Extender2Protocol.connectorPinToProtocol(1))
        assertEquals(9, Extender2Protocol.connectorPinToProtocol(9))
        assertEquals(0, Extender2Protocol.connectorPinToProtocol(10))
        assertThrows(IllegalArgumentException::class.java) {
            Extender2Protocol.connectorPinToProtocol(11)
        }
    }

    @Test
    fun `mode capabilities include both dht variants and swd unchanged`() {
        assertTrue(PinMode.DHT22.supports(10))
        assertTrue(PinMode.DHT11.supports(1))
        assertEquals(listOf(2, 3, 6, 7), (1..10).filter(PinMode.PWM::supports))
        assertEquals(listOf(8, 10), (1..10).filter(PinMode.UNCHANGED::supports))

        val modes = listOf(
            PinMode.ANALOG,
            PinMode.PWM,
            PinMode.INPUT,
            PinMode.WS2812,
            PinMode.INPUT,
            PinMode.UART_RX,
            PinMode.DHT11,
            PinMode.UNCHANGED,
            PinMode.CAPTURE,
            PinMode.UNCHANGED,
        )
        assertEquals("APIWIRd-C-", v2.encodeModes(modes))
        assertEquals(modes, v2.parseModes("~MAPIWIRd-C-\r\n"))
    }

    @Test
    fun `capture on pins four and five is rejected`() {
        val modes = MutableList(10) { PinMode.INPUT }
        modes[3] = PinMode.CAPTURE
        modes[4] = PinMode.CAPTURE
        assertThrows(IllegalArgumentException::class.java) { v2.encodeModes(modes) }
    }

    @Test
    fun `pwm validation accepts only complete in-range integers`() {
        assertEquals(0, Extender2Protocol.validatePwmDuty("0"))
        assertEquals(100, Extender2Protocol.validatePwmDuty("100"))
        assertEquals(1, Extender2Protocol.validatePwmFrequency("1"))
        assertEquals(20_000, Extender2Protocol.validatePwmFrequency("20000"))
        listOf("-1", "101", "50.5", "50foo", "").forEach {
            assertThrows(IllegalArgumentException::class.java) { Extender2Protocol.validatePwmDuty(it) }
        }
        listOf("0", "20001", "1000.5", "1kHz", "").forEach {
            assertThrows(IllegalArgumentException::class.java) { Extender2Protocol.validatePwmFrequency(it) }
        }
    }

    @Test
    fun `mode-specific values are parsed without losing precision`() {
        val adc = v2.parsePinValue(PinUiState(1, PinMode.ANALOG), "~G4095\r\n")
        assertEquals(4095, adc.analogValue)
        val capture = v2.parsePinValue(PinUiState(4, PinMode.CAPTURE), "G1234,57")
        assertEquals(1234.0, capture.captureFrequencyHz ?: -1.0, 0.0)
        assertEquals(57.0, capture.captureDutyPercent ?: -1.0, 0.0)
        val dht = v2.parsePinValue(PinUiState(3, PinMode.DHT22), "G-12.3C,45.6%")
        assertEquals(-12.3, dht.temperatureC ?: 0.0, 0.001)
        assertEquals(45.6, dht.humidityPercent ?: 0.0, 0.001)
        assertFalse(v2.parsePinValue(PinUiState(2, PinMode.INPUT), "G0").digitalValue!!)
    }

    @Test
    fun `console normalization rejects empty multiline and non-ascii commands`() {
        assertEquals("~I\r\n", v2.normalizeConsoleCommand("~I").wireBytes.decodeToString())
        assertThrows(IllegalArgumentException::class.java) { v2.normalizeConsoleCommand(" ") }
        assertThrows(IllegalArgumentException::class.java) { v2.normalizeConsoleCommand("G1\nG2") }
        assertThrows(IllegalArgumentException::class.java) { v2.normalizeConsoleCommand("привет") }
    }
}
