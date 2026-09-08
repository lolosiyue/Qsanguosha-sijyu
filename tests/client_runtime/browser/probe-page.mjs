import { FixtureWorkerClient } from './fixture-worker-client.mjs';

const config = await (await fetch('./config.json', { cache: 'no-store' })).json();
const manifest = new Uint8Array(await (await fetch('./qsanguosha_rules_fixture_worker.assets.json',
  { cache: 'no-store' })).arrayBuffer());
const records = [];
const client = new FixtureWorkerClient();
let heartbeat = 0;
const timer = setInterval(() => ++heartbeat, 10);
try {
  for (const item of config.cases) {
    const input = new Uint8Array(await (await fetch(item.fixture, { cache: 'no-store' })).arrayBuffer());
    const owner = item.fault
      ? new FixtureWorkerClient({ workerUrl: new URL(`./${item.fault}/fixture-worker.mjs`, import.meta.url) })
      : client;
    const options = { hashSeed: item.hashSeed, timeoutMs: item.timeoutMs || 90000,
      ...(item.diagnostic ? { loggingRules: '*.critical=false' } : {}) };
    const expected = item.operation === 'manifest-mismatch'
      ? new Uint8Array([...manifest, 32]) : manifest;
    try {
      let pending;
      if (item.operation === 'disposed') owner.dispose();
      pending = owner.run(input, expected, options);
      if (item.operation === 'cancel') owner.cancel();
      const result = await pending;
      records.push({ tag: item.tag, outcome: 'result',
        output: new TextDecoder('utf-8', { fatal: true }).decode(result.bytes),
        environment: result.environment, logs: result.logs });
    } catch (error) {
      records.push({ tag: item.tag, outcome: 'error', error: String(error?.stack || error) });
    } finally {
      if (owner !== client) owner.dispose();
    }
  }
  const report = { schema_version: 1, status: 'COMPLETE', records, heartbeat,
    userAgent: navigator.userAgent };
  const response = await fetch('../report', {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(report),
  });
  if (!response.ok) throw new Error(`report rejected: HTTP ${response.status}`);
  document.body.textContent = 'Probe finished. Python validates canonical bytes and semantic expectations.';
} catch (error) {
  await fetch('../report', { method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ schema_version: 1, status: 'FAIL', error: String(error?.stack || error), records }) });
} finally {
  client.dispose();
  clearInterval(timer);
}
