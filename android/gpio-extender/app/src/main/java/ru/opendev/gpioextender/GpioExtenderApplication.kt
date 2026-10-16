package ru.opendev.gpioextender

import android.app.Application
import ru.opendev.gpioextender.data.DataStoreDeviceSettings

internal class GpioExtenderApplication : Application() {
    val deviceSettings by lazy { DataStoreDeviceSettings(this) }
}
