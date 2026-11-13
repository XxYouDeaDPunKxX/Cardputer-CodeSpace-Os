#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
state=/workspaces/.csos-smoke
mode=${1:---check}
files=(
    server/wss_service.py server/cxl_remote_server.py
    csos-client/server/csos_touch.py csos-client/server/csos_presence.py
    csos-client/server/csos_quality.py csos-client/server/csos_video.py
    csos-client/server/csos_audio.py csos-client/server/presence_rpc.cjs
)

case "$mode" in
    --check) ;;
    --update)
        if supervisorctl -c "$repo/.devcontainer/supervisord.conf" pid >/dev/null 2>&1; then
            echo "Stop Supervisor before updating the runtime." >&2
            exit 1
        fi
        for file in "${files[@]}"; do
            install -m 600 "$repo/$file" "$state/${file##*/}"
        done
        # Avoid loading bytecode from a previous revision with matching size and timestamp.
        for file in "${files[@]}"; do
            if [[ "$file" == *.py ]]; then
                name=${file##*/}
                rm -f -- "$state/__pycache__/${name%.py}".*.pyc
            fi
        done
        ;;
    *) echo "Usage: $0 [--check|--update]" >&2; exit 2 ;;
esac

drift=0
for file in "${files[@]}"; do
    if ! cmp -s "$repo/$file" "$state/${file##*/}"; then
        printf 'Runtime differs from checkout: %s\n' "$file" >&2
        drift=1
    fi
done
if (( drift )); then
    echo "Stop Supervisor, run bash .devcontainer/sync-server.sh --update, then run bash .devcontainer/start.sh." >&2
    exit 1
fi
