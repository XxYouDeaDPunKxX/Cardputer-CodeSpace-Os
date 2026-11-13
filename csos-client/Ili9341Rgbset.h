#pragma once

#include <stdint.h>

// ILI9341 RGBSET (0x2D): explicit RGB565-to-RGB666 lookup table.
// Hardware GRAM loopback confirmed these exact 128 values recover full
// colors when the panel's internal lookup table is in an incorrect state.
// All 128 bytes must follow the command in one uninterrupted SPI transaction.
template<class Display>
void initializeIli9341Rgbset(Display& tft) {
  tft.startWrite();
  tft.writeCommand(uint8_t{0x2D});
  for (uint8_t i = 0; i < 32; ++i) tft.writeData(uint8_t((i << 1) | (i >> 4)));
  for (uint8_t i = 0; i < 64; ++i) tft.writeData(i);
  for (uint8_t i = 0; i < 32; ++i) tft.writeData(uint8_t((i << 1) | (i >> 4)));
  tft.endWrite();
}
