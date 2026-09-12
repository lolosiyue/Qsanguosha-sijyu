import { RULES_BRIDGE_SCHEMA, canonical, sha256, verifyDeploymentBundle, verifyNativeIdentity } from "./rules-identity";
import { fetchContent, installContent, validateContentManifest, verifyInstalledContent } from "./rules-content";

// A persistent native rules session. Browser messages never supply module URLs.
declare const __QSAN_RULES_DEPLOYMENT_ID__: string;

const INPUT_LIMIT = 4 * 1024 * 1024;
const OUTPUT_LIMIT = 8 * 1024 * 1024;
const MANIFEST_LIMIT = 65536;
const decoder = new TextDecoder("utf-8", { fatal: true });
const encoder = new TextEncoder();

interface WasmFs {
  readFile(path: string): Uint8Array;
  writeFile(path: string, bytes: Uint8Array): void;
  mkdirTree(path: string): void;
  chdir(path: string): void;
  readdir(path: string): string[];
  lstat(path: string): { mode: number; size: number };
  isDir(mode: number): boolean;
  isFile(mode: number): boolean;
}

interface RulesModule {
  FS: WasmFs;
  ENV: Record<string, string>;
  _qsan_client_bridge_schema(): number;
  _qsan_client_code_identity(): number;
  _qsan_client_initialize(): number;
  _qsan_client_stream(): number;
  _qsan_client_shutdown(): number;
}

interface FactoryOptions {
  FS?: WasmFs;
  ENV?: Record<string, string>;
  noInitialRun: boolean;
  noExitRuntime: boolean;
  wasmBinary: Uint8Array;
  locateFile(path: string): string;
  print(line: unknown): void;
  printErr(line: unknown): void;
  onAbort(reason: unknown): void;
  preInit: (() => void)[];
  preRun: ((module?: RulesModule) => void)[];
}

// The project also compiles DOM code. Keep this Worker surface narrow instead of
// mixing the conflicting DOM and WebWorker ambient libraries into tsconfig.
interface RulesWorkerScope {
  onmessage: ((event: MessageEvent<unknown>) => void) | null;
  postMessage(message: unknown, transfer?: Transferable[]): void;
  close(): void;
  location: { href: string; origin: string };
  window?: { setTimeout: typeof setTimeout; clearTimeout: typeof clearTimeout };
}
const worker = globalThis as unknown as RulesWorkerScope;
let phase: "new" | "initializing" | "ready" | "failed" | "disposed" = "new";
let generation: number | undefined;
let runtime: RulesModule | undefined;
let aborted = false;
const logs: string[] = [];
let logSize = 0;

function log(line: unknown): void {
  const text = String(line).slice(0, 2048);
  while (logs.length && logSize + text.length > 16384) logSize -= logs.shift()!.length;
  logs.push(text);
  logSize += text.length;
}

function record(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function integer(value: unknown, minimum: number): value is number {
  return typeof value === "number" && Number.isSafeInteger(value) && value >= minimum;
}

function reportError(error: unknown, requestGeneration: number, id?: number): void {
  worker.postMessage({ schema_version: 1, type: "error", generation: requestGeneration,
    ...(id === undefined ? {} : { id }),
    error: (error instanceof Error ? error.message : String(error)).slice(0, 8192),
    logs: [...logs] });
}

function ownedBuffer(bytes: Uint8Array): ArrayBuffer {
  const buffer = new ArrayBuffer(bytes.byteLength);
  new Uint8Array(buffer).set(bytes);
  return buffer;
}

function readOutput(fs: WasmFs, path: string): Uint8Array {
  const stat = fs.lstat(path);
  if (!fs.isFile(stat.mode) || stat.size <= 0 || stat.size > OUTPUT_LIMIT) {
    throw new Error("WASM result must be a nonempty file of at most 8 MiB");
  }
  const bytes = new Uint8Array(fs.readFile(path));
  if (bytes.byteLength !== stat.size) throw new Error("WASM result size changed while reading");
  return bytes;
}

async function download(url: URL, limit?: number): Promise<Uint8Array> {
  const response = await fetch(url, { cache: "no-store", credentials: "omit", redirect: "error" });
  if (!response.ok) throw new Error(`WASM download failed: HTTP ${response.status}`);
  if (limit !== undefined && Number(response.headers.get("Content-Length")) > limit) {
    throw new Error("WASM manifest exceeds 64 KiB");
  }
  const bytes = new Uint8Array(await response.arrayBuffer());
  if (limit !== undefined && bytes.length > limit) throw new Error("WASM manifest exceeds 64 KiB");
  return bytes;
}

async function prepare(requestGeneration: number): Promise<void> {
  if (phase !== "new") throw new Error("WASM Worker has already been initialized");
  generation = requestGeneration;
  phase = "initializing";
  if (typeof document !== "undefined" || typeof worker.close !== "function"
      || worker.window !== undefined) {
    throw new Error("Native rules require an isolated Dedicated Worker");
  }
  const moduleUrl = new URL("/rules/qsanguosha_client_wasm.mjs", worker.location.href);
  const wasmUrl = new URL("/rules/qsanguosha_client_wasm.wasm", worker.location.href);
  const bundleUrl = new URL("/rules/qsanguosha_client_wasm.bundle.json", worker.location.href);
  const [wasmBinary, moduleBytes, bundleBytes] = await Promise.all([
    download(wasmUrl), download(moduleUrl, 16 * 1024 * 1024), download(bundleUrl, MANIFEST_LIMIT),
  ]);
  await verifyDeploymentBundle(bundleBytes,
    typeof __QSAN_RULES_DEPLOYMENT_ID__ === "string" ? __QSAN_RULES_DEPLOYMENT_ID__ : "", {
    "qsanguosha_client_wasm.mjs": moduleBytes,
    "qsanguosha_client_wasm.wasm": wasmBinary,
  });
  if (wasmBinary.length < 8
      || ![0, 97, 115, 109, 1, 0, 0, 0].every((value, index) => wasmBinary[index] === value)) {
    throw new Error("Missing or invalid WebAssembly binary");
  }
  // Import precisely the bytes just verified, avoiding a second cached fetch.
  const verifiedUrl = URL.createObjectURL(new Blob([ownedBuffer(moduleBytes)], { type: "text/javascript" }));
  let factory;
  try { ({ default: factory } = await import(/* @vite-ignore */ verifiedUrl)); }
  finally { URL.revokeObjectURL(verifiedUrl); }
  if (typeof factory !== "function") throw new Error("WASM module must export an Emscripten factory");
  // Qt's Worker timer implementation names window; provide only real timers.
  worker.window = Object.freeze({
    setTimeout: globalThis.setTimeout.bind(globalThis),
    clearTimeout: globalThis.clearTimeout.bind(globalThis),
  });
  const options: FactoryOptions = {
    noInitialRun: true, noExitRuntime: true, wasmBinary,
    locateFile(path) {
      if (!path.endsWith(".wasm")) throw new Error(`Unexpected WASM module dependency: ${path}`);
      return wasmUrl.href;
    },
    print: log, printErr: log,
    onAbort(reason) {
      aborted = true;
      log(`WASM abort: ${String(reason)}`);
      // Qt callbacks can abort while the main thread is displaying an earlier
      // result. Invalidate that result immediately, even without a new query.
      if (phase === "ready") {
        phase = "failed";
        reportError(new Error("WASM runtime aborted"), requestGeneration);
      }
    },
    preInit: [], preRun: [],
  };
  // Settings has static storage duration. Isolate ENV before native constructors.
  options.preInit.push(() => {
    if (!options.ENV) throw new Error("WASM module does not export ENV before initialization");
    Object.assign(options.ENV, { HOME: "/userdata", XDG_CONFIG_HOME: "/userdata/config",
      XDG_DATA_HOME: "/userdata/data", TMPDIR: "/tmp" });
    delete options.ENV.QT_HASH_SEED;
  });
  options.preRun.push(module => {
    const fs = (module ?? options).FS;
    if (!fs) throw new Error("WASM module does not export FS");
    for (const path of ["/work", "/userdata/config", "/userdata/data", "/tmp"]) fs.mkdirTree(path);
    fs.chdir("/work");
  });
  runtime = await factory(options) as RulesModule;
  if (aborted || !runtime?.FS || typeof runtime._qsan_client_bridge_schema !== "function"
      || runtime._qsan_client_bridge_schema() !== RULES_BRIDGE_SCHEMA
      || typeof runtime._qsan_client_code_identity !== "function"
      || typeof runtime._qsan_client_initialize !== "function"
      || typeof runtime._qsan_client_stream !== "function"
      || typeof runtime._qsan_client_shutdown !== "function") {
    throw new Error("WASM initialization failed or client runtime exports are missing");
  }
  const codeStatus = runtime._qsan_client_code_identity();
  if (aborted || codeStatus !== 0) throw new Error(`WASM code identity failed (${codeStatus})`);
  const code = JSON.parse(decoder.decode(readOutput(runtime.FS, "/work/code.json")));
  if (!record(code) || code.protocol_version !== 2 || code.bridge_schema !== RULES_BRIDGE_SCHEMA
      || typeof code.cpp_hash !== "string" || typeof code.bindings_abi !== "string"
      || !record(code.interaction_schemas) || typeof code.code_id !== "string")
    throw new Error("WASM returned an invalid code identity");
  const unsignedCode = { protocol_version: code.protocol_version, bridge_schema: code.bridge_schema,
    cpp_hash: code.cpp_hash, bindings_abi: code.bindings_abi,
    interaction_schemas: code.interaction_schemas };
  if (await sha256(encoder.encode(`qsan-rules-code-v1\0${canonical(unsignedCode)}`)) !== code.code_id)
    throw new Error("rules_identity_invalid");
  worker.postMessage({ schema_version: 1, type: "prepared", generation, code });
}

async function initialize(requestGeneration: number, serverIdentity: unknown, contentValue: unknown): Promise<void> {
  if (phase !== "initializing" || !runtime || aborted) throw new Error("rules_reload_required");
  const server = await verifyNativeIdentity(serverIdentity);
  const code = JSON.parse(decoder.decode(readOutput(runtime.FS, "/work/code.json")));
  if (!record(code) || code.code_id !== server.code_id) throw new Error("rules_version_mismatch");
  const content = validateContentManifest(contentValue);
  const files = await fetchContent(content);
  installContent(runtime.FS, files);
  await verifyInstalledContent(runtime.FS, content);
  if (aborted) throw new Error("WASM initialization aborted");
  const status = runtime._qsan_client_initialize();
  if (aborted || status !== 0) throw new Error(`WASM client initialization failed (${status})`);
  const info: unknown = JSON.parse(decoder.decode(readOutput(runtime.FS, "/work/init.json")));
  if (!record(info) || info.schema_version !== RULES_BRIDGE_SCHEMA || !integer(info.card_count, 1)
      || !Array.isArray(info.registry) || info.registry.length !== info.card_count) {
    throw new Error("WASM returned an invalid card registry");
  }
  const ids = new Set<number>();
  for (const entry of info.registry as unknown[]) {
    if (!record(entry) || !integer(entry.id, 0) || ids.has(entry.id)
        || typeof entry.object_name !== "string" || !entry.object_name
        || typeof entry.class_name !== "string" || !entry.class_name
        || typeof entry.package !== "string" || !integer(entry.suit, 0) || entry.suit > 6
        || !integer(entry.number, 0)) {
      throw new Error("WASM returned an invalid card registry entry");
    }
    ids.add(entry.id);
  }
  const declaredExtensions = content.files.filter(entry => entry.role === "rules"
    && entry.path.startsWith("extensions/")).map(entry => entry.path.slice("extensions/".length)).sort();
  if (canonical(info.extension_files) !== canonical(declaredExtensions))
    throw new Error("rules_content_unsupported");
  const native = await verifyNativeIdentity(info.rules_bundle);
  if (canonical(native) !== canonical(server)) {
    const fields = [...new Set([...Object.keys(native), ...Object.keys(server)])]
      .filter(key => canonical(native[key]) !== canonical(server[key]));
    throw new Error(`rules_version_mismatch: identity fields ${fields.join(", ")}`);
  }
  // Arm raw-frame ingress before the transport exists. This permanently locks
  // out the external-snapshot entry, so no browser state can replace the
  // native one for the rest of this Engine's life.
  const armed = runStream({ schema_version: 1, action: "reset", generation: requestGeneration });
  if (armed.success !== true) {
    throw new Error(`WASM stream reset rejected: ${String(armed.reason)}`);
  }
  phase = "ready";
  worker.postMessage({ schema_version: 1, type: "ready", generation, info });
}

// One operation per native call; the host reads and replaces fixed paths.
function runStream(operation: Record<string, unknown>): Record<string, unknown> {
  if (!runtime || aborted) throw new Error("WASM runtime is unavailable");
  const bytes = encoder.encode(JSON.stringify(operation));
  if (bytes.byteLength === 0 || bytes.byteLength > INPUT_LIMIT) {
    throw new Error("WASM stream operation must be 1 byte to 4 MiB of JSON");
  }
  runtime.FS.writeFile("/work/stream.json", bytes);
  const status = runtime._qsan_client_stream();
  if (aborted || status !== 0) throw new Error(`WASM stream operation failed (${status})`);
  const result: unknown = JSON.parse(decoder.decode(readOutput(runtime.FS, "/work/stream-result.json")));
  if (!record(result) || result.schema_version !== 1 || typeof result.success !== "boolean"
      || typeof result.reason !== "string" || !record(result.status)) {
    throw new Error("WASM returned an invalid stream result");
  }
  return result;
}

// Frames and queries only. A reset belongs to initialization, never to a
// message: replaying it would silently discard committed native state.
function stream(message: Record<string, unknown>): void {
  if (phase !== "ready" || !runtime || aborted) throw new Error("WASM runtime is unavailable");
  if (!integer(message.id, 1) || !Array.isArray(message.ops) || message.ops.length === 0
      || message.ops.length > 256) {
    throw new Error("WASM stream request must contain an id and 1 to 256 operations");
  }
  const results: Record<string, unknown>[] = [];
  for (const operation of message.ops as unknown[]) {
    if (!record(operation) || operation.schema_version !== 1 || operation.generation !== generation
        || (operation.action !== "frame" && operation.action !== "query")) {
      throw new Error("WASM stream operations must be current-generation frames or queries");
    }
    const result = runStream(operation);
    results.push(result);
    // A rejected operation ends the batch; the controller decides what is fatal.
    if (result.success !== true) break;
  }
  worker.postMessage({ schema_version: 1, type: "stream", generation, id: message.id, results });
}

function dispose(requestGeneration: number): void {
  try {
    if (runtime && !aborted) {
      const status = runtime._qsan_client_shutdown();
      if (status !== 0) throw new Error(`WASM client shutdown failed (${status})`);
    }
    worker.postMessage({ schema_version: 1, type: "disposed", generation: requestGeneration });
  } finally {
    runtime = undefined;
    phase = "disposed";
    worker.close();
  }
}

async function receive(value: unknown): Promise<void> {
  if (phase === "disposed") return;
  const message = record(value) ? value : {};
  const requestGeneration = integer(message.generation, 0) ? message.generation : generation ?? 0;
  const id = integer(message.id, 1) ? message.id : undefined;
  // A delayed message from another session must not mutate the live engine.
  if (generation !== undefined && requestGeneration !== generation) {
    reportError(new Error("Stale WASM session generation"), requestGeneration, id);
    return;
  }
  try {
    if (message.schema_version !== 1 || !integer(message.generation, 0)) {
      throw new Error("Invalid WASM Worker message");
    }
    if (message.type === "dispose") dispose(requestGeneration);
    else if (message.type === "prepare") await prepare(requestGeneration);
    else if (message.type === "initialize") await initialize(requestGeneration, message.identity, message.content);
    else if (message.type === "stream") stream(message);
    else throw new Error("Unknown WASM Worker request");
  } catch (error) {
    phase = "failed";
    reportError(error, requestGeneration, id);
  }
}

// Async module loading and asset hashing must never overlap another request.
let queue = Promise.resolve();
worker.onmessage = event => { queue = queue.then(() => receive(event.data)); };

export {};
