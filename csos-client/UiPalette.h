#pragma once
#include <cstdint>
namespace UiPalette {
constexpr int HudContentHeight=113;
// Exact RGB332 levels survive the HUD's existing 8-bit sprite unchanged.
// Expansion matches the pinned M5GFX colortype.hpp RGB332 -> RGB565 conversion.
constexpr uint16_t from332(uint8_t c){
 const uint16_t r=((c>>5)&7)*4+(((c>>5)&7)>>1);
 const uint16_t g=((c>>2)&7)*9;
 const uint16_t b=(c&3)*10+((c&3)>>1);
 return uint16_t((r<<11)|(g<<5)|b);
}
constexpr uint16_t Background=from332(0x00),Ink=from332(0xFF),Muted=from332(0xB6),Rule=from332(0x49);
constexpr uint16_t KeyFace=from332(0xDA),KeyInk=from332(0x00);
constexpr uint16_t Orange=from332(0xEC),Blue=from332(0x33),Green=from332(0x3A),White=from332(0xFF);
constexpr uint16_t FnFace=from332(0xC4),AaFace=from332(0x0F);
constexpr uint16_t Mouse=from332(0xAF),Keys=from332(0x37),View=from332(0x59),Warning=from332(0xF4);
}
