// Drive the actual compiled web/src/rules-worker.ts, not a fixture Worker.
const decoder = new TextDecoder('utf-8', { fatal: true });
const encoder = new TextEncoder();
const params = new URLSearchParams(location.search);
const reportUrl = `/report/${params.get('token')}`;
const generation = 7;

class Connection {
  constructor() {
    this.worker = new Worker('/rules-worker.mjs', { type: 'module' });
    this.pending = null;
    this.events = [];
    this.worker.onmessage = ({ data }) => {
      if (!this.pending || data?.schema_version !== 1 || data.generation !== generation) {
        this.fail(new Error('unsolicited or incorrectly correlated Worker message'));
        return;
      }
      const { resolve, reject, type, id, timer } = this.pending;
      this.pending = null;
      clearTimeout(timer);
      this.events.push(data.type);
      if (data.type === 'error') reject(new Error(data.error));
      else if (data.type !== type || (id !== undefined && data.id !== id)) {
        reject(new Error('wrong Worker response type/id'));
      } else resolve(data);
    };
    this.worker.onerror = event => this.fail(new Error(event.message || 'Worker script failed'));
    this.worker.onmessageerror = () => this.fail(new Error('Worker message deserialization failed'));
  }
  fail(error) {
    if (this.pending) {
      clearTimeout(this.pending.timer);
      this.pending.reject(error);
      this.pending = null;
    }
    this.fault = error;
  }
  send(message, type, transfer = []) {
    if (this.fault) return Promise.reject(this.fault);
    if (this.pending) return Promise.reject(new Error('concurrent test request'));
    return new Promise((resolve, reject) => {
      this.pending = { resolve, reject, type, id: message.id,
        timer: setTimeout(() => this.fail(new Error('production Worker timed out')), 120000) };
      this.worker.postMessage({ schema_version: 1, generation, ...message }, transfer);
    });
  }
  async evaluate(request, id) {
    const input = encoder.encode(JSON.stringify(request)).buffer;
    const result = await this.send({ type: 'evaluate', id, input }, 'result', [input]);
    if (!(result.bytes instanceof ArrayBuffer)) throw new Error('missing owned output bytes');
    return decoder.decode(result.bytes); // Never parse/re-serialize native output.
  }
  async dispose() { await this.send({ type: 'dispose' }, 'disposed'); }
  terminate() { this.worker.terminate(); }
}

async function run() {
  const response = await fetch('/input.json', { cache: 'no-store' });
  if (!response.ok) throw new Error('missing production test requests');
  const inputs = await response.json();
  const rounds = [];
  for (let round = 0; round < 2; ++round) {
    const connection = new Connection();
    try {
      const ready = await connection.send({ type: 'initialize' }, 'ready');
      const records = [];
      for (let index = 0; index < inputs.length; ++index) {
        records.push({ label: inputs[index].label,
          response_utf8: await connection.evaluate(inputs[index].request, index + 1) });
      }
      await connection.dispose();
      rounds.push({ registry: ready.info, records, events: connection.events });
    } finally { connection.terminate(); }
  }
  // A native transport failure must produce no usable reply. The production
  // Worker treats that as fatal; only a fresh Worker is used for recovery.
  const failed = new Connection();
  let transportError;
  try {
    await failed.send({ type: 'initialize' }, 'ready');
    await failed.evaluate(inputs[0].request, 1);
    const input = encoder.encode('{').buffer;
    try { await failed.send({ type: 'evaluate', id: 2, input }, 'result', [input]); }
    catch (error) { transportError = error.message; }
    if (!transportError?.includes('evaluation failed (2)')) throw new Error('wrong malformed-JSON failure');
    await failed.dispose();
  } finally { failed.terminate(); }
  const fresh = new Connection();
  let recovery;
  try {
    await fresh.send({ type: 'initialize' }, 'ready');
    recovery = await fresh.evaluate(inputs[0].request, 1);
    await fresh.dispose();
  } finally { fresh.terminate(); }
  return { schema_version: 1, status: 'COMPLETE', rounds, transportError, recovery };
}

let report;
try { report = await run(); }
catch (error) { report = { schema_version: 1, status: 'ERROR', error: error.stack || String(error) }; }
await fetch(reportUrl, { method: 'POST', headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify(report) });
document.body.textContent = report.status;
