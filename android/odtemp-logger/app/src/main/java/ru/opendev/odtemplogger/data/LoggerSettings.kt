package ru.opendev.odtemplogger.data

import android.annotation.SuppressLint
import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.net.URI
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

data class LoggerSettings(
    val logEnabled: Boolean = true,
    val logPeriodMs: Long = 60_000,
    val cloudEnabled: Boolean = false,
    val cloudPeriodMs: Long = 60_000,
    val cloudUrl: String = "https://cloud.unitx.pro",
    val deviceId: String = "odtemp-1",
    val token: String = "",
) {
    fun validate() {
        require(logPeriodMs in 100..86_400_000) { "Интервал журнала: от 0,1 до 86400 секунд" }
        require(cloudPeriodMs in 100..86_400_000) { "Интервал облака: от 0,1 до 86400 секунд" }
        if (!cloudEnabled) return
        require(deviceId.matches(Regex("[A-Za-z0-9_-]{1,64}"))) { "ID устройства: 1–64 символа A–Z, a–z, 0–9, _ или -" }
        require(token.matches(Regex("utx1_[A-Za-z0-9_-]{86}"))) { "Нужен write-токен UnitX Cloud (utx1_ и 86 символов)" }
        val uri = try { URI(cloudUrl) } catch (_: Exception) { null }
        require(uri != null && uri.scheme == "https" && !uri.host.isNullOrBlank() &&
            uri.rawUserInfo == null && uri.rawQuery == null && uri.rawFragment == null) {
            "Адрес облака: HTTPS без логина, параметров и фрагмента"
        }
    }
}

/** Token is encrypted by an app-specific Android Keystore key; backups are disabled. */
class SettingsStore(context: Context) {
    private val preferences = context.getSharedPreferences("logger", Context.MODE_PRIVATE)
    private fun key(): SecretKey {
        val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (store.getKey("cloud-token", null) as? SecretKey)?.let { return it }
        return KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore").apply {
            init(KeyGenParameterSpec.Builder("cloud-token", KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).build())
        }.generateKey()
    }

    fun load(): LoggerSettings {
        val token = runCatching {
            val encoded = preferences.getString("token", null) ?: return@runCatching ""
            val bytes = Base64.decode(encoded, Base64.NO_WRAP)
            val cipher = Cipher.getInstance("AES/GCM/NoPadding")
            cipher.init(Cipher.DECRYPT_MODE, key(), GCMParameterSpec(128, bytes.copyOfRange(0, 12)))
            cipher.doFinal(bytes.copyOfRange(12, bytes.size)).toString(Charsets.UTF_8)
        }.getOrDefault("")
        return LoggerSettings(
            preferences.getBoolean("logging", true), preferences.getLong("period", 60_000),
            preferences.getBoolean("cloud", false) && token.isNotEmpty(),
            preferences.getLong("cloudPeriod", 60_000),
            preferences.getString("url", "https://cloud.unitx.pro")!!,
            preferences.getString("device", "odtemp-1")!!, token,
        )
    }

    @SuppressLint("UseKtx") // commit() is deliberately checked for persistent-storage errors.
    fun save(settings: LoggerSettings) {
        settings.validate()
        val encrypted = if (settings.token.isEmpty()) "" else {
            val cipher = Cipher.getInstance("AES/GCM/NoPadding")
            cipher.init(Cipher.ENCRYPT_MODE, key())
            Base64.encodeToString(cipher.iv + cipher.doFinal(settings.token.toByteArray()), Base64.NO_WRAP)
        }
        check(preferences.edit().putBoolean("logging", settings.logEnabled).putLong("period", settings.logPeriodMs)
            .putBoolean("cloud", settings.cloudEnabled).putLong("cloudPeriod", settings.cloudPeriodMs)
            .putString("url", settings.cloudUrl).putString("device", settings.deviceId).putString("token", encrypted).commit()) {
            "Не удалось сохранить настройки"
        }
    }
}
