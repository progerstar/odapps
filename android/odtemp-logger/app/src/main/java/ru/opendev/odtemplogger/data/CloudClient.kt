package ru.opendev.odtemplogger.data

import ru.opendev.odtemplogger.protocol.Sample
import java.net.HttpURLConnection
import java.net.URL

class CloudClient(private val settings: LoggerSettings) {
    init { settings.copy(cloudEnabled = true).validate() }

    companion object {
        fun payload(sample: Sample): String = buildString {
            require(sample.temperature.isFinite() && sample.humidity?.isFinite() != false)
            append("{\"ty\":1,\"v1\":${sample.temperature}")
            sample.humidity?.let { append(",\"v2\":$it") }
            append('}')
        }
    }

    fun post(sample: Sample) {
        val connection = URL("${settings.cloudUrl.trimEnd('/')}/state/${settings.deviceId}").openConnection() as HttpURLConnection
        try {
            connection.requestMethod = "POST"
            connection.connectTimeout = 10_000
            connection.readTimeout = 10_000
            // Never forward a write-token to a redirect target.
            connection.instanceFollowRedirects = false
            connection.setRequestProperty("Authorization", "Bearer ${settings.token}")
            connection.setRequestProperty("Content-Type", "application/json")
            connection.doOutput = true
            connection.outputStream.use { it.write(payload(sample).toByteArray(Charsets.UTF_8)) }
            check(connection.responseCode in 200..299) { "Облако: HTTP ${connection.responseCode}" }
        } finally {
            connection.disconnect()
        }
    }
}
