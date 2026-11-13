#!/bin/bash
set -euo pipefail
# User daemon, local Unix socket only. No network listener or physical sound card.
pulseaudio --start --exit-idle-time=-1 --log-target=file:/tmp/csos-pulse.log
if ! pactl list short sinks | grep -q $'\tcsos_output\t'; then
 pactl load-module module-null-sink norewinds=1 sink_name=csos_output rate=16000 channels=1 sink_properties=device.description=CS_OS_Speaker
fi
if ! pactl list short sinks | grep -q $'\tcsos_mic_feed\t'; then
 pactl load-module module-null-sink norewinds=1 sink_name=csos_mic_feed rate=16000 channels=1 sink_properties=device.description=CS_OS_Mic_Feed
fi
if ! pactl list short sources | grep -q $'\tcsos_microphone\t'; then
 pactl load-module module-remap-source master=csos_mic_feed.monitor source_name=csos_microphone source_properties=device.description=CS_OS_Microfono
fi
marker="$HOME/.config/csos-audio-defaults-set"
if [[ ! -f "$marker" ]]; then
 pactl set-default-sink csos_output
 pactl set-default-source csos_microphone
 mkdir -p "$(dirname "$marker")";touch "$marker"
fi
