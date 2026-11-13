# XPT2046_Touchscreen 1.4 — CS OS

Vendored from the installed Paul Stoffregen 1.4 library. MIT license retained
in both source files. Per-instance setPressureThreshold(), default 400,
replaces the fixed pressure gate for XY acquisition and touched().
setSPIFrequency() defaults to upstream 2 MHz; the normal client selects 1 MHz.
This candidate follows https://github.com/PaulStoffregen/XPT2046_Touchscreen/issues/60
(developer reports unstable ILI9341 2.8-inch modules recovered at 1 MHz).
ADC conversion order, dummy conversion, best-pair average and IRQ behavior
remain upstream. Normal CS OS configures 300; research builds retain 400.
Source: https://github.com/PaulStoffregen/XPT2046_Touchscreen
