# Main changes

For the sequence of experiments and their matching configurations, see [Versions and development milestones](docs/VERSIONI.md).

- **Session recovery.** The current build creates its receive task before starting audio and waits for the first compatible frame. Flashing and menus were checked on the device. Session stability still needs measurement.
- **Video quality.** The client negotiates interactive JPEG payloads up to 8 KiB. The server uses a separate refinement budget. In a TFT trial, this allowed image sharpness to recover after moving windows. Audio dropouts remain.
- **Performance reference.** An earlier build reached about 15.9 FPS in an animated scene with remote audio disabled. The figure describes that particular test and provides a reference for video comparisons under matching conditions.
