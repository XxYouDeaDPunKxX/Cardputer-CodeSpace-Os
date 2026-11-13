#pragma once
#include <cstdint>

// Raw register observation, not an assertion of transport success or LCD readiness.
// The ILI9341's indexed RDID4 bytes 2 and 3 encode the controller signature.
struct Ili9341Id4 {
  uint8_t bytes[4] = {};
  uint16_t controllerId() const {
    return (uint16_t(bytes[2]) << 8) | bytes[3];
  }
  bool matchesIli9341Signature() const { return controllerId() == 0x9341; }
};

// Real M5GFX Panel_ILI9341 primitive interface:
//   startWrite/endWrite, waitDMA, writeCommand(data, bytes),
//   writeData(data, bytes), readCommand(cmd, dummyBytes, length).
// IMPORTANT: M5GFX readCommand's "index" argument only inserts skip clocks;
// it does NOT select ILI9341 register index 0xD9. Make that selection explicit.
// Precondition: panel config dummy_read_bits=0 (verified by prior JTAG probe);
// no external panel write session in progress.
template<class Panel>
Ili9341Id4 readIli9341IndexedId4(Panel& panel) {
  Ili9341Id4 result;
  panel.waitDMA();
  panel.startWrite(); // retain the same SPI mutex through the complete sequence
  for (uint8_t index = 0; index < 4; ++index) {
    panel.writeCommand(0xD9, 1);
    panel.writeData(uint8_t(0x10 + index), 1);
    result.bytes[index] = uint8_t(panel.readCommand(0xD3, 0, 1));
  }
  // The index selector is controller state: leave it at the default index.
  panel.writeCommand(0xD9, 1);
  panel.writeData(0x10, 1);
  panel.endWrite();
  return result;
}
