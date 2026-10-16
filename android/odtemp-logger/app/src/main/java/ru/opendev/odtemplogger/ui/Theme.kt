package ru.opendev.odtemplogger.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

@Composable
fun LoggerTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = if (isSystemInDarkTheme()) darkColorScheme(
        primary = Color(0xFF77D9BE), secondary = Color(0xFFA9CADD), tertiary = Color(0xFFEFB998),
    ) else lightColorScheme(
        primary = Color(0xFF006B56), secondary = Color(0xFF396478), tertiary = Color(0xFF8E4F26),
        background = Color(0xFFF6FAF8), surface = Color(0xFFF6FAF8),
    ), content = content)
}
