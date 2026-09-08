// Only operations and deployment hashes are served. Expected results stay native.
const token = new URLSearchParams(location.search).get('token');
async function run(plan) {
  const worker = new Worker('./browser/rules-ingress-worker.mjs', { type: 'module' });
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => { worker.terminate(); reject(new Error('stream Worker timeout')); }, 120000);
    const finish = action => { clearTimeout(timer); worker.terminate(); action(); };
    worker.onmessage = event => finish(() => event.data.error
      ? reject(new Error(JSON.stringify(event.data))) : resolve(event.data));
    worker.onerror = event => finish(() => reject(new Error(event.message)));
    worker.onmessageerror = () => finish(() => reject(new Error('stream Worker messageerror')));
    worker.postMessage(plan);
  });
}
let report;
try {
  const response = await fetch('./input.json', { cache: 'no-store' });
  if (!response.ok) throw new Error('missing stream input');
  const plan = await response.json();
  const rounds = [await run(plan), await run(plan)];
  report = { schema_version: 1, status: 'COMPLETE', rounds };
} catch (error) {
  report = { schema_version: 1, status: 'ERROR', error: String(error?.stack || error) };
}
const published = await fetch(`/report/${token}`, { method: 'POST', headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify(report) });
if (!published.ok) throw new Error('stream evidence publication failed');
