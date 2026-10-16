package ru.opendev.odtemplogger.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Tab
import androidx.compose.material3.PrimaryTabRow
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import ru.opendev.odtemplogger.LoggerController
import ru.opendev.odtemplogger.LoggerState
import ru.opendev.odtemplogger.data.LoggerSettings
import ru.opendev.odtemplogger.protocol.Sample

// The interface currently has one language, including its numeric formatting.
private val displayLocale = Locale.forLanguageTag("ru-RU")
private fun number(value: Double) = String.format(displayLocale, "%.2f", value)
private fun time(at: Long) = SimpleDateFormat("HH:mm:ss", displayLocale).format(Date(at))

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun LoggerApp(controller: LoggerController, onConnect: (Int) -> Unit, onStop: () -> Unit, onExport: (File) -> Unit) {
    val state by controller.state.collectAsStateWithLifecycle()
    var tab by rememberSaveable { mutableIntStateOf(0) }
    var showDevices by remember { mutableStateOf(false) }
    Scaffold(topBar = { TopAppBar(title = {
        Column { Text("ODTemp Logger"); Text("Температура и влажность", style = MaterialTheme.typography.labelMedium) }
    }) }) { padding ->
        Column(Modifier.fillMaxSize().padding(padding)) {
            PrimaryTabRow(tab) {
                listOf("Показания", "Журналы", "Настройки").forEachIndexed { index, title ->
                    Tab(selected = tab == index, onClick = { tab = index }, text = { Text(title) })
                }
            }
            when (tab) {
                0 -> MonitorScreen(state, onConnect = {
                    controller.refresh()
                    if (controller.state.value.devices.size == 1) onConnect(controller.state.value.devices.single().id)
                    else showDevices = true
                }, onStop = onStop)
                1 -> LogsScreen(controller, state, onExport)
                2 -> if (state.settingsReady) SettingsScreen(state.settings, state.running, controller::saveSettings)
                    else Text("Загрузка настроек…", Modifier.padding(16.dp))
            }
        }
    }
    if (showDevices) AlertDialog(onDismissRequest = { showDevices = false }, title = { Text("Выберите датчик") },
        text = {
            Column {
                if (state.devices.isEmpty()) Text("Датчик не найден. Подключите ODTEMP через USB OTG и нажмите «Обновить».")
                state.devices.forEach { device ->
                    TextButton(onClick = { showDevices = false; onConnect(device.id) }) { Text(device.name) }
                }
            }
        }, confirmButton = { TextButton(onClick = controller::refresh) { Text("Обновить") } },
        dismissButton = { TextButton(onClick = { showDevices = false }) { Text("Закрыть") } })
}

@Composable
fun MonitorScreen(state: LoggerState, onConnect: () -> Unit, onStop: () -> Unit) {
    Column(Modifier.verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
        Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainer)) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(if (state.sample != null) "● Датчик подключён" else if (state.running) "○ Ожидание показаний" else "○ Наблюдение выключено",
                    color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.SemiBold)
                Text(state.status, style = MaterialTheme.typography.bodySmall)
                if (state.firmware.isNotEmpty()) Text("Прошивка ${state.firmware}", style = MaterialTheme.typography.bodySmall)
                if (state.running) {
                    OutlinedButton(onClick = onStop, modifier = Modifier.fillMaxWidth()) { Text("Остановить") }
                    if (state.sample == null && state.devices.isNotEmpty()) TextButton(onClick = onConnect) { Text("Разрешить USB") }
                } else Button(onClick = onConnect, enabled = state.settingsReady && !state.settingsSaving, modifier = Modifier.fillMaxWidth().testTag("connect")) { Text("Подключить") }
            }
        }
        state.error?.let { Text(it, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall) }
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(24.dp)) {
                Text("Температура", style = MaterialTheme.typography.titleMedium)
                Text(state.sample?.let { "${number(it.temperature)} °C" } ?: "— °C",
                    fontSize = 48.sp, fontWeight = FontWeight.Medium, modifier = Modifier.testTag("temperature"))
                state.sample?.humidity?.let {
                    Spacer(Modifier.height(16.dp))
                    Text("Влажность", style = MaterialTheme.typography.titleMedium)
                    Text("${number(it)} %", fontSize = 36.sp, modifier = Modifier.testTag("humidity"))
                }
                Text(state.sample?.let { "Обновлено ${time(it.at)}" } ?: "Нет свежих измерений", style = MaterialTheme.typography.bodySmall)
            }
        }
        if (state.recent.size >= 2) {
            MeasurementChart(state.recent)
        }
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("Запись и отправка", style = MaterialTheme.typography.titleMedium)
                Text(if (state.settings.logEnabled) "В журнале: ${state.recorded} измерений" else "Запись журнала отключена")
                if (state.logFile.isNotEmpty()) Text(state.logFile, style = MaterialTheme.typography.bodySmall)
                Text("UnitX Cloud: ${state.cloudStatus}", style = MaterialTheme.typography.bodySmall)
            }
        }
        if (!state.running) Text("Нужен ODTEMP-1, 1U, 1N или 1D и телефон с USB Host/OTG. Наблюдение продолжается при свёрнутом приложении.",
            style = MaterialTheme.typography.bodySmall)
    }
}

@Composable
private fun MeasurementChart(samples: List<Sample>) {
    val temperature = samples.map { it.at to it.temperature }
    val humidity = samples.mapNotNull { sample -> sample.humidity?.let { sample.at to it } }
    val temperatureScale = ChartScale.from(temperature.map { it.second })
    val humidityScale = if (humidity.size >= 2) ChartScale.from(humidity.map { it.second }) else null
    val start = samples.first().at
    val end = samples.last().at
    val duration = (end - start).coerceAtLeast(1)
    val temperatureColor = MaterialTheme.colorScheme.primary
    val humidityColor = MaterialTheme.colorScheme.secondary
    val grid = MaterialTheme.colorScheme.outlineVariant
    val description = "График температуры от ${number(temperatureScale.minimum)} до ${number(temperatureScale.maximum)} °C" +
        (humidityScale?.let { "; влажности от ${number(it.minimum)} до ${number(it.maximum)} %" } ?: "")
    Card(Modifier.fillMaxWidth().testTag("measurement-chart")) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(if (humidityScale != null) "Температура и влажность" else "Температура за последние измерения",
                style = MaterialTheme.typography.titleSmall)
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text("Температура, °C", color = temperatureColor, style = MaterialTheme.typography.labelSmall)
                if (humidityScale != null) Text("Влажность, %", color = humidityColor, style = MaterialTheme.typography.labelSmall)
            }
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                ChartAxis(temperatureScale, temperatureColor)
                Canvas(Modifier.weight(1f).height(140.dp).testTag("measurement-plot")
                    .semantics { contentDescription = description }) {
                    repeat(3) { i -> val y = size.height * i / 2; drawLine(grid, Offset(0f, y), Offset(size.width, y)) }
                    fun path(points: List<Pair<Long, Double>>, scale: ChartScale): Path = Path().apply {
                        points.forEachIndexed { index, point ->
                            val x = ((point.first - start).toDouble() / duration * size.width).toFloat().coerceIn(0f, size.width)
                            val y = (size.height * (1 - scale.fraction(point.second))).toFloat().coerceIn(0f, size.height)
                            if (index == 0) moveTo(x, y) else lineTo(x, y)
                        }
                    }
                    drawPath(path(temperature, temperatureScale), temperatureColor, style = Stroke(width = 2.dp.toPx()))
                    humidityScale?.let { scale ->
                        drawPath(path(humidity, scale), humidityColor, style = Stroke(width = 2.dp.toPx(),
                            pathEffect = PathEffect.dashPathEffect(floatArrayOf(8.dp.toPx(), 4.dp.toPx()))))
                    }
                }
                if (humidityScale != null) ChartAxis(humidityScale, humidityColor)
            }
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text(time(start), style = MaterialTheme.typography.labelSmall)
                Text(time(end), style = MaterialTheme.typography.labelSmall)
            }
        }
    }
}

@Composable
private fun ChartAxis(scale: ChartScale, color: Color) {
    Column(Modifier.width(52.dp).height(140.dp), verticalArrangement = Arrangement.SpaceBetween) {
        listOf(scale.maximum, scale.middle, scale.minimum).forEach { value ->
            Text(number(value), color = color, style = MaterialTheme.typography.labelSmall)
        }
    }
}

@Composable
private fun LogsScreen(controller: LoggerController, state: LoggerState, onExport: (File) -> Unit) {
    var files by remember { mutableStateOf<List<File>>(emptyList()) }
    var refresh by remember { mutableIntStateOf(0) }
    LaunchedEffect(refresh, state.logFile, state.running) { files = withContext(Dispatchers.IO) { controller.logs.files() } }
    Column(Modifier.verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text("Журналы измерений", style = MaterialTheme.typography.headlineSmall)
        Text("Выберите «Сохранить CSV», чтобы скопировать журнал в память телефона или отправить через доступное хранилище.", style = MaterialTheme.typography.bodyMedium)
        TextButton(onClick = { refresh++ }) { Text("Обновить список") }
        if (files.isEmpty()) Text("Журналов пока нет. Подключите датчик и включите запись в настройках.")
        files.forEach { file ->
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(16.dp)) {
                    Text(file.name, style = MaterialTheme.typography.titleSmall)
                    Text("${(file.length() / 1024.0).let { String.format(displayLocale, "%.1f", it) }} КБ", style = MaterialTheme.typography.bodySmall)
                    TextButton(onClick = { onExport(file) }) { Text("Сохранить CSV") }
                }
            }
        }
        Text("Журналы хранятся внутри приложения. Экспортируйте нужные файлы перед удалением приложения.", style = MaterialTheme.typography.bodySmall)
    }
}

@Composable
private fun SettingsScreen(settings: LoggerSettings, running: Boolean, save: (LoggerSettings, (String?) -> Unit) -> Unit) {
    var logging by rememberSaveable(settings) { mutableStateOf(settings.logEnabled) }
    var period by rememberSaveable(settings) { mutableStateOf((settings.logPeriodMs / 1000.0).toString()) }
    var cloud by rememberSaveable(settings) { mutableStateOf(settings.cloudEnabled) }
    var cloudPeriod by rememberSaveable(settings) { mutableStateOf((settings.cloudPeriodMs / 1000.0).toString()) }
    var url by rememberSaveable(settings) { mutableStateOf(settings.cloudUrl) }
    var device by rememberSaveable(settings) { mutableStateOf(settings.deviceId) }
    // A write-token must not be copied into Activity saved instance state.
    var token by remember(settings) { mutableStateOf(settings.token) }
    var message by remember { mutableStateOf<String?>(null) }
    var saving by remember { mutableStateOf(false) }
    val enabled = !running && !saving
    Column(Modifier.verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text("Настройки записи", style = MaterialTheme.typography.headlineSmall)
        if (running) Text("Остановите наблюдение, чтобы изменить настройки.", color = MaterialTheme.colorScheme.primary)
        Toggle("Записывать журнал CSV", logging, enabled) { logging = it }
        PeriodField("Интервал журнала, с", period, enabled) { period = it }
        HorizontalDivider()
        Text("UnitX Cloud", style = MaterialTheme.typography.titleLarge)
        Toggle("Отправлять показания", cloud, enabled) { cloud = it }
        if (cloud) {
            OutlinedTextField(url, { url = it }, label = { Text("Адрес облака (HTTPS)") }, enabled = enabled, singleLine = true, modifier = Modifier.fillMaxWidth())
            OutlinedTextField(device, { device = it }, label = { Text("ID устройства") }, enabled = enabled, singleLine = true, modifier = Modifier.fillMaxWidth())
            OutlinedTextField(token, { token = it }, label = { Text("Write-токен") }, enabled = enabled, singleLine = true,
                visualTransformation = PasswordVisualTransformation(), keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password), modifier = Modifier.fillMaxWidth())
            PeriodField("Интервал отправки, с", cloudPeriod, enabled) { cloudPeriod = it }
            Text("Отправляются только новые показания. При отсутствии сети история остаётся в CSV; автоматической загрузки пропущенных записей нет.", style = MaterialTheme.typography.bodySmall)
        }
        message?.let { Text(it, color = MaterialTheme.colorScheme.primary) }
        Button(enabled = enabled, modifier = Modifier.fillMaxWidth(), onClick = {
            fun milliseconds(value: String): Long {
                val seconds = value.trim().replace(',', '.').toDoubleOrNull()
                require(seconds != null && seconds.isFinite() && seconds in 0.1..86400.0) { "Интервал: от 0,1 до 86400 секунд" }
                return (seconds * 1000).toLong()
            }
            try {
                val updated = LoggerSettings(logging, milliseconds(period), cloud, milliseconds(cloudPeriod), url.trim(), device.trim(), token.trim())
                updated.validate()
                saving = true
                save(updated) { error -> saving = false; message = error ?: "Настройки сохранены" }
            } catch (error: Exception) { message = error.message }
        }) { Text(if (saving) "Сохранение…" else "Сохранить настройки") }
        Text("Интервалы: 0,1–86400 с. Скорость новых измерений зависит от модели датчика. При наблюдении телефон питает датчик и остаётся активным; расход батареи увеличивается.", style = MaterialTheme.typography.bodySmall)
    }
}

@Composable
private fun Toggle(label: String, checked: Boolean, enabled: Boolean, change: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
        Text(label, Modifier.weight(1f))
        Switch(checked, change, enabled = enabled, modifier = Modifier.semantics { contentDescription = label })
    }
}

@Composable
private fun PeriodField(label: String, value: String, enabled: Boolean, change: (String) -> Unit) {
    OutlinedTextField(value, change, label = { Text(label) }, enabled = enabled, singleLine = true,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal), modifier = Modifier.fillMaxWidth())
}
