package ru.opendev.odtemplogger

import org.junit.Assert.*
import org.junit.Test
import ru.opendev.odtemplogger.data.CloudClient
import ru.opendev.odtemplogger.data.CsvFormat
import ru.opendev.odtemplogger.data.CsvLog
import ru.opendev.odtemplogger.data.LoggerSettings
import ru.opendev.odtemplogger.protocol.Sample
import java.io.ByteArrayOutputStream
import java.nio.file.Files
import java.util.Locale
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

class CloudAndLogTest {
    private val settings = LoggerSettings(cloudEnabled = true, token = "utx1_" + "A".repeat(86))

    @Test fun defaultSettingsAreValidOffline() = LoggerSettings().validate()
    @Test fun validCloudConfiguration() = settings.validate()

    @Test fun invalidCloudInputsAreRejected() {
        listOf(settings.copy(cloudUrl = "http://cloud.unitx.pro"), settings.copy(cloudUrl = "https://a.test/?key=x"),
            settings.copy(cloudUrl = "https://user:password@a.test"), settings.copy(cloudUrl = "https://a.test/#x"),
            settings.copy(cloudUrl = "file:///tmp/cloud"), settings.copy(deviceId = "room/01"),
            settings.copy(deviceId = "x".repeat(65)), settings.copy(token = "utx1_short"),
            settings.copy(token = "utx1_" + "!".repeat(86)), settings.copy(logPeriodMs = 0),
            settings.copy(cloudPeriodMs = 99), settings.copy(logPeriodMs = Long.MAX_VALUE)).forEach {
            assertThrows(IllegalArgumentException::class.java) { it.validate() }
        }
    }

    @Test fun temperatureOnlyDoesNotInventHumidity() {
        assertEquals("{\"ty\":1,\"v1\":-2.0}", CloudClient.payload(Sample(-2.0, null)))
    }

    @Test fun humidityMatchesDesktopCloudFormat() {
        assertEquals("{\"ty\":1,\"v1\":25.0,\"v2\":50.0}", CloudClient.payload(Sample(25.0, 50.0)))
    }

    @Test fun csvHasDecimalPointEvenInRussianLocale() {
        val previous = Locale.getDefault()
        try {
            Locale.setDefault(Locale.forLanguageTag("ru-RU"))
            assertTrue(CsvFormat.row(Sample(-2.25, 56.75, 0)).endsWith(",-2.25,56.75\n"))
            assertTrue(CsvFormat.row(Sample(0.0, null, 0)).endsWith(",0.00,\n"))
        } finally { Locale.setDefault(previous) }
    }

    @Test fun logsSurviveRecreationAndExportIncludesHeader() {
        val dir = Files.createTempDirectory("odtemp-logs").toFile()
        try {
            val log = CsvLog(dir)
            val file = log.start()
            log.append(Sample(25.0, null, 0))
            val recreated = CsvLog(dir)
            assertEquals(listOf(file), recreated.files())
            val output = ByteArrayOutputStream()
            recreated.export(file, output)
            val text = output.toString("UTF-8")
            assertTrue(text.startsWith(CsvFormat.HEADER))
            assertEquals(2, text.lines().filter { it.isNotEmpty() }.size)
            assertTrue(text.contains(",25.00,\n"))
        } finally { dir.deleteRecursively() }
    }

    @Test fun exportCannotReadAnUnrelatedFile() {
        val dir = Files.createTempDirectory("odtemp-logs").toFile()
        try {
            assertThrows(IllegalArgumentException::class.java) {
                CsvLog(dir).export(java.io.File(dir, "../secret.csv"), ByteArrayOutputStream())
            }
        } finally { dir.deleteRecursively() }
    }

    @Test fun slowExportDoesNotBlockRecordingOrIncludeLaterRows() {
        val dir = Files.createTempDirectory("odtemp-logs").toFile()
        val pool = Executors.newFixedThreadPool(2)
        val copying = CountDownLatch(1)
        val resume = CountDownLatch(1)
        try {
            val log = CsvLog(dir)
            val file = log.start()
            log.append(Sample(1.0, null, 0))
            val expected = file.readText()
            val output = object : ByteArrayOutputStream() {
                override fun write(bytes: ByteArray, offset: Int, length: Int) {
                    copying.countDown()
                    check(resume.await(5, TimeUnit.SECONDS))
                    super.write(bytes, offset, length)
                }
            }
            val export = pool.submit { log.export(file, output) }
            assertTrue(copying.await(5, TimeUnit.SECONDS))
            pool.submit { log.append(Sample(2.0, null, 1000)) }.get(2, TimeUnit.SECONDS)
            resume.countDown()
            export.get(5, TimeUnit.SECONDS)
            assertEquals(expected, output.toString("UTF-8"))
            assertTrue(file.readText().contains(",2.00,\n"))
        } finally { resume.countDown(); pool.shutdownNow(); dir.deleteRecursively() }
    }
}
