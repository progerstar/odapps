package ru.opendev.odtemplogger.ui

/** Each measurement keeps its own units; a constant series is centered instead of dividing by zero. */
data class ChartScale(val minimum: Double, val maximum: Double) {
    val middle get() = (minimum + maximum) / 2
    fun fraction(value: Double): Double = (value - minimum) / (maximum - minimum)

    companion object {
        fun from(values: List<Double>): ChartScale {
            require(values.isNotEmpty() && values.all { it.isFinite() })
            val low = values.min()
            val high = values.max()
            val middle = (low + high) / 2
            val span = (high - low).coerceAtLeast(1.0)
            return ChartScale(middle - span / 2, middle + span / 2)
        }
    }
}
