import { sha256 } from "./rules-identity";
import { fetchContent, installContent, validateContentManifest, verifyInstalledContent, type ContentFs } from "./rules-content";

interface SoloFs extends ContentFs { chdir(path: string): void }
interface SoloModule {
  FS: SoloFs;
  ENV: Record<string, string>;
  _qsan_solo_initialize(): number;
  _qsan_solo_start(): number;
  _qsan_solo_frame(): number;
  _qsan_solo_pump(): number;
  _qsan_solo_stop(): number;
}
interface WorkerScope {
  onmessage: ((event: MessageEvent) => void) | null;
  postMessage(value: unknown): void;
  close(): void;
  window?: unknown;
}
const scope = globalThis as unknown as WorkerScope;
const encoder = new TextEncoder();
const decoder = new TextDecoder("utf-8", { fatal: true });
const children = new Set<Worker>();
let runtime: SoloModule | null = null;
let pumpTimer: ReturnType<typeof setInterval> | undefined;
let started = false;
let failed = false;
let disposed = false;
let lastPhase: unknown = "";

// Emscripten owns a pthread pool. Keep handles so discarding a match also stops
// idle pool workers. No native memory is freed while any thread can access it.
const NativeWorker = globalThis.Worker;
globalThis.Worker = class extends NativeWorker {
  constructor(url: string | URL, options?: WorkerOptions) {
    super(url, options);
    children.add(this);
  }
};

function post(value: Record<string, unknown>): void {
  scope.postMessage({ schema_version: 1, ...value });
}
function result(): Record<string, unknown> {
  if (!runtime) throw new Error("單機執行環境尚未就緒");
  const bytes = runtime.FS.readFile("/work/solo-result.json");
  if (bytes.length > 8 * 1024 * 1024) throw new Error("單機回覆超過大小限制");
  const value: unknown = JSON.parse(decoder.decode(bytes));
  if (!value || typeof value !== "object" || Array.isArray(value)) throw new Error("單機回覆格式錯誤");
  return value as Record<string, unknown>;
}
function check(status: number): Record<string, unknown> {
  const value = result();
  if (status !== 0 || value.phase === "failed") throw new Error(String(value.error || value.reason || "單機執行失敗"));
  return value;
}
async function download(path: string, limit: number): Promise<Uint8Array> {
  const response = await fetch(path, { credentials: "omit", redirect: "error", cache: "no-cache" });
  if (!response.ok || response.redirected) throw new Error(`遊戲包缺少檔案：${path}`);
  const bytes = new Uint8Array(await response.arrayBuffer());
  if (bytes.length > limit) throw new Error(`遊戲檔案超過大小限制：${path}`);
  return bytes;
}
async function prepare(): Promise<void> {
  if (runtime || disposed || failed) throw new Error("請重新建立單機執行環境");
  if (!globalThis.crossOriginIsolated || typeof SharedArrayBuffer === "undefined")
    throw new Error("請使用完整遊戲包內的 StartGame.exe 啟動遊戲");
  const manifest = JSON.parse(decoder.decode(await download("/solo/manifest.json", 4 * 1024 * 1024)));
  if (manifest?.schema_version !== 1) throw new Error("單機發布版本不支援");
  const moduleEntry = manifest.runtime?.module;
  const wasmEntry = manifest.runtime?.wasm;
  if (moduleEntry?.path !== "qsanguosha_solo_wasm.mjs" || wasmEntry?.path !== "qsanguosha_solo_wasm.wasm")
    throw new Error("單機執行檔配對錯誤");
  const moduleUrl = new URL("/solo/qsanguosha_solo_wasm.mjs", globalThis.location.href).href;
  const wasmUrl = new URL("/solo/qsanguosha_solo_wasm.wasm", globalThis.location.href).href;
  const [moduleBytes, wasmBinary] = await Promise.all([
    download(moduleUrl, 16 * 1024 * 1024), download(wasmUrl, 512 * 1024 * 1024)
  ]);
  for (const [entry, bytes] of [[moduleEntry, moduleBytes], [wasmEntry, wasmBinary]] as const) {
    if (bytes.length !== entry.size || await sha256(bytes) !== entry.sha256)
      throw new Error("單機執行檔版本不一致，請重新解壓完整遊戲包");
  }
  const content = validateContentManifest(manifest.content);
  if (!content.files.some(entry => entry.path === "lua/ai/smart-ai.lua" && entry.role === "ai"))
    throw new Error("遊戲包缺少 AI 腳本");
  const files = await fetchContent(content, fetch, true);
  // Qt's timer shim needs these timer functions even in a Dedicated Worker.
  scope.window = Object.freeze({ setTimeout: globalThis.setTimeout.bind(globalThis),
    clearTimeout: globalThis.clearTimeout.bind(globalThis) });
  const options: Record<string, unknown> & { ENV?: Record<string, string>; FS?: SoloFs } = {
    noInitialRun: true, noExitRuntime: true, wasmBinary,
    // pthread workers import the original ES module from this same local bundle.
    mainScriptUrlOrBlob: moduleUrl,
    locateFile: (path: string) => {
      if (path.endsWith(".wasm")) return wasmUrl;
      if (path.endsWith(".mjs") || path.endsWith(".worker.js")) return new URL(path, moduleUrl).href;
      throw new Error(`未知的單機執行檔：${path}`);
    },
    print: (line: unknown) => console.info(String(line)),
    printErr: (line: unknown) => console.warn(String(line)),
    onAbort: (reason: unknown) => report(new Error(`單機執行中止：${String(reason)}`)),
  };
  options.preInit = [() => {
    if (!options.ENV) throw new Error("單機執行環境缺少 ENV");
    Object.assign(options.ENV, { HOME: "/userdata", XDG_CONFIG_HOME: "/userdata/config",
      XDG_DATA_HOME: "/userdata/data", TMPDIR: "/tmp",
      QSAN_ASSET_ROOT: "/assets", QSAN_USER_DATA_ROOT: "/userdata" });
  }];
  options.preRun = [(module?: SoloModule) => {
    const fs = (module ?? options).FS;
    if (!fs) throw new Error("單機執行環境缺少檔案系統");
    for (const path of ["/work", "/userdata/config", "/userdata/data", "/tmp"]) fs.mkdirTree(path);
    fs.chdir("/work");
    installContent(fs, files);
  }];
  // Files are shipped together and hash-checked above, never chosen by messages.
  const { default: factory } = await import(/* @vite-ignore */ moduleUrl);
  runtime = await factory(options) as SoloModule;
  if (failed || disposed) return;
  for (const name of ["_qsan_solo_initialize", "_qsan_solo_start", "_qsan_solo_frame", "_qsan_solo_pump", "_qsan_solo_stop"] as const)
    if (typeof runtime[name] !== "function") throw new Error("單機執行檔缺少必要入口");
  await verifyInstalledContent(runtime.FS, content, true);
  const catalog = check(runtime._qsan_solo_initialize());
  if (!Array.isArray(catalog.modes) || !Array.isArray(catalog.packages) || !Array.isArray(catalog.generals) || !catalog.defaults)
    throw new Error("單機內容清單格式錯誤");
  post({ type: "prepared", catalog });
}
function pump(): void {
  if (!runtime || failed || disposed) return;
  try {
    const output = check(runtime._qsan_solo_pump());
    if (!Array.isArray(output.frames) || output.frames.some(frame => typeof frame !== "string"))
      throw new Error("單機訊息格式錯誤");
    if (output.frames.length || output.phase !== lastPhase)
      post({ type: "frames", frames: output.frames, phase: output.phase });
    lastPhase = output.phase;
    if (output.phase === "closed") clearInterval(pumpTimer);
  } catch (error) { report(error); }
}
function report(error: unknown): void {
  if (disposed || failed) return;
  failed = true;
  clearInterval(pumpTimer);
  post({ type: "error", error: error instanceof Error ? error.message : String(error) });
}
function dispose(): void {
  disposed = true;
  clearInterval(pumpTimer);
  for (const child of children) child.terminate();
  children.clear();
  // A match is terminal: do not call C++ destructors against stopped threads.
  // The entire shared heap is reclaimed with its workers, never reused.
  runtime = null;
  post({ type: "closed" });
  scope.close();
}
async function receive(message: Record<string, unknown>): Promise<void> {
  if (message.type === "prepare") return prepare();
  if (message.type === "stop") {
    if (runtime && !failed) {
      check(runtime._qsan_solo_stop());
      if (pumpTimer === undefined) pumpTimer = setInterval(pump, 20);
      pump();
    } else post({ type: "closed" });
    return;
  }
  if (!runtime || failed || disposed) throw new Error("單機執行環境尚未就緒");
  if (message.type === "start") {
    if (started) throw new Error("一個執行環境只可建立一局");
    started = true;
    runtime.FS.writeFile("/work/solo-command.json", encoder.encode(JSON.stringify(message.options)));
    check(runtime._qsan_solo_start());
    pumpTimer = setInterval(pump, 20);
    pump();
  } else if (message.type === "frame") {
    if (!started || typeof message.frame !== "string") throw new Error("單機輸入格式錯誤");
    const bytes = encoder.encode(message.frame);
    if (bytes.length > 4 * 1024 * 1024) throw new Error("單機輸入超過大小限制");
    runtime.FS.writeFile("/work/solo-frame.txt", bytes);
    check(runtime._qsan_solo_frame());
    pump();
  }
}
let pending = Promise.resolve();
scope.onmessage = event => {
  if (!event.data || typeof event.data !== "object") return;
  if (event.data.type === "dispose") { dispose(); return; }
  pending = pending.then(() => receive(event.data)).catch(report);
};
