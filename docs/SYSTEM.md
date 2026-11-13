# Reference system configuration

The client is derived from the November 13 recovery-off build, with USB console disabled in the normal build. It uses the Cardputer ADV, V2 wiring, 8 MiB flash, the project partition table, and disabled PSRAM. Setup installs Arduino ESP32 3.2.1, M5Cardputer 1.1.1, M5GFX 0.2.15 with the included display patch, M5Unified 0.2.10, and JPEGDEC 1.8.5.

| Component | Configuration |
| --- | --- |
| Transport | CXL 1.4.1, protocol 3 over authenticated WSS; 320 × 240 client with interactive JPEG payloads up to 8 KiB |
| Server | Normal launcher in `server/wss_service.py` and core server in `server/cxl_remote_server.py`. The original core digest is recorded in `THIRD_PARTY.md`. |
| Runtime modules | Touch, presence, refinement, lossless repair, and audio. Bootstrap copies modules from the checkout. Startup verifies that the runtime copies match the sources. |
| Video | Initial server limit of 5 FPS, adjustable from the client. Lossless repair enabled. Adaptive rectangles and regional video require explicit activation. |
| Audio | CXL v5 duplex at 12 kHz; virtual PulseAudio sinks and sources at 16 kHz. |
| Desktop | Xvfb 640 × 480 × 24, XFCE profile in `desktop/profile.json`, CSOS-Greybird GTK theme, and top panel. |
| Diagnostics | USB CDC console, CXL traces, and FPS probe disabled in the normal build. The normal server starts without a capture marker. |

Setup steps and a fresh-install verification checklist are in [Setup and build](SETUP.md).
