package ru.opendev.odtemplogger.data

import ru.opendev.odtemplogger.protocol.Sample
import java.io.File
import java.io.OutputStream
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

object CsvFormat {
    const val HEADER = "timestamp,temperature_c,humidity_percent\n"
    fun row(sample: Sample): String {
        val time = SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss.SSSXXX", Locale.US).format(Date(sample.at))
        return "$time,${String.format(Locale.US, "%.2f", sample.temperature)},${sample.humidity?.let { String.format(Locale.US, "%.2f", it) } ?: ""}\n"
    }
}

class CsvLog(private val directory: File) {
    private var current: File? = null

    @Synchronized fun start(): File {
        check(directory.isDirectory || directory.mkdirs()) { "Не удалось создать папку журнала" }
        val file = File.createTempFile("odtemp_${SimpleDateFormat("yyyy-MM-dd_HH-mm-ss", Locale.US).format(Date())}_", ".csv", directory)
        file.writeText(CsvFormat.HEADER)
        current = file
        return file
    }

    @Synchronized fun append(sample: Sample) {
        val file = current ?: error("Журнал не открыт")
        file.appendText(CsvFormat.row(sample))
    }

    @Synchronized fun files(): List<File> = directory.listFiles()?.filter { it.extension == "csv" }
        ?.sortedByDescending { it.lastModified() } ?: emptyList()

    fun export(file: File, output: OutputStream) {
        // Freeze a complete row boundary without holding the writer lock during a slow SAF upload.
        val length = synchronized(this) {
            require(file.canonicalFile.parentFile == directory.canonicalFile && file.extension == "csv")
            file.length()
        }
        file.inputStream().use { input ->
            var remaining = length
            val buffer = ByteArray(8192)
            while (remaining > 0) {
                val count = input.read(buffer, 0, minOf(remaining, buffer.size.toLong()).toInt())
                check(count >= 0) { "Файл журнала изменился во время экспорта" }
                output.write(buffer, 0, count)
                remaining -= count
            }
        }
    }
}
