// Snapshot-only native C ABI parity page. Production stream ingress is tested
// by check-rules-ingress.py and is intentionally not exercised here.
const token = new URLSearchParams(location.search).get('token');
async function runRound(inputs, content, mode) {
  const worker = new Worker('/rules-worker.mjs', { type: 'module' });
  try {
    return await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('snapshot Worker timeout')), 120000);
      worker.onmessage = event => { clearTimeout(timer); event.data?.error ? reject(new Error(event.data.error)) : resolve(event.data); };
      worker.onerror = event => { clearTimeout(timer); reject(new Error(event.message || 'snapshot Worker failed')); };
      worker.postMessage({ requests: inputs, content, mode });
    });
  } finally { worker.terminate(); }
}
let report;
try {
  const inputs = await (await fetch('/input.json', { cache: 'no-store' })).json();
  const hello = await (await fetch('/hello.json', { cache: 'no-store' })).json();
  const rounds = [await runRound(inputs, hello.rules_content), await runRound(inputs, hello.rules_content)];
  const malformed = await runRound(inputs.slice(0, 1), hello.rules_content, 'malformed');
  const recovery = await runRound(inputs.slice(0, 1), hello.rules_content, 'recovery');
  if (!malformed.transportError) throw new Error('missing real malformed evaluate evidence');
  report = { schema_version: 1, status: 'COMPLETE', rounds,
    transportError: malformed.transportError, recovery: recovery.records[0].response_utf8 };
} catch (error) { report = { schema_version: 1, status: 'ERROR', error: String(error?.stack || error) }; }
const response = await fetch(`/report/${token}`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(report) });
if (!response.ok) throw new Error('snapshot evidence publication failed');
