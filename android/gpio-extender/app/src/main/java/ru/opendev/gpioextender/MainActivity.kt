package ru.opendev.gpioextender

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import ru.opendev.gpioextender.ui.GpioExtenderApp
import ru.opendev.gpioextender.ui.theme.GpioExtenderTheme

internal class MainActivity : ComponentActivity() {
    private val viewModel: GpioViewModel by viewModels { GpioViewModel.factory(application) }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            GpioExtenderTheme {
                GpioExtenderApp(viewModel)
            }
        }
    }
}
