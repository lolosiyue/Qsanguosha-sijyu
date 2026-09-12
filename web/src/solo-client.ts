import type { SessionTransport } from "./session";

export interface SoloOptions {
  mode: string;
  enabled_packages: string[];
  ban_generals: string[];
  operation_timeout: number;
  ai_delay: number;
}
export interface SoloCatalog {
  modes: { id: string; name: string; player_count: number }[];
  packages: { id: string; name: string; enabled: boolean; forbidden: boolean }[];
  generals: { id: string; name: string; package: string }[];
  defaults: SoloOptions;
}

class SoloTransport implements SessionTransport {
  readyState: number = 1;
  private events = new EventTarget();
  constructor(private owner: SoloController) {}
  addEventListener(type: "message", listener: (event: MessageEvent) => void): void;
  addEventListener(type: "error" | "close", listener: () => void): void;
  addEventListener(type: string, listener: ((event: MessageEvent) => void) | (() => void)): void {
    this.events.addEventListener(type, listener as EventListener);
  }
  send(frame: string): void {
    if (this.readyState !== 1) throw new Error("單機對局已關閉");
    this.owner.send(frame);
  }
  receive(frame: string): void {
    if (this.readyState === 1) this.events.dispatchEvent(new MessageEvent("message", { data: frame }));
  }
  fail(): void {
    if (this.readyState === 1) this.events.dispatchEvent(new Event("error"));
  }
  finish(): void {
    if (this.readyState === 3) return;
    this.readyState = 3;
    this.events.dispatchEvent(new Event("close"));
  }
  close(): void {
    if (this.readyState === 3) return;
    this.readyState = 3;
    void this.owner.close();
  }
}

// One Worker owns exactly one match. Every restart allocates a new WASM heap;
// late messages from a stopped worker cannot enter a later match's transport.
export class SoloController {
  catalog: SoloCatalog | null = null;
  error = "";
  status: "idle" | "loading" | "ready" | "starting" | "running" | "stopping" | "failed" = "idle";
  private worker: Worker | null = null;
  private transport: SoloTransport | null = null;
  private generation = 0;
  private prepared: { resolve(): void; reject(error: Error): void } | null = null;
  private timer: ReturnType<typeof setTimeout> | undefined;
  private closing: Promise<void> | null = null;
  private closed: (() => void) | null = null;
  constructor(private onChange: () => void) {}

  async prepare(): Promise<void> {
    await this.close();
    if (!globalThis.crossOriginIsolated || typeof SharedArrayBuffer === "undefined") {
      this.error = "請解壓完整遊戲包，並使用 StartGame.exe 啟動 Chrome 或 Edge。";
      this.status = "failed";
      this.onChange();
      throw new Error(this.error);
    }
    this.error = "";
    this.catalog = null;
    this.status = "loading";
    const generation = ++this.generation;
    const worker = new Worker(new URL("./solo-worker.ts", import.meta.url), { type: "module" });
    this.worker = worker;
    const result = new Promise<void>((resolve, reject) => { this.prepared = { resolve, reject }; });
    worker.onmessage = (event: MessageEvent) => {
      if (this.worker !== worker || generation !== this.generation) return;
      const message = event.data;
      if (!message || message.schema_version !== 1) return;
      if (message.type === "prepared") {
        this.clearTimer();
        this.catalog = message.catalog as SoloCatalog;
        this.status = "ready";
        this.prepared?.resolve();
        this.prepared = null;
      } else if (message.type === "frames") {
        for (const frame of message.frames as string[]) this.transport?.receive(frame);
        if (this.status === "starting" && message.phase === "active") {
          this.clearTimer();
          this.status = "running";
        }
        if (message.phase === "closed") this.finishClose();
      } else if (message.type === "closed") {
        this.finishClose();
      } else if (message.type === "error") {
        this.fail(String(message.error || "單機對局執行失敗"));
      }
      this.onChange();
    };
    worker.onerror = event => {
      event.preventDefault();
      if (this.worker === worker) this.fail(event.message || "單機執行環境無法啟動");
    };
    this.timer = setTimeout(() => this.fail("單機內容載入逾時，請重新啟動遊戲。"), 120000);
    worker.postMessage({ type: "prepare" });
    this.onChange();
    return result;
  }

  createTransport(options: SoloOptions): SessionTransport {
    if (this.status !== "ready" || !this.worker) throw new Error("請先載入單機內容");
    const transport = new SoloTransport(this);
    this.transport = transport;
    this.status = "starting";
    // LiveSession installs its listeners before native Hello can arrive.
    this.worker.postMessage({ type: "start", options });
    this.timer = setTimeout(() => this.fail("建立對局逾時，請返回首頁重試。"), 120000);
    this.onChange();
    return transport;
  }
  send(frame: string): void {
    if (!this.worker || this.status === "stopping" || this.status === "failed")
      throw new Error("單機對局已關閉");
    this.worker.postMessage({ type: "frame", frame });
  }
  private clearTimer(): void { clearTimeout(this.timer); this.timer = undefined; }
  private fail(reason: string): void {
    this.error = reason;
    this.status = "failed";
    this.prepared?.reject(new Error(reason));
    this.prepared = null;
    this.transport?.fail();
    void this.close().then(() => { this.status = "failed"; this.onChange(); });
    this.onChange();
  }
  close(): Promise<void> {
    if (this.closing) return this.closing;
    if (!this.worker) return Promise.resolve();
    this.clearTimer();
    this.prepared?.reject(new Error("單機載入已取消"));
    this.prepared = null;
    this.status = "stopping";
    this.closing = new Promise(resolve => { this.closed = resolve; });
    this.worker.postMessage({ type: "stop" });
    // A stuck native initialization must not leave the UI waiting forever.
    this.timer = setTimeout(() => this.finishClose(), 3000);
    return this.closing;
  }
  private finishClose(): void {
    this.clearTimer();
    const worker = this.worker;
    this.worker = null;
    ++this.generation;
    if (worker) {
      worker.postMessage({ type: "dispose" });
      // Let the worker terminate its own pthread pool before dropping its heap.
      setTimeout(() => worker.terminate(), 100);
    }
    const transport = this.transport;
    this.transport = null;
    transport?.finish();
    this.status = "idle";
    const resolve = this.closed;
    this.closed = null;
    this.closing = null;
    resolve?.();
  }
  terminate(): void {
    this.prepared?.reject(new Error("單機對局已關閉"));
    this.prepared = null;
    this.finishClose();
  }
}
