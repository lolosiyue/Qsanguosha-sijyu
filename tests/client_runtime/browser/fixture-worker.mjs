// Single-use Dedicated Worker. Module locations are fixed, never supplied by input.
import { executeFixture, INPUT_LIMIT } from '../wasm-fixture-host.mjs';

let used = false;
self.onmessage = async event => {
  const message = event.data;
  // A second request must not enter a CLI whose globals have already shut down.
  if (used) return;
  used = true;
  const id = typeof message?.id === 'string' ? message.id : '';
  const logs = [];
  let logSize = 0;
  const log = line => {
    const text = String(line).slice(0, 2048);
    while (logs.length && logSize + text.length > 16384) logSize -= logs.shift().length;
    logs.push(text); logSize += text.length;
  };
  try {
    if (message?.schema_version !== 1 || message.type !== 'run' || !/^[1-9][0-9]*$/.test(id)
        || !(message.input instanceof ArrayBuffer) || message.input.byteLength > INPUT_LIMIT
        || !(message.manifest instanceof ArrayBuffer) || message.manifest.byteLength > 65536
        || ![undefined, '0'].includes(message.hashSeed)
        || ![undefined, '*.critical=false'].includes(message.loggingRules)) {
      throw new Error('invalid fixture Worker request');
    }
    if (typeof DedicatedWorkerGlobalScope === 'undefined'
        || !(self instanceof DedicatedWorkerGlobalScope) || typeof document !== 'undefined') {
      throw new Error('fixture requires a real Dedicated Worker without a DOM');
    }
    const binaryUrl = new URL('qsanguosha_rules_fixture_worker.wasm', import.meta.url);
    const response = await fetch(binaryUrl, { cache: 'no-store', credentials: 'omit' });
    if (!response.ok) throw new Error(`WASM download failed: HTTP ${response.status}`);
    const wasmBinary = new Uint8Array(await response.arrayBuffer());
    if (wasmBinary.length < 8
        || ![0, 97, 115, 109, 1, 0, 0, 0].every((value, index) => wasmBinary[index] === value)) {
      throw new Error('missing/invalid WebAssembly binary');
    }
    const { default: factory } = await import('./qsanguosha_rules_fixture_worker.mjs');
    if (typeof factory !== 'function') throw new Error('module must export an Emscripten factory');
    if (globalThis.window !== undefined) throw new Error('fixture Worker must have isolated globals');
    // Qt 6.11.1 QWasmTimer uses window timers. Delegate to real Worker timers;
    // do not invent document, storage, navigator or browser UI implementations.
    globalThis.window = Object.freeze({
      setTimeout: globalThis.setTimeout.bind(globalThis),
      clearTimeout: globalThis.clearTimeout.bind(globalThis),
    });
    const bytes = await executeFixture(factory, new Uint8Array(message.input),
      new Uint8Array(message.manifest), {
        wasmBinary, hashSeed: message.hashSeed, loggingRules: message.loggingRules, printErr: log,
        hashBytes: async input => [...new Uint8Array(await crypto.subtle.digest('SHA-256', input))]
          .map(value => value.toString(16).padStart(2, '0')).join(''),
      });
    self.postMessage({ schema_version: 1, type: 'result', id, bytes: bytes.buffer,
      logs, environment: { dedicatedWorker: true, documentAbsent: true,
        timerBridge: Object.keys(globalThis.window).sort() } }, [bytes.buffer]);
  } catch (error) {
    self.postMessage({ schema_version: 1, type: 'error', id,
      error: String(error?.stack || error).slice(0, 8192), logs });
  } finally {
    self.close(); // The owner also terminates the Worker on every settlement.
  }
};
