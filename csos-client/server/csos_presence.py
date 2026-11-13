"""Presence from authenticated Cardputer P frames; never renews itself."""
import json
import math
import os
from pathlib import Path
import secrets
import threading
import time

_file_lock = threading.Lock()


def _process_start():
    try:
        return Path('/proc/self/stat').read_text().rsplit(')', 1)[1].split()[19]
    except (OSError, IndexError):
        return None


def with_presence(Session, lease_path='/workspaces/.csos-smoke/presence-lease.json', clock=time.time):
    path = Path(lease_path)

    class PresenceSession(Session):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, **kwargs)
            self._presence_owner = secrets.token_hex(16)
            self._presence_authenticated = False

        def handshake(self, monitors):
            self._presence_authenticated = False
            requires_hmac = bool(self.server.cfg.get('require_pin') and self.server.cfg.get('pin'))
            result = super().handshake(monitors)
            self._presence_authenticated = requires_hmac
            return result

        def handle(self, t, p):
            with self.lock:
                if (t == ord('P') and p == b'' and self.alive
                        and getattr(self, 'ready', False) and self._presence_authenticated):
                    now = clock()
                    if math.isfinite(now) and now >= 0:
                        record = {'at': now, 'pid': os.getpid(), 'owner': self._presence_owner,
                                  'source_start': _process_start()}
                        # All sessions in the service share this lock; replace is
                        # atomic for the independent read-only Node worker.
                        with _file_lock:
                            temporary = path.with_name(path.name + '.' + self._presence_owner + '.tmp')
                            try:
                                with temporary.open('w', encoding='utf-8') as stream:
                                    json.dump(record, stream, separators=(',', ':'))
                                os.chmod(temporary, 0o600)
                                os.replace(temporary, path)
                            except OSError:
                                try: temporary.unlink(missing_ok=True)
                                except OSError: pass
                return super().handle(t, p)

        def close(self, reason=None):
            with self.lock:
                self._presence_authenticated = False
                with _file_lock:
                    try:
                        record = json.loads(path.read_text(encoding='utf-8'))
                        if record.get('owner') == self._presence_owner and record.get('pid') == os.getpid():
                            path.unlink(missing_ok=True)
                    except (OSError, ValueError, AttributeError):
                        pass
                return super().close(reason)

    return PresenceSession
