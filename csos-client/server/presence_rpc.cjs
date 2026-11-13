'use strict';
// Local internal Codespaces RPC; no listener and no external dependencies.
const fs = require('node:fs');
const http2 = require('node:http2');
const {performance} = require('node:perf_hooks');

function readJSON(file) {
  const data = fs.readFileSync(file);
  if (data.length > 4096) throw new Error('oversized state');
  return JSON.parse(data.toString('utf8'));
}
function processStart(pid) {
  const data = fs.readFileSync(`/proc/${pid}/stat`, 'utf8');
  return data.slice(data.lastIndexOf(')') + 1).trim().split(/\s+/)[19];
}
function processAlive(pid) { try { process.kill(pid, 0); return true; } catch { return false; } }
function controlEnabled(file, now=Date.now(), read=readJSON) {
  try {
    const v=read(file), day=new Date(now).toISOString().slice(0,10);
    if(v.enabled === true && v.token_has_no_expiry === true && v.token_expires === null)return true;
    return v.enabled === true && typeof v.token_expires === 'string'
      && /^\d{4}-\d{2}-\d{2}$/.test(v.token_expires)
      && new Date(v.token_expires).toISOString().slice(0,10) === v.token_expires && v.token_expires > day;
  } catch { return false; }
}
function leaseActive(file, now = Date.now() / 1000, deps = {}) {
  try {
    const v = (deps.read || readJSON)(file);
    if (!Number.isFinite(now) || !Number.isFinite(v.at) || v.at < 0 || now-v.at < 0 || now-v.at >= 30
      || !Number.isSafeInteger(v.pid) || v.pid <= 0 || typeof v.owner !== 'string' || !v.owner
      || typeof v.source_start !== 'string' || !/^\d+$/.test(v.source_start)) return false;
    return (deps.alive || processAlive)(v.pid) && (deps.start || processStart)(v.pid) === v.source_start;
  } catch { return false; }
}
function rpcOnce({connect = () => http2.connect('http://127.0.0.1:16634'), timeoutMs = 3000} = {}) {
  return new Promise(resolve => {
    let client, request, done = false, status, httpStatus, chunks = [], size = 0;
    const finish = result => {
      if (done) return;
      done = true; clearTimeout(timer);
      if (request) request.destroy();
      if (client) client.destroy();
      resolve(result);
    };
    const timer = setTimeout(() => finish(false), timeoutMs);
    try {
      client = connect(); client.on('error', () => finish(false));
      request = client.request({':method':'POST',
        ':path':'/Codespaces.Grpc.CodespaceHostService.v1.CodespaceHost/NotifyCodespaceOfClientActivity',
        'content-type':'application/grpc',te:'trailers',authorization:'Bearer token','grpc-timeout':'3S'});
      request.on('error', () => finish(false));
      request.on('response', h => {httpStatus=h[':status']; status=h['grpc-status'];});
      request.on('trailers', h => {status=h['grpc-status'];});
      request.on('data', b => {size+=b.length; if(size>4096) finish(false); else chunks.push(b);});
      request.on('end', () => {
        const b=Buffer.concat(chunks);
        finish(httpStatus === 200 && status === '0' && b.length === 7 && b[0] === 0
          && b.readUInt32BE(1) === 2 && b[5] === 8 && b[6] === 1);
      });
      const payload=Buffer.from([10,2,103,104,18,9,...Buffer.from('keepAlive')]);
      const frame=Buffer.alloc(payload.length+5); frame.writeUInt32BE(payload.length,1); payload.copy(frame,5);
      request.end(frame);
    } catch { finish(false); }
  });
}
function createWorker({active, rpc=rpcOnce, log=console.log}) {
  let last=-Infinity, busy=false, previous;
  return {async tick(now=performance.now()/1000) {
    if(busy) return;
    let enabled=false; try { enabled=active(); } catch {}
    if(!enabled) { if(previous!==false) log('lease inactive'); previous=false; return; }
    previous=true;
    if(!Number.isFinite(now) || now-last<60) return;
    last=now; busy=true;
    try { log(await rpc() ? 'RPC success' : 'RPC failed'); }
    catch { log('RPC failed'); }
    finally { busy=false; }
  }};
}
function main(args) {
  if(args.length !== 4 || args[0] !== '--lease' || !args[1].startsWith('/') || args[2] !== '--control' || !args[3].startsWith('/')) {
    console.error('Usage: node presence_rpc.cjs --lease /absolute/lease.json --control /absolute/exposure-control.json'); return 2;
  }
  const worker=createWorker({active:()=>controlEnabled(args[3]) && leaseActive(args[1])});
  worker.tick(); const timer=setInterval(()=>worker.tick(),1000);
  for(const signal of ['SIGTERM','SIGINT']) process.once(signal,()=>{clearInterval(timer);process.exit(0);});
  return 0;
}
module.exports={leaseActive,controlEnabled,rpcOnce,createWorker,main};
if(require.main===module) process.exitCode=main(process.argv.slice(2));
