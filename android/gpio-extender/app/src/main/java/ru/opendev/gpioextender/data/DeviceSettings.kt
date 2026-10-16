package ru.opendev.gpioextender.data

import android.content.Context
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map
import ru.opendev.gpioextender.model.DeviceType

internal interface DeviceSettings {
    val deviceType: Flow<DeviceType>
    suspend fun setDeviceType(type: DeviceType)
}

private val Context.dataStore by preferencesDataStore(name = "gpio_extender_settings")

internal class DataStoreDeviceSettings(context: Context) : DeviceSettings {
    private val store = context.applicationContext.dataStore

    override val deviceType: Flow<DeviceType> = store.data.map { preferences ->
        preferences[DEVICE_TYPE_KEY]
            ?.let { stored -> DeviceType.entries.firstOrNull { it.name == stored } }
            ?: DeviceType.EXTENDER_1
    }

    override suspend fun setDeviceType(type: DeviceType) {
        store.edit { it[DEVICE_TYPE_KEY] = type.name }
    }

    private companion object {
        val DEVICE_TYPE_KEY = stringPreferencesKey("device_type")
    }
}
