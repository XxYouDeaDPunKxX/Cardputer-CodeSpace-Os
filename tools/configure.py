"""Generate ignored device headers from a local JSON file; never print values."""
import argparse
import hashlib
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--config', required=True, type=Path)
args = p.parse_args()
data = json.loads(args.config.read_text(encoding='utf-8-sig'))
limits = {'wifi_ssid':32, 'wifi_password':64, 'wss_host':128, 'bridge_user':64,
          'bridge_password':128, 'cxl_pin':64, 'github_token':255, 'codespace_name':80}
for key, limit in limits.items():
    value = data.get(key, '')
    if not isinstance(value, str) or len(value.encode()) > limit or '\0' in value:
        raise SystemExit('Invalid local field: '+key)
name = data.get('codespace_name', '')
if name and re.fullmatch(r'[A-Za-z0-9-]+', name) is None:
    raise SystemExit('Invalid local field: codespace_name')
if name and data.get('wss_host', '').lower() != f'{name.lower()}-8089.app.github.dev':
    raise SystemExit('Codespace name and WSS host do not match.')
fields = {'CSOS_WIFI_SSID':'wifi_ssid', 'CSOS_WIFI_PASSWORD':'wifi_password',
          'CSOS_WSS_HOST':'wss_host', 'CSOS_BRIDGE_USER':'bridge_user',
          'CSOS_BRIDGE_PASSWORD':'bridge_password', 'CSOS_CXL_PIN':'cxl_pin'}
def header(path, values):
    path.write_text('#pragma once\n'+''.join('#define '+k+' '+json.dumps(v)+'\n' for k,v in values.items()))
header(root/'csos-client/CloudConfig_private.h', {k:data.get(v,'') for k,v in fields.items()})
token = data.get('github_token','')
header(root/'csos-client/ManagementConfig_private.h', {
    'CSOS_GH_BOOTSTRAP_TOKEN':token,
    'CSOS_GH_BOOTSTRAP_ID':hashlib.sha256(token.encode()).hexdigest() if token else '',
    'CSOS_CODESPACE_NAME':data.get('codespace_name','')})
print('Local headers generated; values suppressed. They and the binary must stay untracked.')
