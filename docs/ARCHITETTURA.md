# Architecture

The Cardputer ADV handles the keyboard, touch input, and displays. Its built-in screen shows controls and session status. The external 320 × 240 TFT shows the remote Linux desktop.

## From Cardputer to desktop

1. The client opens a WSS connection through Codespace port 8089. The bridge authenticates the client and forwards the session to the CXL server on local port 5901. CXL verifies the PIN through an HMAC handshake.
2. The server captures the XFCE desktop running on Xvfb at 640 × 480 and sends JPEG updates. The server can also send lossless updates if the client supports them. The client decodes the updates and draws them on the TFT.
3. Server modules translate keyboard and touch actions into X11 input. Touch uses coordinates belonging to the displayed frame. Panning changes the desktop viewport.
4. CXL v5 duplex audio runs at 12 kHz. On Linux, PulseAudio provides virtual devices for system playback and the remote microphone.

## Startup and state

The devcontainer prepares dependencies, server components, and host configuration. Supervisor manages Xvfb, XFCE, CXL, the bridge, the port controller, and the presence service. The XFCE profile applies the CSOS-Greybird theme and TFT-oriented panel and window settings. At startup, CS OS compares the server modules in the checkout with the copies it runs.

The GitHub token controls the Codespace lifecycle. The bridge and CXL use separate credentials. The [setup guide](SETUP.md) explains how to configure them. The [system reference](SYSTEM.md) lists versions, settings, and toolchain.
