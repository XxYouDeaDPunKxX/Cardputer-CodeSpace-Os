"""Create runtime credentials from Codespaces secrets, without displaying them."""
from pathlib import Path
from datetime import date
import json
import os

state = Path('/workspaces/.csos-smoke')
state.mkdir(mode=0o700, parents=True, exist_ok=True)
os.chmod(state,0o700)
def write_private(name, content):
    target=state/name
    if target.exists():
        return  # Existing operator configuration wins over provisioning.
    fd=os.open(target,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    with os.fdopen(fd,'w') as out:
        out.write(content)

keys=('CSOS_BRIDGE_USER','CSOS_BRIDGE_PASSWORD','CSOS_CXL_PIN','CSOS_GH_TOKEN')
if not all(os.environ.get(key) for key in keys):
    print('Runtime installed. Configure the four CSOS credentials before starting the host.')
    raise SystemExit(0)
expiry=os.environ.get('CSOS_GH_TOKEN_EXPIRES','')
no_expiry=os.environ.get('CSOS_GH_TOKEN_NO_EXPIRY')=='1'
if bool(expiry)==no_expiry:
    raise SystemExit('Set either CSOS_GH_TOKEN_EXPIRES=YYYY-MM-DD or CSOS_GH_TOKEN_NO_EXPIRY=1.')
if expiry:
    date.fromisoformat(expiry)
write_private('bridge-private.json',json.dumps({
    'username':os.environ['CSOS_BRIDGE_USER'], 'password':os.environ['CSOS_BRIDGE_PASSWORD'],
    'cxl_pin':os.environ['CSOS_CXL_PIN']})+'\n')
write_private('github-lifecycle.env','GH_TOKEN='+os.environ['CSOS_GH_TOKEN']+'\n')
write_private('exposure-control.json',json.dumps({
    'enabled':os.environ.get('CSOS_EXPOSE_PUBLIC')=='1', 'revision':'initial-provision',
    'codespace_name':os.environ.get('CODESPACE_NAME',''),
    'token_expires':expiry or None, 'token_has_no_expiry':no_expiry})+'\n')
print('Host configuration ready; secret values suppressed. Existing files preserved.')
