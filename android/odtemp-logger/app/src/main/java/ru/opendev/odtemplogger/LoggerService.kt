package ru.opendev.odtemplogger

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat

class LoggerService : Service() {
    private val controller get() = (application as LoggerApplication).controller
    private var wakeLock: PowerManager.WakeLock? = null
    private val handler = Handler(Looper.getMainLooper())
    private val renewWakeLock = object : Runnable {
        override fun run() {
            wakeLock?.acquire(10 * 60_000L)
            handler.postDelayed(this, 5 * 60_000L)
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP || intent == null) {
            stopSelf()
            return START_NOT_STICKY
        }
        val notificationManager = getSystemService(NotificationManager::class.java)
        if (Build.VERSION.SDK_INT >= 26) notificationManager.createNotificationChannel(
            NotificationChannel(CHANNEL, "Наблюдение за датчиком", NotificationManager.IMPORTANCE_LOW))
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE)
        val stop = PendingIntent.getService(this, 1, Intent(this, LoggerService::class.java).setAction(ACTION_STOP), PendingIntent.FLAG_IMMUTABLE)
        val notification = NotificationCompat.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.ic_thermometer).setContentTitle("ODTemp Logger")
            .setContentText("Наблюдение за USB-датчиком включено")
            .setContentIntent(open).setOngoing(true).addAction(0, "Остановить", stop).build()
        try {
            ServiceCompat.startForeground(this, 1, notification,
                if (Build.VERSION.SDK_INT >= 29) ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE else 0)
            if (wakeLock == null) {
                wakeLock = getSystemService(PowerManager::class.java).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "$packageName:monitor")
                    .apply { setReferenceCounted(false) }
                renewWakeLock.run()
            }
            controller.start(intent.getIntExtra("device", -1))
            if (!controller.state.value.running) stopSelf()
        } catch (error: Exception) {
            controller.startError(error.message ?: "Не удалось запустить службу")
            stopSelf()
        }
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        controller.stop()
        handler.removeCallbacks(renewWakeLock)
        wakeLock?.let { if (it.isHeld) it.release() }
        wakeLock = null
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    companion object {
        const val ACTION_STOP = "ru.opendev.odtemplogger.STOP"
        private const val CHANNEL = "sensor-monitor"
    }
}
