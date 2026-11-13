"""Reapply pinned narrow backend fixes to the pinned project-local M5GFX 0.2.15.

Default: check only. --apply requires explicit invocation, is idempotent and
refuses any unrecognized upstream code or different M5GFX version.
No changes to global Arduino packages, installed firmware, or other projects.
"""
from pathlib import Path
import argparse
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LIBRARY = ROOT / ".tools/arduino/user/libraries/M5GFX"

BUS_OLD = """    if (_cfg.use_lock) spi::endTransaction(_cfg.spi_host);
#if defined (ARDUINO) // Arduino ESP32
    *_spi_user_reg = SPI_USR_MOSI | SPI_USR_MISO | SPI_DOUTDIN; // for other SPI device (e.g. SD card)
#endif"""
BUS_NEW = """#if defined (ARDUINO) // Arduino ESP32
    // Restore the shared peripheral registers while still owning its mutex.
    *_spi_user_reg = SPI_USR_MOSI | SPI_USR_MISO | SPI_DOUTDIN; // for other SPI device (e.g. SD card)
#endif
    if (_cfg.use_lock) spi::endTransaction(_cfg.spi_host);"""
PANEL_OLD = """    _bus->init();
    rst_control(true);"""
PANEL_NEW = """    if (!_bus->init()) return false;
    rst_control(true);"""

# The Arduino SPI HAL and IDF device registration must both be usable. IDF
# bus_initialize may return ALREADY_INITIALIZED, which is not alone fatal.
COMMON_SPI_OLD = """        SPI.end();
        SPI.begin(spi_sclk, spi_miso, spi_mosi);
        _spi_handle[spi_host] = SPI.bus();"""
COMMON_SPI_NEW = """        SPI.end();
        if (!SPI.begin(spi_sclk, spi_miso, spi_mosi)) {
          return cpp::fail(error_t::connection_lost);
        }
        _spi_handle[spi_host] = SPI.bus();"""
COMMON_HANDLE_OLD = """        _spi_handle[spi_host] = spiStartBus(spi_num, SPI_CLK_EQU_SYSCLK, 0, 0);
      }

#endif"""
COMMON_HANDLE_NEW = """        _spi_handle[spi_host] = spiStartBus(spi_num, SPI_CLK_EQU_SYSCLK, 0, 0);
      }
      if (_spi_handle[spi_host] == nullptr) {
        return cpp::fail(error_t::connection_lost);
      }

#endif"""
COMMON_DEVICE_OLD = """        if (ESP_OK != spi_bus_add_device(static_cast<spi_host_device_t>(spi_host), &devcfg, &_spi_dev_handle[spi_host])) {
          ESP_LOGW("LGFX", "Failed to spi_bus_add_device. ");
        }"""
COMMON_DEVICE_NEW = """        if (ESP_OK != spi_bus_add_device(static_cast<spi_host_device_t>(spi_host), &devcfg, &_spi_dev_handle[spi_host])
            || _spi_dev_handle[spi_host] == nullptr) {
          ESP_LOGW("LGFX", "Failed to spi_bus_add_device. ");
          return cpp::fail(error_t::connection_lost);
        }"""

PATCHES = {
    "src/lgfx/v1/platforms/esp32/Bus_SPI.cpp": [(BUS_OLD, BUS_NEW)],
    "src/lgfx/v1/panel/Panel_Device.cpp": [(PANEL_OLD, PANEL_NEW)],
    "src/lgfx/v1/platforms/esp32/common.cpp": [
        (COMMON_SPI_OLD, COMMON_SPI_NEW),
        (COMMON_HANDLE_OLD, COMMON_HANDLE_NEW),
        (COMMON_DEVICE_OLD, COMMON_DEVICE_NEW),
    ],
}

def transform(source: str, old: str, new: str) -> tuple[str, bool]:
    eol = "\r\n" if "\r\n" in source else "\n"
    old, new = old.replace("\n", eol), new.replace("\n", eol)
    if source.count(new) == 1 and source.count(old) == 0:
        return source, False
    if source.count(old) == 1 and source.count(new) == 0:
        return source.replace(old, new, 1), True
    raise ValueError("unsupported upstream source or ambiguous patch location")

def run(root: Path, apply: bool) -> int:
    properties = (root / "library.properties").read_text(encoding="utf-8")
    if not re.search(r"(?m)^version=0\.2\.15\s*$", properties):
        raise ValueError("M5GFX version is not pinned to 0.2.15")
    changes = []
    for name, replacements in PATCHES.items():
        path = root / name
        edited = path.read_bytes().decode("utf-8")
        patch_count = 0
        for old, new in replacements:
            edited, changed = transform(edited, old, new)
            patch_count += int(changed)
        changes.append((path, edited, patch_count))
    if not apply and any(count for _, _, count in changes):
        raise ValueError("backend not patched; run with --apply to apply pinned fixes")
    if apply:
        for path, edited, count in changes:
            if count:
                path.write_bytes(edited.encode("utf-8"))
    print("M5GFX 0.2.15 display backend:",
          "PATCHED" if apply else "CHECK OK",
          "(" + str(sum(count for _, _, count in changes)) + " patch(es) applied)")
    return 0

def self_test() -> None:
    for replacements in PATCHES.values():
        for old, new in replacements:
            patched, changed = transform("prefix\n" + old + "\nsuffix", old, new)
            assert changed and new in patched
            unchanged, changed_again = transform(patched, old, new)
            assert not changed_again and unchanged == patched
            try:
                transform("unsupported version", old, new)
            except ValueError:
                pass
            else:
                raise AssertionError("unexpected upstream accepted")
    print("Backend patch transform self-test PASS")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--library-root", type=Path, default=DEFAULT_LIBRARY)
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        if args.self_test:
            self_test()
        else:
            sys.exit(run(args.library_root, args.apply))
    except (OSError, UnicodeError, ValueError) as exc:
        print(f"display backend patch: {exc}", file=sys.stderr)
        sys.exit(2)
