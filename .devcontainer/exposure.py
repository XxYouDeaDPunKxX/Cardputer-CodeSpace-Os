"""Operator switch: pause persists over restarts and closes active sessions."""
import argparse
from datetime import date
import json
import os
import time
from port_keeper import BASE, close_exposure, control_lock, read_control, save_control, target_name

p = argparse.ArgumentParser()
p.add_argument('action', choices=['pause', 'resume', 'status'])
p.add_argument('--token-expires')
p.add_argument('--token-no-expiry',action='store_true')
p.add_argument('--codespace')
args = p.parse_args()
control = read_control()
if args.action == 'status':
    print(json.dumps(control))
    raise SystemExit(0)
if args.token_expires:
    if args.token_no_expiry:
        p.error('choose token-expires or token-no-expiry')
    date.fromisoformat(args.token_expires)
    control['token_expires'] = args.token_expires
    control['token_has_no_expiry'] = False
if args.token_no_expiry:
    control['token_expires'] = None
    control['token_has_no_expiry'] = True
if args.codespace:
    control['codespace_name'] = args.codespace
name = target_name(control)
control['enabled'] = args.action == 'resume'
with control_lock():
    control = save_control(control)
    private = close_exposure(name) if args.action == 'pause' else None
if args.action == 'pause':
    deadline = time.monotonic()+35
    while time.monotonic() < deadline:
        try:
            status = json.loads((BASE / 'autonomy-status.json').read_text())
            if status['phase'] == 'paused' and status['control_revision'] == control['revision']:
                print(json.dumps({'paused': True, 'private_confirmed': private,
                                  'persists_after_restart': True}))
                raise SystemExit(0 if private else 1)
        except (OSError, ValueError, KeyError):
            pass
        time.sleep(.25)
    raise SystemExit('Backend stopped and pause saved; keeper acknowledgement missing')
print(json.dumps({'enabled': True, 'token_expires': control.get('token_expires')}))
