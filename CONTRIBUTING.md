# Contributing

CS OS is maintained by one person. If you want to work on the firmware, server, or Codespaces setup, these are the areas that need the most attention.

- **Audio continuity.** Playback still drops out during remote desktop sessions. Start with the client audio queues in `csos-client/DuplexAudio.cpp` or the PulseAudio path in `csos-client/server/csos_audio.py`.
- **Video under load.** The picture sharpens after window movement, but the stream slows down when the desktop is busy. The [version notes](docs/VERSIONI.md) describe the renderer and server changes already tried. The earlier FPS measurement used a specific animated scene with audio off.
- **Codespaces setup.** The current setup recreates the desktop profile used in development. Improvements to provisioning and profile setup on a fresh Codespace are welcome.

The [setup guide](docs/SETUP.md) explains how to build and run CS OS. If you share logs while working on a change, remove credentials, private addresses, and environment identifiers.

Run the video-quality tests from the repository root:

```sh
python -m pip install -r requirements-dev.txt
python -m unittest discover -s csos-client/tests -p "test*quality*.py"
```
