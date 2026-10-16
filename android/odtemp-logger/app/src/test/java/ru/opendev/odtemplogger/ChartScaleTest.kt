package ru.opendev.odtemplogger

import org.junit.Assert.*
import org.junit.Test
import ru.opendev.odtemplogger.ui.ChartScale

class ChartScaleTest {
    @Test fun constantReadingIsCenteredWithFiniteScale() {
        val scale = ChartScale.from(listOf(50.0, 50.0))
        assertEquals(0.5, scale.fraction(50.0), 0.0)
        assertTrue(scale.minimum < 50 && scale.maximum > 50)
    }

    @Test fun temperatureAndHumidityUseIndependentUnits() {
        val temperature = ChartScale.from(listOf(-2.0, 3.0))
        val humidity = ChartScale.from(listOf(40.0, 60.0))
        assertEquals(0.0, temperature.fraction(-2.0), 0.0)
        assertEquals(1.0, temperature.fraction(3.0), 0.0)
        assertEquals(0.0, humidity.fraction(40.0), 0.0)
        assertEquals(1.0, humidity.fraction(60.0), 0.0)
    }

    @Test fun smallVariationStaysInsideReadableScale() {
        val scale = ChartScale.from(listOf(25.2, 25.3))
        assertTrue(scale.fraction(25.2) in 0.0..1.0)
        assertTrue(scale.fraction(25.3) in 0.0..1.0)
        assertTrue(scale.maximum - scale.minimum >= 1.0)
    }
}
