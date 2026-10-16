package ru.opendev.gpioextender.ui

import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import java.util.concurrent.atomic.AtomicReference
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import ru.opendev.gpioextender.model.ConnectionState
import ru.opendev.gpioextender.model.ConsoleDirection
import ru.opendev.gpioextender.model.ConsoleEntry
import ru.opendev.gpioextender.model.DeviceType
import ru.opendev.gpioextender.model.PinMode
import ru.opendev.gpioextender.model.PinUiState
import ru.opendev.gpioextender.model.UiState
import ru.opendev.gpioextender.model.UsbDeviceDescriptor
import ru.opendev.gpioextender.ui.theme.GpioExtenderTheme

class GpioExtenderUiTest {
    @get:Rule
    val composeRule = createComposeRule()

    @Test
    fun navigation_hides_configuration_for_extender_one() {
        composeRule.setContent {
            GpioExtenderTheme { GpioExtenderContent(UiState(deviceType = DeviceType.EXTENDER_1)) }
        }

        composeRule.onNodeWithText("Управление").assertIsDisplayed()
        composeRule.onNodeWithText("Консоль").assertIsDisplayed()
        composeRule.onNodeWithText("Настройки").assertIsDisplayed()
        composeRule.onAllNodesWithText("Конфигурация").assertCountEquals(0)
    }

    @Test
    fun extender_two_configuration_has_pin_modes_and_save_action() {
        composeRule.setContent {
            GpioExtenderTheme { GpioExtenderContent(connectedState(DeviceType.EXTENDER_2)) }
        }

        composeRule.onNodeWithText("Конфигурация").performClick()
        composeRule.onNodeWithText("Изменение режима сразу отправляет полную конфигурацию устройству.")
            .assertIsDisplayed()
        composeRule.onNodeWithContentDescription("Режим пина 1").assertIsDisplayed()
    }

    @Test
    fun control_uses_mode_specific_widgets() {
        val pins = List(10) { index ->
            when (index + 1) {
                1 -> PinUiState(1, PinMode.ANALOG, analogValue = 2048)
                2 -> PinUiState(2, PinMode.PWM, pwmDuty = 42)
                else -> PinUiState(index + 1, PinMode.INPUT, digitalValue = false)
            }
        }
        composeRule.setContent {
            GpioExtenderTheme {
                GpioExtenderContent(connectedState(DeviceType.EXTENDER_2).copy(pins = pins))
            }
        }

        composeRule.onNodeWithText("ADC: 2048 / 4095").assertIsDisplayed()
        composeRule.onNodeWithText("Скважность: 42 %").assertIsDisplayed()
    }

    @Test
    fun settings_reports_selected_device_type() {
        val selected = AtomicReference<DeviceType>()
        composeRule.setContent {
            GpioExtenderTheme {
                GpioExtenderContent(
                    state = UiState(deviceType = DeviceType.EXTENDER_1),
                    actions = UiActions(selectDeviceType = selected::set),
                )
            }
        }

        composeRule.onNodeWithText("Настройки").performClick()
        composeRule.onNodeWithText("GPIO Extender 2").performClick()
        assertEquals(DeviceType.EXTENDER_2, selected.get())
    }

    @Test
    fun console_shows_manual_entries_and_can_be_cleared() {
        var cleared = false
        val state = connectedState(DeviceType.EXTENDER_1).copy(
            consoleEntries = listOf(
                ConsoleEntry(1, 0, ConsoleDirection.COMMAND, "~I"),
                ConsoleEntry(2, 1, ConsoleDirection.RESPONSE, "~IFake"),
            ),
        )
        composeRule.setContent {
            GpioExtenderTheme {
                GpioExtenderContent(state, UiActions(clearConsole = { cleared = true }))
            }
        }

        composeRule.onNodeWithText("Консоль").performClick()
        composeRule.onNodeWithText("~IFake", substring = true).assertIsDisplayed()
        composeRule.onNodeWithText("Очистить").performClick()
        assertEquals(true, cleared)
    }

    private fun connectedState(type: DeviceType): UiState {
        val device = UsbDeviceDescriptor(1, "Различимый USB description", hasPermission = true)
        return UiState(
            deviceType = type,
            connectionState = ConnectionState.Connected(device),
            deviceInfo = "Тестовое устройство",
        )
    }
}
