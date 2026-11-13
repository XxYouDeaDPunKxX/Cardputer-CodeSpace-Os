#!/bin/bash
set -euo pipefail
export CSOS_REPO
CSOS_REPO=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
state=/workspaces/.csos-smoke
if [[ ! -f "$state/bridge-private.json" || ! -f "$state/github-lifecycle.env" ]]; then
    echo 'Host not provisioned. See docs/SETUP.md; credentials were not created.'
    exit 0
fi
bash "$CSOS_REPO/.devcontainer/sync-server.sh" --check
mkdir -p "$state/autonomy-logs"
bash "$CSOS_REPO/server/audio-setup.sh"
config="$CSOS_REPO/.devcontainer/supervisord.conf"
if supervisorctl -c "$config" pid >/dev/null 2>&1; then exit 0; fi
auth_tmp=$(mktemp "$state/Xauthority.XXXXXX")
chmod 600 "$auth_tmp"
xauth -f "$auth_tmp" add :77 . "$(openssl rand -hex 16)"
mv "$auth_tmp" "$state/Xauthority"
supervisord -c "$config"
