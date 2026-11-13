#pragma once

#ifndef CSOS_USB_CONSOLE
#define CSOS_USB_CONSOLE 0
#endif

#if CSOS_USB_CONSOLE
#include <Arduino.h>
#define CsosSerial Serial
#define CSOS_LOGF(...) Serial.printf(__VA_ARGS__)
#define CSOS_LOGLN(...) Serial.println(__VA_ARGS__)
#else
#include <cstddef>
#include <cstdint>

struct DisabledDiagnosticSerial {
    constexpr explicit operator bool() const { return false; }
    constexpr int availableForWrite() const { return 0; }
    constexpr std::size_t write(const std::uint8_t*, std::size_t) const { return 0; }
};

inline constexpr DisabledDiagnosticSerial CsosSerial;
// Discard the arguments at preprocessing time, including side effects.
#define CSOS_LOGF(...) ((void)0)
#define CSOS_LOGLN(...) ((void)0)
#endif
