"""Independent auth cases; never print credentials or response headers."""
import argparse
import base64
import hashlib
import hmac
import io
import json
from pathlib import Path
import ssl
import struct
import time

from PIL import Image
from websockets.exceptions import InvalidStatus
from websockets.sync.client import connect

p = argparse.ArgumentParser()
p.add_argument('--url', required=True)
p.add_argument('--secret', required=True)
p.add_argument('--output', required=True)
p.add_argument('--max-message', type=int, default=4096)
p.add_argument('--duration', type=int, default=0)
p.add_argument('--require-pan', action='store_true')
args = p.parse_args()
config = json.loads(Path(args.secret).read_text(encoding='utf-8-sig'))
out = Path(args.output)
out.mkdir(parents=True, exist_ok=True)
results = {'url': args.url, 'cases': {}}

for label in ('missing', 'wrong', 'unsupported_scheme', 'malformed_basic', 'correct'):
    headers = {}
    if label != 'missing':
        password = config['password'] if label == 'correct' else 'deliberately-wrong'
        value = base64.b64encode((config['username'] + ':' + password).encode()).decode()
        headers['Authorization'] = 'Basic ' + value
    if label == 'unsupported_scheme':
        headers['Authorization'] = 'Bearer deliberately-invalid'
    elif label == 'malformed_basic':
        headers['Authorization'] = 'Basic a'
    started = time.monotonic()
    expected = {'missing': 401, 'wrong': 401, 'unsupported_scheme': 403,
                'malformed_basic': 403, 'correct': 101}
    result = {'expected_status': expected[label]}
    try:
        with connect(args.url, additional_headers=headers, open_timeout=5,
                     close_timeout=1, proxy=None, subprotocols=['binary'],
                     compression=None, max_size=args.max_message) as ws:
            result['status'] = ws.response.status_code
            assert label == 'correct', 'unauthorized upgrade'
            if args.url.startswith('wss:'):
                assert isinstance(ws.socket, ssl.SSLSocket), 'TLS socket missing'
                assert ws.socket.context.verify_mode == ssl.CERT_REQUIRED
                assert ws.socket.context.check_hostname, 'hostname verification disabled'
                result['tls'] = {'version': ws.socket.version(),
                                 'certificate_required': True,
                                 'hostname_verification': True}
            pending = bytearray()

            def exact(n):
                while len(pending) < n:
                    chunk = ws.recv(timeout=5)
                    assert isinstance(chunk, bytes), 'nonbinary CXL'
                    result['max_message_bytes'] = max(result.get('max_message_bytes', 0), len(chunk))
                    assert len(chunk) <= args.max_message, 'message cap exceeded'
                    pending.extend(chunk)
                value = bytes(pending[:n])
                del pending[:n]
                return value

            hello = exact(21)
            assert hello[:5] == b'CXLS\x03', 'CXL greeting'
            ws.send(b'CXLR\x03' + struct.pack('<HHI', 320, 240, 16384)
                    + hmac.new(config['cxl_pin'].encode(), hello[5:], hashlib.sha256).digest())
            info = exact(6)
            assert info[:1] == b'O', 'CXL auth'
            exact(info[5])
            canvas = Image.new('RGB', (320, 240))
            tiles = 0
            frames = 0
            input_events = 0
            stream_started = time.monotonic()
            next_progress = stream_started + 60
            deadline = stream_started + max(15, args.duration + 15)
            while time.monotonic() < deadline:
                tag = exact(1)
                if tag == b'T':
                    x, y, w, h, n = struct.unpack('<HHHHI', exact(12))
                    assert n <= 16384 and x+w <= 320 and y+h <= 240
                    tile = Image.open(io.BytesIO(exact(n)))
                    tile.load()
                    assert tile.size == (w, h)
                    canvas.paste(tile, (x, y))
                    tiles += 1
                elif tag == b'F':
                    frame = exact(11)
                    result['pan_supported'] = bool(frame[10] & 16)
                    if args.require_pan:
                        assert result['pan_supported'], 'pan capability missing'
                    ws.send(b'A\x04' + frame[:4])
                    frames += 1
                    assert tiles > 0, 'empty frame'
                    result.update(cxl_handshake=True, jpeg_tiles=tiles, frame_acked=True,
                                  frames_acked=frames, input_events=input_events,
                                  stream_elapsed_seconds=round(time.monotonic()-stream_started, 3))
                    if args.duration and time.monotonic() >= next_progress:
                        print(json.dumps({'stream_progress': {
                            'seconds': result['stream_elapsed_seconds'], 'frames_acked': frames,
                            'input_events': input_events, 'max_message_bytes': result['max_message_bytes']}}), flush=True)
                        next_progress += 60
                    if args.duration and time.monotonic()-stream_started < args.duration:
                        ws.send(b'M\x04' + struct.pack('<hh', 8 if frames % 2 else -8, 0))
                        input_events += 1
                        continue
                    canvas.save(out / 'panel.png')
                    break
                else:
                    assert tag == b'H', 'unexpected CXL tag'
            else:
                raise TimeoutError('frame deadline')
    except InvalidStatus as exc:
        result['status'] = exc.response.status_code
        result['content_length'] = exc.response.headers.get('Content-Length')
        if result['status'] in (401, 403) and result['content_length'] != '0':
            result['framing_error'] = True
    except Exception as exc:
        result['error_type'] = type(exc).__name__
    result['elapsed_seconds'] = round(time.monotonic() - started, 3)
    result['passed'] = (result.get('status') == result['expected_status']
                        and 'error_type' not in result and not result.get('framing_error', False)
                        and (label != 'correct' or result.get('frame_acked', False)))
    results['cases'][label] = result
    print(json.dumps({label: result}), flush=True)
results['passed'] = all(item['passed'] for item in results['cases'].values())
(out / 'proof.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
raise SystemExit(0 if results['passed'] else 1)
