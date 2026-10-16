package ru.opendev.gpioextender.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import ru.opendev.gpioextender.GpioViewModel
import ru.opendev.gpioextender.model.ConnectionState
import ru.opendev.gpioextender.model.ConsoleDirection
import ru.opendev.gpioextender.model.DeviceType
import ru.opendev.gpioextender.model.PinMode
import ru.opendev.gpioextender.model.PinUiState
import ru.opendev.gpioextender.model.UiState

internal data class UiActions(
    val selectDeviceType: (DeviceType) -> Unit = {},
    val selectUsbDevice: (Int) -> Unit = {},
    val retryUsb: () -> Unit = {},
    val setControlVisible: (Boolean) -> Unit = {},
    val refreshValues: () -> Unit = {},
    val setV1Output: (Int, Boolean) -> Unit = { _, _ -> },
    val setV2Output: (Int, Boolean) -> Unit = { _, _ -> },
    val setPwmFrequency: (String) -> Unit = {},
    val setPwm: (Int, Int) -> Unit = { _, _ -> },
    val setPinMode: (Int, PinMode) -> Unit = { _, _ -> },
    val saveConfiguration: () -> Unit = {},
    val sendUart: (Int, String) -> Unit = { _, _ -> },
    val sendConsole: (String) -> Unit = {},
    val clearConsole: () -> Unit = {},
    val dismissMessage: () -> Unit = {},
)

@Composable
internal fun GpioExtenderApp(viewModel: GpioViewModel) {
    val state by viewModel.uiState.collectAsStateWithLifecycle()
    GpioExtenderContent(
        state = state,
        actions = UiActions(
            selectDeviceType = viewModel::selectDeviceType,
            selectUsbDevice = viewModel::selectUsbDevice,
            retryUsb = viewModel::retryUsb,
            setControlVisible = viewModel::setControlVisible,
            refreshValues = viewModel::refreshValues,
            setV1Output = viewModel::setExtender1Output,
            setV2Output = viewModel::setExtender2Output,
            setPwmFrequency = viewModel::setPwmFrequency,
            setPwm = viewModel::setPwm,
            setPinMode = viewModel::setPinMode,
            saveConfiguration = viewModel::saveConfiguration,
            sendUart = viewModel::sendUart,
            sendConsole = viewModel::sendConsoleCommand,
            clearConsole = viewModel::clearConsole,
            dismissMessage = viewModel::dismissMessage,
        ),
    )
}

private data class Destination(val route: String, val label: String, val glyph: String)

private val ControlDestination = Destination("control", "Управление", "●")
private val ConfigurationDestination = Destination("configuration", "Конфигурация", "◆")
private val ConsoleDestination = Destination("console", "Консоль", ">_")
private val SettingsDestination = Destination("settings", "Настройки", "⚙")

@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun GpioExtenderContent(
    state: UiState,
    actions: UiActions = UiActions(),
) {
    val navController = rememberNavController()
    val backStackEntry by navController.currentBackStackEntryAsState()
    val currentRoute = backStackEntry?.destination?.route ?: ControlDestination.route
    val destinations = buildList {
        add(ControlDestination)
        if (state.deviceType == DeviceType.EXTENDER_2) add(ConfigurationDestination)
        add(ConsoleDestination)
        add(SettingsDestination)
    }
    val snackbar = remember { SnackbarHostState() }

    val lifecycleOwner = LocalLifecycleOwner.current
    DisposableEffect(lifecycleOwner, currentRoute) {
        val lifecycle = lifecycleOwner.lifecycle
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_RESUME -> actions.setControlVisible(
                    currentRoute == ControlDestination.route,
                )
                Lifecycle.Event.ON_PAUSE,
                Lifecycle.Event.ON_STOP,
                Lifecycle.Event.ON_DESTROY
                -> actions.setControlVisible(false)
                else -> Unit
            }
        }
        lifecycle.addObserver(observer)
        actions.setControlVisible(
            currentRoute == ControlDestination.route &&
                lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED),
        )
        onDispose {
            lifecycle.removeObserver(observer)
            actions.setControlVisible(false)
        }
    }
    LaunchedEffect(state.deviceType, currentRoute) {
        if (state.deviceType == DeviceType.EXTENDER_1 && currentRoute == ConfigurationDestination.route) {
            navController.navigate(ControlDestination.route) {
                popUpTo(navController.graph.findStartDestination().id) { inclusive = false }
                launchSingleTop = true
            }
        }
    }
    LaunchedEffect(state.userMessage) {
        state.userMessage?.let {
            snackbar.showSnackbar(it)
            actions.dismissMessage()
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Column {
                        Text("GPIO Extender", maxLines = 1, overflow = TextOverflow.Ellipsis)
                        Text(
                            state.deviceType.title,
                            style = MaterialTheme.typography.labelMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                },
                actions = {
                    if (state.operationInProgress) {
                        CircularProgressIndicator(Modifier.padding(end = 16.dp).size(22.dp), strokeWidth = 2.dp)
                    }
                },
            )
        },
        bottomBar = {
            NavigationBar {
                destinations.forEach { destination ->
                    NavigationBarItem(
                        selected = currentRoute == destination.route,
                        onClick = {
                            navController.navigate(destination.route) {
                                popUpTo(navController.graph.findStartDestination().id) { saveState = true }
                                launchSingleTop = true
                                restoreState = true
                            }
                        },
                        icon = { Text(destination.glyph) },
                        label = { Text(destination.label, maxLines = 1) },
                    )
                }
            }
        },
        snackbarHost = { SnackbarHost(snackbar) },
    ) { padding ->
        Column(Modifier.fillMaxSize().padding(padding)) {
            ConnectionPanel(state, actions)
            HorizontalDivider()
            NavHost(
                navController = navController,
                startDestination = ControlDestination.route,
                modifier = Modifier.weight(1f),
            ) {
                composable(ControlDestination.route) { ControlScreen(state, actions) }
                composable(ConfigurationDestination.route) { ConfigurationScreen(state, actions) }
                composable(ConsoleDestination.route) { ConsoleScreen(state, actions) }
                composable(SettingsDestination.route) { SettingsScreen(state, actions) }
            }
        }
    }
}

@Composable
private fun ConnectionPanel(state: UiState, actions: UiActions) {
    val connection = state.connectionState
    Card(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 6.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainer),
    ) {
        Column(Modifier.fillMaxWidth().padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            when (connection) {
                ConnectionState.Disconnected -> {
                    Text("GPIO Extender не найден", fontWeight = FontWeight.SemiBold)
                    TextButton(onClick = actions.retryUsb) { Text("Повторить поиск") }
                }
                ConnectionState.Searching -> Text("Поиск USB-устройства…")
                is ConnectionState.SelectionRequired -> {
                    Text("Подключено несколько устройств", fontWeight = FontWeight.SemiBold)
                    connection.devices.forEach { device ->
                        OutlinedButton(
                            onClick = { actions.selectUsbDevice(device.deviceId) },
                            modifier = Modifier.fillMaxWidth(),
                        ) { Text(device.displayName) }
                    }
                }
                is ConnectionState.PermissionRequired -> {
                    Text("Ожидание разрешения USB")
                    Text(connection.device.displayName, style = MaterialTheme.typography.bodySmall)
                }
                is ConnectionState.PermissionDenied -> {
                    Text("Доступ к USB отклонён", color = MaterialTheme.colorScheme.error)
                    Text(connection.device.displayName, style = MaterialTheme.typography.bodySmall)
                    TextButton(onClick = actions.retryUsb) { Text("Запросить снова") }
                }
                is ConnectionState.Connecting -> {
                    Text("Подключение…", fontWeight = FontWeight.SemiBold)
                    Text(connection.device.displayName, style = MaterialTheme.typography.bodySmall)
                }
                is ConnectionState.Connected -> {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        StatusDot(true)
                        Spacer(Modifier.size(8.dp))
                        Text("Подключено: ${connection.device.displayName}", fontWeight = FontWeight.SemiBold)
                    }
                    state.deviceInfo?.let { Text(it, style = MaterialTheme.typography.bodySmall) }
                }
                is ConnectionState.Error -> {
                    Text(connection.message, color = MaterialTheme.colorScheme.error)
                    TextButton(onClick = actions.retryUsb) { Text("Повторить") }
                }
            }
        }
    }
}

@Composable
private fun ControlScreen(state: UiState, actions: UiActions) {
    if (state.deviceType == DeviceType.EXTENDER_1) {
        Extender1Control(state, actions)
    } else {
        Extender2Control(state, actions)
    }
}

@Composable
private fun Extender1Control(state: UiState, actions: UiActions) {
    val connected = state.connectionState is ConnectionState.Connected
    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        SectionTitle("Выходы")
        state.v1Outputs.forEachIndexed { index, value ->
            Card(Modifier.fillMaxWidth()) {
                Row(
                    Modifier.fillMaxWidth().padding(16.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Column {
                        Text("Выход ${index + 1}", fontWeight = FontWeight.Medium)
                        Text(
                            when (value) {
                                true -> "Включён"
                                false -> "Выключен"
                                null -> "Состояние неизвестно"
                            },
                            style = MaterialTheme.typography.bodySmall,
                        )
                    }
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        OutlinedButton(
                            onClick = { actions.setV1Output(index, false) },
                            enabled = connected && !state.operationInProgress && value != false,
                        ) { Text("Выкл.") }
                        Button(
                            onClick = { actions.setV1Output(index, true) },
                            enabled = connected && !state.operationInProgress && value != true,
                            modifier = Modifier.semantics { contentDescription = "Выход ${index + 1}" },
                        ) { Text("Вкл.") }
                    }
                }
            }
        }
        SectionTitle("Входы")
        state.v1Inputs.forEachIndexed { index, value ->
            IndicatorRow("Вход ${index + 1}", value)
        }
        OutlinedButton(onClick = actions.refreshValues, enabled = connected) { Text("Обновить входы") }
        Text(
            "Команды I, P, служебные LED и перезагрузка доступны в консоли.",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
private fun Extender2Control(state: UiState, actions: UiActions) {
    val connected = state.connectionState is ConnectionState.Connected
    var frequencyText by remember(state.pwmFrequencyHz) { mutableStateOf(state.pwmFrequencyHz.toString()) }
    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            OutlinedTextField(
                value = frequencyText,
                onValueChange = { frequencyText = it.filter(Char::isDigit).take(5) },
                label = { Text("Общая частота PWM, Гц") },
                singleLine = true,
                modifier = Modifier.weight(1f),
            )
            Button(onClick = { actions.setPwmFrequency(frequencyText) }) { Text("Принять") }
        }
        Text("Допустимый диапазон: 1…20000 Гц", style = MaterialTheme.typography.bodySmall)
        state.pins.forEach { pin -> Extender2PinCard(pin, state, connected, actions) }
        OutlinedButton(onClick = actions.refreshValues, enabled = connected) { Text("Обновить сейчас") }
    }
}

@Composable
private fun Extender2PinCard(pin: PinUiState, state: UiState, connected: Boolean, actions: UiActions) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text("Пин ${pin.number}", fontWeight = FontWeight.SemiBold)
                Text(pin.mode.title, color = MaterialTheme.colorScheme.primary)
            }
            when (pin.mode) {
                PinMode.OUTPUT -> {
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
                        Text(if (pin.digitalValue == null) "Состояние неизвестно" else if (pin.digitalValue) "Высокий уровень" else "Низкий уровень")
                        Switch(
                            checked = pin.digitalValue == true,
                            onCheckedChange = { actions.setV2Output(pin.number, it) },
                            enabled = connected && !state.operationInProgress,
                        )
                    }
                }
                PinMode.INPUT -> DigitalValue(pin.digitalValue)
                PinMode.ANALOG -> Text("ADC: ${pin.analogValue?.toString() ?: "—"} / 4095")
                PinMode.PWM -> PwmControl(pin, connected && !state.operationInProgress, actions)
                PinMode.CAPTURE -> {
                    Text("Частота: ${pin.captureFrequencyHz?.formatNumber() ?: "—"} Гц")
                    Text("Скважность: ${pin.captureDutyPercent?.formatNumber() ?: "—"} %")
                }
                PinMode.UART_TX -> UartTxControl(pin.number, connected, actions)
                PinMode.UART_RX -> Text("RX: ${pin.uartText ?: "—"}", fontFamily = FontFamily.Monospace)
                PinMode.DHT22, PinMode.DHT11 -> {
                    Text("Температура: ${pin.temperatureC?.formatNumber() ?: "—"} °C")
                    Text("Влажность: ${pin.humidityPercent?.formatNumber() ?: "—"} %")
                }
                PinMode.WS2812 -> Text("Палитра/HEX отправляется командой W в консоли.")
                PinMode.UNCHANGED -> Text("Состояние пина не изменяется прошивкой.")
            }
            pin.error?.let { Text("Ошибка: $it", color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall) }
        }
    }
}

@Composable
private fun PwmControl(pin: PinUiState, enabled: Boolean, actions: UiActions) {
    var sliderValue by remember(pin.number, pin.pwmDuty) { mutableFloatStateOf((pin.pwmDuty ?: 0).toFloat()) }
    Text("Скважность: ${sliderValue.toInt()} %")
    Slider(
        value = sliderValue,
        onValueChange = { sliderValue = it },
        onValueChangeFinished = { actions.setPwm(pin.number, sliderValue.toInt()) },
        enabled = enabled,
        valueRange = 0f..100f,
        steps = 99,
    )
}

@Composable
private fun UartTxControl(pin: Int, enabled: Boolean, actions: UiActions) {
    var text by remember(pin) { mutableStateOf("") }
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
        OutlinedTextField(
            value = text,
            onValueChange = { text = it.replace("\r", "").replace("\n", "").take(50) },
            label = { Text("USART TX") },
            singleLine = true,
            modifier = Modifier.weight(1f),
        )
        Button(onClick = { actions.sendUart(pin, text) }, enabled = enabled && text.isNotEmpty()) { Text("TX") }
    }
}

@Composable
private fun ConfigurationScreen(state: UiState, actions: UiActions) {
    if (state.deviceType != DeviceType.EXTENDER_2) {
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            Text("Конфигурация доступна только для GPIO Extender 2")
        }
        return
    }
    val connected = state.connectionState is ConnectionState.Connected
    LazyColumn(
        Modifier.fillMaxSize().padding(horizontal = 16.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        item {
            Text(
                "Изменение режима сразу отправляет полную конфигурацию устройству.",
                modifier = Modifier.padding(vertical = 12.dp),
                style = MaterialTheme.typography.bodySmall,
            )
        }
        items(state.pins, key = PinUiState::number) { pin ->
            ModeSelector(pin, connected && !state.operationInProgress, actions)
        }
        item {
            Button(
                onClick = actions.saveConfiguration,
                enabled = connected && !state.operationInProgress,
                modifier = Modifier.fillMaxWidth().padding(vertical = 12.dp),
            ) { Text("Сохранить во flash") }
        }
    }
}

@Composable
private fun ModeSelector(pin: PinUiState, enabled: Boolean, actions: UiActions) {
    var expanded by remember { mutableStateOf(false) }
    Card(Modifier.fillMaxWidth()) {
        Row(
            Modifier.fillMaxWidth().padding(12.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column {
                Text("Пин ${pin.number}", fontWeight = FontWeight.Medium)
                Text("Текущий режим: ${pin.mode.title}", style = MaterialTheme.typography.bodySmall)
            }
            Box {
                OutlinedButton(
                    onClick = { expanded = true },
                    enabled = enabled,
                    modifier = Modifier.semantics { contentDescription = "Режим пина ${pin.number}" },
                ) { Text(pin.mode.code.toString()) }
                DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
                    PinMode.availableFor(pin.number).forEach { mode ->
                        DropdownMenuItem(
                            text = { Text("${mode.code} — ${mode.title}") },
                            onClick = {
                                expanded = false
                                actions.setPinMode(pin.number, mode)
                            },
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun ConsoleScreen(state: UiState, actions: UiActions) {
    var input by remember { mutableStateOf("") }
    val connected = state.connectionState is ConnectionState.Connected
    Column(Modifier.fillMaxSize().padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedTextField(
                value = input,
                onValueChange = { input = it.replace("\r", "").replace("\n", "") },
                label = { Text("Команда (~ необязательна)") },
                singleLine = true,
                modifier = Modifier.weight(1f),
            )
            Button(
                onClick = {
                    actions.sendConsole(input)
                    input = ""
                },
                enabled = connected && input.isNotBlank(),
            ) { Text("Отправить") }
        }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
            Text("Записей: ${state.consoleEntries.size} / 500", style = MaterialTheme.typography.bodySmall)
            TextButton(onClick = actions.clearConsole, enabled = state.consoleEntries.isNotEmpty()) { Text("Очистить") }
        }
        LazyColumn(
            Modifier.fillMaxWidth().weight(1f).background(MaterialTheme.colorScheme.surfaceContainerLowest).padding(8.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            items(state.consoleEntries, key = { it.id }) { entry ->
                val color = when (entry.direction) {
                    ConsoleDirection.COMMAND -> MaterialTheme.colorScheme.primary
                    ConsoleDirection.RESPONSE -> MaterialTheme.colorScheme.onSurface
                    ConsoleDirection.ERROR, ConsoleDirection.TIMEOUT -> MaterialTheme.colorScheme.error
                }
                Text(
                    "${TIME_FORMAT.format(Date(entry.timestampMillis))} ${entry.direction.marker} ${entry.text}",
                    color = color,
                    fontFamily = FontFamily.Monospace,
                    style = MaterialTheme.typography.bodySmall,
                )
            }
        }
    }
}

@Composable
private fun SettingsScreen(state: UiState, actions: UiActions) {
    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        SectionTitle("Тип устройства")
        Text(
            "Версия не определяется по USB: выберите тип устройства вручную. USB description показан в статусе, а выбранный протокол сохраняется в настройках.",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        DeviceType.entries.forEach { type ->
            Row(
                Modifier.fillMaxWidth().selectable(
                    selected = state.deviceType == type,
                    onClick = { actions.selectDeviceType(type) },
                ).padding(vertical = 10.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                RadioButton(selected = state.deviceType == type, onClick = null)
                Spacer(Modifier.size(8.dp))
                Column {
                    Text(type.title, fontWeight = FontWeight.Medium)
                    Text(
                        if (type == DeviceType.EXTENDER_1) "5 выходов и 5 входов" else "10 настраиваемых пинов",
                        style = MaterialTheme.typography.bodySmall,
                    )
                }
            }
        }
        HorizontalDivider(Modifier.padding(vertical = 8.dp))
        Text("Версия приложения 1.0.0", style = MaterialTheme.typography.bodySmall)
        Text("Работает локально через USB CDC ACM; доступ к интернету не требуется.", style = MaterialTheme.typography.bodySmall)
    }
}

@Composable
private fun SectionTitle(text: String) {
    Text(text, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
}

@Composable
private fun IndicatorRow(label: String, value: Boolean?) {
    Card(Modifier.fillMaxWidth()) {
        Row(Modifier.fillMaxWidth().padding(14.dp), verticalAlignment = Alignment.CenterVertically) {
            StatusDot(value)
            Spacer(Modifier.size(10.dp))
            Text(label, modifier = Modifier.weight(1f))
            Text(when (value) { true -> "1"; false -> "0"; null -> "—" })
        }
    }
}

@Composable
private fun DigitalValue(value: Boolean?) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        StatusDot(value)
        Spacer(Modifier.size(8.dp))
        Text(when (value) { true -> "Высокий уровень (1)"; false -> "Низкий уровень (0)"; null -> "Нет данных" })
    }
}

@Composable
private fun StatusDot(value: Boolean?) {
    val color = when (value) {
        true -> Color(0xFF2E7D32)
        false -> MaterialTheme.colorScheme.outline
        null -> Color(0xFFF9A825)
    }
    Box(
        Modifier.size(12.dp).background(color, shape = androidx.compose.foundation.shape.CircleShape)
            .semantics { contentDescription = when (value) { true -> "Активно"; false -> "Неактивно"; null -> "Неизвестно" } },
    )
}

private fun Double.formatNumber(): String = if (this % 1.0 == 0.0) toLong().toString() else "%.1f".format(this)

private val ConsoleDirection.marker: String
    get() = when (this) {
        ConsoleDirection.COMMAND -> ">"
        ConsoleDirection.RESPONSE -> "<"
        ConsoleDirection.ERROR -> "!"
        ConsoleDirection.TIMEOUT -> "⌛"
    }

private val TIME_FORMAT = SimpleDateFormat("HH:mm:ss.SSS", Locale.getDefault())
