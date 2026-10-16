package ru.opendev.odtemplogger

import android.Manifest
import android.content.Intent
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.lifecycle.lifecycleScope
import java.io.File
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import ru.opendev.odtemplogger.ui.LoggerApp
import ru.opendev.odtemplogger.ui.LoggerTheme

class MainActivity : ComponentActivity() {
    private val controller get() = (application as LoggerApplication).controller
    private var exportFile: String? = null
    private val notificationPermission = registerForActivityResult(ActivityResultContracts.RequestPermission()) { }
    private val export = registerForActivityResult(ActivityResultContracts.CreateDocument("text/csv")) { uri ->
        val name = exportFile
        exportFile = null
        if (uri != null && name != null) lifecycleScope.launch {
            try {
                withContext(Dispatchers.IO) {
                    val file = controller.logs.files().firstOrNull { it.name == name } ?: error("Файл журнала не найден")
                    contentResolver.openOutputStream(uri, "wt")?.use { controller.logs.export(file, it) }
                        ?: error("Не удалось открыть файл назначения")
                }
                android.widget.Toast.makeText(this@MainActivity, "Журнал сохранён", android.widget.Toast.LENGTH_SHORT).show()
            } catch (error: Exception) {
                android.widget.Toast.makeText(this@MainActivity, error.message ?: "Ошибка экспорта", android.widget.Toast.LENGTH_LONG).show()
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        exportFile = savedInstanceState?.getString("exportFile")
        enableEdgeToEdge()
        setContent {
            LoggerTheme {
                LoggerApp(controller,
                    onConnect = { id ->
                        if (Build.VERSION.SDK_INT >= 33) notificationPermission.launch(Manifest.permission.POST_NOTIFICATIONS)
                        controller.connect(id)
                    },
                    onStop = { stopService(Intent(this, LoggerService::class.java)) },
                    onExport = { file: File -> exportFile = file.name; export.launch(file.name) },
                )
            }
        }
    }

    override fun onResume() { super.onResume(); controller.refresh() }
    override fun onNewIntent(intent: Intent) { super.onNewIntent(intent); setIntent(intent); controller.refresh() }
    override fun onSaveInstanceState(outState: Bundle) {
        outState.putString("exportFile", exportFile)
        super.onSaveInstanceState(outState)
    }
}
