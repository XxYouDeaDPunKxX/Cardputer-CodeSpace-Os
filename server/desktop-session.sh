#!/bin/bash
set -euo pipefail
# Initialize inside the display's DBus session, before XFCE starts.
bash "$(dirname -- "${BASH_SOURCE[0]}")/../desktop/apply-profile.sh"
preferences="${XDG_CONFIG_HOME:-$HOME/.config}/csos/gtk-overlay-scrolling"
if [[ -z "${GTK_OVERLAY_SCROLLING+x}" && -f "$preferences" ]]; then
    read -r GTK_OVERLAY_SCROLLING < "$preferences"
    export GTK_OVERLAY_SCROLLING
fi
exec /usr/bin/startxfce4
