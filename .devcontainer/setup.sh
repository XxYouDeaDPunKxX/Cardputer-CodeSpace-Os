#!/bin/bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
state=/workspaces/.csos-smoke
sudo apt-get update -qq
sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends supervisor xfce4 xfce4-terminal xfce4-pulseaudio-plugin xvfb xauth dbus-x11 pulseaudio pulseaudio-utils gnome-sound-recorder vlc x11-utils mousepad falkon fuse libfuse2t64 greybird-gtk-theme gtk2-engines-murrine
mkdir -p "$state/autonomy-logs"
chmod 700 "$state"
python3 -m venv "$state/autonomy-venv"
"$state/autonomy-venv/bin/python" -m pip install --disable-pip-version-check -r "$repo/server/requirements.lock"
bash "$repo/.devcontainer/sync-server.sh" --update
python3 "$repo/tools/provision-host.py"
