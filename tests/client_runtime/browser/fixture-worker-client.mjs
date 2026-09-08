import { INPUT_LIMIT, OUTPUT_LIMIT } from '../wasm-fixture-host.mjs';

// Verification host only, not a persistent game-session API. A new Worker owns
// each run; cancel/supersede/dispose destroys the old VM rather than reusing it.
export class FixtureWorkerClient {
  constructor({ workerUrl = new URL('./fixture-worker.mjs', import.meta.url),
    workerFactory = url => new Worker(url, { type: 'module' }) } = {}) {
    this.workerUrl = workerUrl;
    this.workerFactory = workerFactory;
    this.serial = 0n;
    this.active = null;
    this.disposed = false;
  }

  run(input, manifest, { hashSeed, loggingRules, timeoutMs = 90000, signal } = {}) {
    if (this.disposed) return Promise.reject(new Error('fixture Worker client is disposed'));
    if (!(input instanceof Uint8Array) || input.length > INPUT_LIMIT
        || !(manifest instanceof Uint8Array) || manifest.length > 65536
        || !Number.isSafeInteger(timeoutMs) || timeoutMs <= 0 || timeoutMs > 600000) {
      return Promise.reject(new Error('invalid fixture Worker input or timeout'));
    }
    if (signal?.aborted) return Promise.reject(new Error('fixture Worker request aborted'));
    this.cancel('superseded');
    const id = String(++this.serial);
    return new Promise((resolve, reject) => {
      const entry = { worker: null, timer: null, finish: null };
      const onAbort = () => entry.finish(new Error('fixture Worker request aborted'));
      entry.finish = (error, result) => {
        if (this.active !== entry) return;
        this.active = null;
        clearTimeout(entry.timer);
        signal?.removeEventListener('abort', onAbort);
        if (entry.worker) {
          entry.worker.onmessage = entry.worker.onerror = entry.worker.onmessageerror = null;
          entry.worker.terminate();
        }
        if (error) reject(error); else resolve(result);
      };
      this.active = entry;
      try {
        entry.worker = this.workerFactory(this.workerUrl);
        entry.worker.onmessage = event => {
          if (this.active !== entry) return; // Queued messages from terminated generations.
          const message = event.data;
          if (message?.id !== id) return;
          if (message.schema_version !== 1) {
            entry.finish(new Error('invalid fixture Worker reply schema'));
          } else if (message.type === 'error') {
            const logs = Array.isArray(message.logs) ? message.logs.join('\n').slice(0, 16384) : '';
            entry.finish(new Error(`${String(message.error).slice(0, 8192)}\n${logs}`));
          } else if (message.type === 'result' && message.bytes instanceof ArrayBuffer
              && message.bytes.byteLength > 0 && message.bytes.byteLength <= OUTPUT_LIMIT) {
            entry.finish(null, { bytes: new Uint8Array(message.bytes),
              environment: message.environment, logs: message.logs });
          } else {
            entry.finish(new Error('invalid fixture Worker result'));
          }
        };
        entry.worker.onerror = event => {
          event.preventDefault?.();
          entry.finish(new Error(`fixture Worker error: ${event.message || 'script failed'}`));
        };
        entry.worker.onmessageerror = () => entry.finish(new Error('fixture Worker message decode failed'));
        signal?.addEventListener('abort', onAbort, { once: true });
        if (signal?.aborted) { onAbort(); return; }
        entry.timer = setTimeout(() => entry.finish(new Error('fixture Worker timed out')), timeoutMs);
        // Copy, then transfer. Do not detach the caller's fixture/manifest buffers.
        const inputCopy = new Uint8Array(input).buffer;
        const manifestCopy = new Uint8Array(manifest).buffer;
        entry.worker.postMessage({ schema_version: 1, type: 'run', id,
          input: inputCopy, manifest: manifestCopy, hashSeed, loggingRules }, [inputCopy, manifestCopy]);
      } catch (error) { entry.finish(error); }
    });
  }

  cancel(reason = 'cancelled') {
    this.active?.finish(new Error(`fixture Worker ${reason}`));
  }

  dispose() {
    this.cancel('disposed');
    this.disposed = true;
  }
}
