"""Authenticated loopback bridge with bounded TCP-to-WebSocket messages."""
import json
import multiprocessing
from pathlib import Path
import sys

if __name__ == '__main__':
    multiprocessing.set_start_method('fork', force=True)
    config = json.loads(Path('/workspaces/.csos-smoke/bridge-private.json').read_text())
    from websockify.websocketproxy import ProxyRequestHandler, websockify_init
    ProxyRequestHandler.buffer_size = 4096
    sys.argv = ['websockify', '--auth-plugin=csos_http_auth.FramedBasicHTTPAuth',
                '--auth-source=' + config['username'] + ':' + config['password'],
                '127.0.0.1:8089', '127.0.0.1:5901']
    websockify_init()
