package ru.opendev.odtemplogger

import org.junit.Assert.*
import org.junit.Test
import ru.opendev.odtemplogger.protocol.SampleGate
import ru.opendev.odtemplogger.protocol.SensorProtocol

class SensorProtocolTest {
    @Test fun acceptsCurrentOdtempProductNames() {
        assertTrue(SensorProtocol.supportsProductName("UNITX ODTEMP-1 NST1001"))
        assertTrue(SensorProtocol.supportsProductName("UNITX ODTEMP-1 DHT"))
    }

    @Test fun acceptsLegacyNst1001ProductNameReadFromPhone() {
        assertTrue(SensorProtocol.supportsProductName("UNITX USB NST1001"))
        assertTrue(SensorProtocol.supportsProductName("unitx usb nst1001"))
    }

    @Test fun rejectsOtherUnitxDevicesSharingTheSamePid() {
        listOf(null, "", "UNITX USB GPIO Extender", "UNITX ODRFID", "UNITX USB").forEach {
            assertFalse(SensorProtocol.supportsProductName(it))
        }
    }

    @Test fun temperatureOnly() {
        val sample = SensorProtocol.sample(byteArrayOf(1, 0xC4.toByte(), 9), at = 123)!!
        assertEquals(25.0, sample.temperature, 0.0)
        assertNull(sample.humidity)
        assertEquals(123L, sample.at)
    }

    @Test fun negativeTemperatureAndHumidity() {
        val sample = SensorProtocol.sample(byteArrayOf(1, 0x38, 0xFF.toByte(), 0x88.toByte(), 0x13))!!
        assertEquals(-2.0, sample.temperature, 0.0)
        assertEquals(50.0, sample.humidity!!, 0.0)
    }

    @Test fun ignoresSensorNotReadySentinelInsteadOfPlottingMinus327() {
        assertNull(SensorProtocol.sample(byteArrayOf(1, 0, 0x80.toByte())))
        assertNull(SensorProtocol.sample(byteArrayOf(1, 0, 0x80.toByte(), 0, 0x80.toByte())))
        assertNull(SensorProtocol.sample(byteArrayOf(1, 0xC4.toByte(), 9, 0, 0x80.toByte())))
        assertNull(SensorProtocol.sample(byteArrayOf(1, 0, 0x80.toByte(), 0x88.toByte(), 0x13)))
    }

    @Test fun acceptsZeroTemperatureAndZeroHumidity() {
        val sample = SensorProtocol.sample(byteArrayOf(1, 0, 0, 0, 0))!!
        assertEquals(0.0, sample.temperature, 0.0)
        assertEquals(0.0, sample.humidity!!, 0.0)
    }

    @Test fun rejectsOtherReportIdsAndTruncatedPackets() {
        listOf(byteArrayOf(), byteArrayOf(1), byteArrayOf(1, 0), byteArrayOf(1, 1, 2, 3),
            byteArrayOf(2, 1, 2), byteArrayOf(3, 1, 2), byteArrayOf(4, 1, 2)).forEach {
            assertNull(SensorProtocol.sample(it))
        }
    }

    @Test fun respectsTransferLengthInsteadOfBufferCapacity() {
        assertNull(SensorProtocol.sample(ByteArray(64), 100))
        val buffer = ByteArray(64).also { it[0] = 1; it[1] = 0x64 }
        assertNull(SensorProtocol.sample(buffer))
        assertEquals(1.0, SensorProtocol.sample(buffer, 3)!!.temperature, 0.0)
        assertNull(SensorProtocol.sample(buffer, 3)!!.humidity)
    }

    @Test fun decodesFirmwareWithBoundedLength() {
        assertEquals("1.15S", SensorProtocol.firmware(byteArrayOf(3, 5) + "1.15S".toByteArray()))
        assertNull(SensorProtocol.firmware(byteArrayOf(3, 10, 65)))
        assertNull(SensorProtocol.firmware(byteArrayOf(3, 0)))
    }

    @Test fun changingIntervalPreservesOtherFeatureFields() {
        val original = byteArrayOf(4, 0xD0.toByte(), 7, 0, 0, 0x56, 0x12, 0x7F)
        assertEquals(2000L, SensorProtocol.interval(original))
        val updated = SensorProtocol.withInterval(original, 100)
        assertEquals(100L, SensorProtocol.interval(updated))
        assertArrayEquals(original.copyOfRange(5, 8), updated.copyOfRange(5, 8))
        assertEquals(2000L, SensorProtocol.interval(original))
    }

    @Test fun unsigned32BitInterval() {
        assertEquals(0xffffffffL, SensorProtocol.interval(byteArrayOf(4, -1, -1, -1, -1)))
    }

    @Test(expected = IllegalArgumentException::class) fun rejectsWrongFeatureId() {
        SensorProtocol.withInterval(byteArrayOf(1, 0, 0, 0, 0), 1000)
    }

    @Test fun recordsOnlyOneIncomingSamplePerPeriod() {
        val gate = SampleGate(1000)
        assertTrue(gate.accept(42))
        assertFalse(gate.accept(42))
        assertFalse(gate.accept(1041))
        assertTrue(gate.accept(1042))
    }

    @Test fun largeGapDoesNotReplayMissedSlots() {
        val gate = SampleGate(1000)
        assertTrue(gate.accept(0))
        assertTrue(gate.accept(60_000))
        assertFalse(gate.accept(60_001))
        // A new connection starts a new gate and can record the first fresh sample immediately.
        assertTrue(SampleGate(1000).accept(60_001))
    }
}
