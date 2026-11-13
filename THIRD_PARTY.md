# Third-party components

- The CXL client and server are derived from `szuszjan/cardputer-xl-remote`, commit
  `0484fabb4b45d9740bf81094b8ade9c64fcc39f6`. The server included here is
  modified for CS OS. Its SHA-256 is
  `a066f09e3539bf540ae2bd0a8a2e71cd949f809a58128c4eebbb6ba4b5b04b15`.
  The upstream commit was recorded in the development laboratory as having no
  license file at its root. This attribution records the code's provenance.
- `vendor/WebSockets`: arduinoWebSockets 2.7.2, with its license included. Local
  changes: maximum ESP32 payload of 4096 bytes; idle read timeout of 250 ms and
  total deadline of 500 ms; connection/header timeout remains 5000 ms.
- Font4 from M5GFX 0.2.23, `vendor/fonts/Font32rle.h`, with the M5GFX license included.
- `Panel_ILI9341.hpp`: LovyanGFX; FreeBSD attributions retained in the file.
- Espressif esp_lcd_touch 1.2.1 driver: original Apache-2.0 SPDX headers retained.
  Atanisoft XPT2046 1.0.6 driver: original headers and attributions retained.
- Other Arduino and Python libraries are installed from their respective distributions
  at the specified versions; see the setup guide and `server/requirements.lock`.
- `CaBundle.h` contains public CA certificates only, not private keys.

No blanket license is added to the derived sources in this extraction.

## Bundled XPT2046_Touchscreen 1.4

Paul Stoffregen's MIT license is retained in the source headers under
`vendor/XPT2046_Touchscreen`. Per-instance pressure threshold and SPI frequency
extensions are described in `CSOS-NOTES.md`; upstream defaults of 400 and 2 MHz
are preserved. https://github.com/PaulStoffregen/XPT2046_Touchscreen

The `desktop/CSOS-Greybird` CSS imports the system-installed Greybird theme; it
contains no copied Greybird theme assets. Greybird: https://github.com/shimmerproject/Greybird .
