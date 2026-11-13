# Versions and development milestones

Use the [setup guide](SETUP.md) to install the current version. The measurements below apply to the stated test conditions.

| Milestone | Change and observation | Relevant configuration |
| --- | --- | --- |
| **November 5 · V2 TFT and DMA** | The renderer alternates two half-buffers: while one is transferred, the decoder prepares the other. In a local test with the same scene, mean time fell from 54.05 to 33.05 ms. The firmware was flashed and tested on the Cardputer. This measurement concerns the renderer. | V2 wiring, 40 MHz TFT SPI, ESP32 3.3.2. The server inventory is dated the same day. |
| **November 6 · FPS reference** | Approximately 15.9 FPS on the TFT in an animated scene, across four windows of about 20 seconds each. MAX and a limit of 30 gave similar results in that scene. | V2 wiring, ESP32 3.3.2, remote audio disabled. Both server modules used in the test were recovered. The XFCE profile active during measurement has not been established. |
| **November 8 · JPEG8 and sharpness recovery** | Candidate12 suffered audio/video lag. Raising the client JPEG limit from 4 to 8 KiB improved video according to the hands-on test. A later server correction restored sharpness after moving windows, with the same firmware. Audio remained intermittent. | V2, ESP32 3.2.1, CXL v5 audio at 12 kHz. The corrected server retains 8 KiB for interactive JPEG and applies a separate 4 KiB-per-cycle refinement budget. |
| **November 13 · Session recovery** | The firmware starts the receive task before audio and waits for the first compatible frame. Flashing and the menu were verified. This is the base of the current client. | V2, ESP32 3.2.1, CXL traces disabled. The [system reference](SYSTEM.md) describes the current client, server, and desktop configuration. |

## Historical source packages

Both packages require [V2 wiring](CABLAGGIO-V2.md). Download the ZIP for the milestone you want to build.

- **[November 6 FPS test](../history/CS-OS-fps-06nov-sorgenti.zip)**. Client from the test and a CXL/WSS pair recovered by hash. The FPS probe can be enabled on demand. The included desktop also applies the November 7 XFWM fixes, which came *after* the measurement.
- **[November 8 JPEG8 with refinement](../history/CS-OS-jpeg8-refinement-08nov-sorgenti.zip)**. JPEG8 client and correction modules verified against the deployment. The package uses the normal launcher and a reconstructed desktop profile.

Each package includes its own guide, Codespaces configuration, and build dependencies. Package-specific notes distinguish components matching the original test from reconstructed components.
