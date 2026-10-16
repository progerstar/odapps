package ru.opendev.odtemplogger

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertTextEquals
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import ru.opendev.odtemplogger.protocol.Sample
import ru.opendev.odtemplogger.ui.LoggerTheme
import ru.opendev.odtemplogger.ui.MonitorScreen

class MonitorScreenTest {
    @get:Rule val compose = createComposeRule()

    @Test fun waitingScreenHasNoInventedMeasurements() {
        var connect = false
        compose.setContent { LoggerTheme { MonitorScreen(LoggerState(settingsReady = true), { connect = true }, {}) } }
        compose.onNodeWithTag("temperature").assertTextEquals("— °C")
        compose.onNodeWithTag("humidity").assertDoesNotExist()
        compose.onNodeWithTag("connect").performClick()
        assertTrue(connect)
    }

    @Test fun temperatureOnlyDeviceDoesNotShowHumidity() {
        compose.setContent { LoggerTheme {
            MonitorScreen(LoggerState(running = true, sample = Sample(-2.25, null)), {}, {})
        } }
        compose.onNodeWithTag("temperature").assertTextEquals("-2,25 °C")
        compose.onNodeWithTag("humidity").assertDoesNotExist()
    }

    @Test fun humidityAndStopAreAvailableForDhtDevice() {
        var stopped = false
        compose.setContent { LoggerTheme {
            MonitorScreen(LoggerState(running = true, sample = Sample(25.0, 50.0)), {}, { stopped = true })
        } }
        compose.onNodeWithTag("humidity").assertTextEquals("50,00 %")
        compose.onNodeWithText("Остановить").assertIsDisplayed().performClick()
        assertTrue(stopped)
    }

    @Test fun humidityIsVisibleOnTheSameChartWithPercentScale() {
        val samples = listOf(Sample(25.0, 40.0, 1000), Sample(26.0, 60.0, 2000))
        compose.setContent { LoggerTheme {
            MonitorScreen(LoggerState(running = true, sample = samples.last(), recent = samples), {}, {})
        } }
        compose.onNodeWithTag("measurement-chart").performScrollTo().assertIsDisplayed()
        compose.onNodeWithText("Температура, °C").assertIsDisplayed()
        compose.onNodeWithText("Влажность, %").assertIsDisplayed()
    }

    @Test fun temperatureOnlyHistoryHasNoHumidityChart() {
        val samples = listOf(Sample(-2.25, null, 1000), Sample(-2.0, null, 2000))
        compose.setContent { LoggerTheme {
            MonitorScreen(LoggerState(running = true, sample = samples.last(), recent = samples), {}, {})
        } }
        compose.onNodeWithTag("measurement-chart").assertExists()
        compose.onNodeWithText("Влажность, %").assertDoesNotExist()
    }
}
