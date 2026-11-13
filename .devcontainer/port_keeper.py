"""Cloud port recovery with an explicit, persistent exposure interlock."""
from datetime import datetime, timezone
from contextlib import contextmanager
import fcntl
import http.client
import json
import os
from pathlib import Path
import socket
import subprocess
import time

BASE = Path('/workspaces/.csos-smoke')
CODE = Path(__file__).resolve().parent
PYTHON = str(BASE / 'autonomy-venv/bin/python')
CONTROL = BASE / 'exposure-control.json'


@contextmanager
def control_lock():
    with (BASE / 'exposure-control.lock').open('a') as stream:
        os.chmod(stream.name, 0o600)
        fcntl.flock(stream, fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(stream, fcntl.LOCK_UN)


def days_remaining(control):
    if control.get('token_has_no_expiry') is True and control.get('token_expires', 'missing') is None:
        return None
    try:
        expiry = datetime.fromisoformat(control.get('token_expires', '')).date()
        return (expiry-datetime.now(timezone.utc).date()).days
    except (TypeError, ValueError):
        return -1


def read_control():
    try:
        value = json.loads(CONTROL.read_text())
        if not isinstance(value, dict):
            return {'enabled': False, 'revision': 'invalid'}
        return value
    except (OSError, ValueError):
        return {'enabled': False, 'revision': 'missing-or-invalid'}


def credential_expired(control):
    remaining = days_remaining(control)
    return remaining is not None and remaining <= 0


def save_control(value):
    value['revision'] = str(time.time_ns())
    temp = CONTROL.with_suffix('.tmp')
    temp.write_text(json.dumps(value, indent=2))
    temp.chmod(0o600)
    temp.replace(CONTROL)
    return value


def target_name(control=None):
    name = (control or read_control()).get('codespace_name') or os.environ.get('CODESPACE_NAME')
    if not name:
        raise RuntimeError('Codespace identity missing; configure --codespace explicitly')
    return name


def gh(*args):
    credential = BASE / 'github-lifecycle.env'
    if credential.stat().st_mode & 0o077:
        raise RuntimeError('management credential permissions must be 0600')
    # Read each call: maintenance can replace the file without a rebuild.
    token = next(line.split('=', 1)[1].strip() for line in credential.read_text().splitlines()
                 if line.startswith('GH_TOKEN='))
    env = dict(os.environ, GH_TOKEN=token)
    run = subprocess.run(['gh', *args], env=env, capture_output=True, text=True, timeout=20)
    if run.returncode:
        raise RuntimeError('gh exit ' + str(run.returncode))
    return run.stdout


def supervisor(*args):
    return subprocess.run(['/usr/bin/supervisorctl', '-c', str(CODE / 'supervisord.conf'), *args],
                          capture_output=True, text=True, timeout=15)


def close_exposure(name):
    # Closing the backend also terminates sessions if GitHub cannot be reached.
    run = supervisor('stop', 'bridge')
    if run.returncode and 'not running' not in run.stdout.lower():
        raise RuntimeError('cannot stop bridge')
    try:
        gh('codespace', 'ports', 'visibility', '8089:private', '-c', name)
        return True
    except Exception:
        return False


def dependencies():
    output = supervisor('status').stdout
    states = {parts[0]: parts[1:] for line in output.splitlines() if len(parts := line.split()) >= 2}
    names = ('xvfb', 'xfce', 'cxl')
    if not all(states.get(name, [''])[0] == 'RUNNING' for name in names):
        raise RuntimeError('desktop service not running')
    subprocess.run(['xdpyinfo', '-display', ':77'], stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL, timeout=3, check=True)
    with socket.create_connection(('127.0.0.1', 5901), timeout=2) as stream:
        stream.settimeout(2)
        hello = bytearray()
        while len(hello) < 5:
            chunk = stream.recv(5-len(hello))
            if not chunk:
                raise RuntimeError('CXL greeting closed')
            hello.extend(chunk)
        if hello != b'CXLS\x03':
            raise RuntimeError('CXL greeting invalid')
    return tuple(states[name][2].rstrip(',') for name in names)


def auth_healthy():
    connection = http.client.HTTPConnection('127.0.0.1', 8089, timeout=3)
    try:
        connection.request('GET', '/', headers={'Upgrade': 'websocket', 'Connection': 'Upgrade',
            'Sec-WebSocket-Key': 'Y3Nvcy1wcm9iZS0xNmJ5dGU=', 'Sec-WebSocket-Version': '13'})
        response = connection.getresponse()
        return response.status == 401 and response.getheader('Content-Length') == '0'
    except (OSError, http.client.HTTPException):
        return False
    finally:
        connection.close()


def main():
    name = target_name()
    generation = str(time.time_ns())
    forwarder = None
    registration = 'declarative-existing'
    ready = False
    previous_dependencies = None
    previous_bridge = None
    last_frame_check = 0
    frame_checks = 0

    def state(phase, control, **extra):
        value = {'phase': phase, 'generation': generation, 'registration': registration,
                 'observed_at_utc': datetime.now(timezone.utc).isoformat(),
                 'pid': os.getpid(), 'control_revision': control.get('revision'),
                 'token_expires': control.get('token_expires'), **extra}
        target = BASE / 'autonomy-status.json'
        temp = target.with_suffix('.tmp')
        temp.write_text(json.dumps(value, indent=2))
        temp.replace(target)

    try:
        while True:
            control = read_control()
            days_left = days_remaining(control)
            try:
                if control.get('enabled') is not True or credential_expired(control):
                    private = close_exposure(name)
                    ready = False
                    state('paused' if control.get('enabled') is not True else 'credential-expired',
                          control, private_confirmed=private, days_left=days_left)
                    time.sleep(5)
                    continue
                current_dependencies = dependencies()
                if current_dependencies != previous_dependencies:
                    ready = False
                if not auth_healthy():
                    with control_lock():
                        current = read_control()
                        if current.get('enabled') is not True or credential_expired(current):
                            raise RuntimeError('exposure not enabled')
                        run = supervisor('start', 'bridge')
                        if run.returncode and 'already started' not in run.stdout.lower():
                            raise RuntimeError('bridge not running')
                    ready = False
                    time.sleep(1)
                    if not auth_healthy():
                        raise RuntimeError('bridge auth not ready')
                bridge_pid = supervisor('pid', 'bridge').stdout.strip()
                if bridge_pid != previous_bridge:
                    ready = False
                if not ready or time.monotonic()-last_frame_check >= 60:
                    with (BASE / 'autonomy-logs/preflight.log').open('w') as output:
                        probe = subprocess.run([PYTHON, str(CODE / 'tunnel_probe.py'),
                            '--url', 'ws://127.0.0.1:8089/', '--secret', str(BASE / 'bridge-private.json'),
                            '--output', str(BASE / 'autonomy-preflight'), '--max-message', '4096'],
                            stdout=output, stderr=subprocess.STDOUT, timeout=45)
                    if probe.returncode:
                        raise RuntimeError('local auth/frame preflight failed')
                    last_frame_check = time.monotonic()
                    frame_checks += 1
                ports = json.loads(gh('codespace', 'ports', '-c', name, '--json', 'sourcePort,visibility'))
                port = next((p for p in ports if p['sourcePort'] == 8089), None)
                if forwarder is not None and forwarder.poll() is not None:
                    forwarder = None
                if port is None or (registration == 'cloud-cli' and forwarder is None):
                    registration = 'cloud-cli'
                    if forwarder is None:
                        env = dict(os.environ)
                        credential = BASE / 'github-lifecycle.env'
                        env['GH_TOKEN'] = next(line.split('=', 1)[1].strip()
                            for line in credential.read_text().splitlines() if line.startswith('GH_TOKEN='))
                        with (BASE / 'autonomy-logs/cloud-forwarder.log').open('a') as log:
                            forwarder = subprocess.Popen(['gh', 'codespace', 'ports', 'forward',
                                '8089:18092', '-c', name], env=env, stdout=log, stderr=subprocess.STDOUT)
                    time.sleep(2)
                    ports = json.loads(gh('codespace', 'ports', '-c', name, '--json', 'sourcePort,visibility'))
                    port = next((p for p in ports if p['sourcePort'] == 8089), None)
                if port is None:
                    raise RuntimeError('port not registered')
                # Pause and publication are serialized, including the network call.
                with control_lock():
                    current = read_control()
                    if (current.get('enabled') is not True or credential_expired(current)
                            or current.get('revision') != control.get('revision')):
                        raise RuntimeError('exposure control changed')
                    if port['visibility'] != 'public':
                        gh('codespace', 'ports', 'visibility', '8089:public', '-c', name)
                    ready = True
                    previous_dependencies = current_dependencies
                    previous_bridge = bridge_pid
                    state('ready', control, forwarder_pid=forwarder.pid if forwarder else None,
                          days_left=days_left, renewal_required=days_left is not None and days_left <= 7,
                          bridge_pid=bridge_pid, frame_checks=frame_checks,
                          frame_check_age_seconds=round(time.monotonic()-last_frame_check, 1))
            except Exception as exc:
                private = close_exposure(name)
                ready = False
                state('retry', control, error_type=type(exc).__name__, private_confirmed=private)
            time.sleep(10)
    finally:
        if forwarder is not None:
            forwarder.terminate()


if __name__ == '__main__':
    main()
