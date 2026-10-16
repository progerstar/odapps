package ru.opendev.odtemplogger

import android.app.Application

class LoggerApplication : Application() {
    val controller by lazy { LoggerController(this) }
    override fun onCreate() {
        super.onCreate()
        controller.initialize()
    }
}
