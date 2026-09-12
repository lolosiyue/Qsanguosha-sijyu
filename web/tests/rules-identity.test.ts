import { afterEach, describe, expect, it, vi } from "vitest";
import { webcrypto } from "node:crypto";
import { canonical, rulesCompatibilityError, rulesErrorMessage, sha256, verifyDeployment, verifyDeploymentBundle, verifyNativeIdentity } from "../src/rules-identity";
import { LiveSession } from "../src/session";
import { Command, decodeMessage, encodeMessage, type JsonObject } from "../src/protocol";

vi.stubGlobal("crypto", webcrypto);
const h = "a".repeat(64);
async function seal(value: JsonObject): Promise<JsonObject> {
  const result = { ...value }; delete result.bundle_id; delete result.code_id;
  const code = { protocol_version: result.protocol_version, bridge_schema: result.bridge_schema,
    cpp_hash: result.cpp_hash, bindings_abi: result.bindings_abi, interaction_schemas: result.interaction_schemas };
  result.code_id = await sha256(new TextEncoder().encode(`qsan-rules-code-v1\0${canonical(code)}`));
  result.bundle_id = await sha256(new TextEncoder().encode(`qsan-rules-bundle-v1\0${canonical(result)}`));
  return result;
}
async function bundle(): Promise<JsonObject> {
  return seal({ schema_version: 1, protocol_version: 2, bridge_schema: 2, ruleset: "sijyu",
    content_profile: "declared-v2", cpp_hash: h, card_registry_hash: h, lua_hash: h, bindings_abi: h,
    packages: ["standard", "wind"], interaction_schemas: { "1": h, "2": h } });
}

class Socket extends EventTarget {
  static OPEN = 1;
  static instances: Socket[] = [];
  readyState = 1;
  sent: ReturnType<typeof decodeMessage>[] = [];
  closed = false;
  constructor(readonly url: string) { super(); Socket.instances.push(this); }
  send(frame: string) { this.sent.push(decodeMessage(frame)); }
  close() { this.closed = true; }
  frame(command: number, payload: JsonObject, id = "1", type: "notification" | "reply" = "notification", reply_to?: string) {
    this.dispatchEvent(new MessageEvent("message", { data: encodeMessage({ v: 2, type, source: "lobby", destination: "client",
      message_id: id, command, payload, ...(reply_to ? { reply_to } : {}) }) }));
  }
}
const options = { wsUrl: "ws://localhost:9528", screenName: "web", avatar: "caocao", reconnect: false };
const settle = async () => { for (let i = 0; i < 8; i++) await Promise.resolve(); };
afterEach(() => { Socket.instances = []; vi.unstubAllGlobals(); vi.stubGlobal("crypto", webcrypto); });

describe("rules identity", () => {
  it("preserves a server content rejection before validating identity metadata", async () => {
    await expect(verifyNativeIdentity({ schema_version: 1, error_code: "rules_content_unsupported" }))
      .rejects.toThrow("rules_content_unsupported");
    await expect(verifyNativeIdentity({ schema_version: 1, error_code: "unknown_error" }))
      .rejects.toThrow("rules_identity_invalid");
  });
  it("checks the native seal and matches the same bundle", async () => {
    const value = await bundle(); expect(await verifyNativeIdentity(value)).toEqual(value);
    expect(rulesCompatibilityError(value, value)).toBe("");
    await expect(verifyNativeIdentity({ ...value, lua_hash: "b".repeat(64) })).rejects.toThrow("rules_identity_invalid");
  });
  it("binds code_id to every code field and excludes content fields", async () => {
    const base = await bundle();
    const fakeCode = { ...base, code_id: "f".repeat(64) };
    delete fakeCode.bundle_id;
    fakeCode.bundle_id = await sha256(new TextEncoder().encode(`qsan-rules-bundle-v1\0${canonical(fakeCode)}`));
    await expect(verifyNativeIdentity(fakeCode)).rejects.toThrow("rules_identity_invalid");
    const codeChanges: JsonObject[] = [
      { protocol_version: 3 }, { bridge_schema: 1 }, { cpp_hash: "b".repeat(64) },
      { bindings_abi: "b".repeat(64) }, { interaction_schemas: { "1": "b".repeat(64) } }];
    for (const change of codeChanges) {
      const changed = await seal({ ...base, ...change });
      expect(changed.code_id).not.toBe(base.code_id);
    }
    const contentChanges: JsonObject[] = [
      { ruleset: "other" }, { content_profile: "declared-v2" },
      { packages: ["wind", "standard"] }, { card_registry_hash: "b".repeat(64) },
      { lua_hash: "b".repeat(64) }];
    for (const change of contentChanges) {
      const changed = await seal({ ...base, ...change });
      expect(changed.code_id).toBe(base.code_id);
    }
  });
  it("rejects reordered packages/cards and altered same-name Lua content", async () => {
    const base = await bundle();
    for (const diff of [{ packages: ["wind", "standard"] }, { card_registry_hash: "b".repeat(64) }, { lua_hash: "b".repeat(64) }]) {
      expect(rulesCompatibilityError(base, await seal({ ...base, ...diff }))).toBe("rules_version_mismatch");
    }
  });
  it("distinguishes reload, unsupported interactions, profile, and missing metadata", async () => {
    const base = await bundle();
    expect(rulesCompatibilityError(base, await seal({ ...base, bridge_schema: 1 }))).toBe("rules_version_mismatch");
    expect(rulesCompatibilityError(base, await seal({ ...base, bindings_abi: "b".repeat(64) }))).toBe("rules_version_mismatch");
    expect(rulesCompatibilityError(base, { ...base, interaction_schemas: { "1": h } })).toBe("rules_interaction_unsupported");
    expect(rulesCompatibilityError(base, { ...base, content_profile: "extension-v1" })).toBe("rules_content_unsupported");
    expect(rulesCompatibilityError({}, base)).toBe("rules_reload_required");
    expect(rulesErrorMessage("rules_identity_required")).toContain("重新載入");
    expect(rulesErrorMessage("rules_version_mismatch")).toContain("規則版本不相符");
    expect(rulesErrorMessage("rules_interaction_unsupported")).toContain("此內容包不受支援");
  });
  it("rejects old loader/new binary, changed sidecar, missing pairing and old bridge", async () => {
    const files = { "qsanguosha_client_wasm.mjs": new Uint8Array([1]), "qsanguosha_client_wasm.wasm": new Uint8Array([2]),
      "qsanguosha_client_wasm.assets.json": new Uint8Array([3]) };
    const record: Record<string, string> = {};
    for (const [name, bytes] of Object.entries(files)) record[name] = await sha256(bytes);
    const manifest = { schema_version: 1, bridge_schema: 2, files: record };
    await verifyDeployment(manifest, files);
    const original = new TextEncoder().encode(canonical(manifest));
    const pinned = await sha256(original);
    await verifyDeploymentBundle(original, pinned, files);
    const replacement = { ...files, "qsanguosha_client_wasm.wasm": new Uint8Array([4]) };
    const newer = { ...manifest, files: { ...record,
      "qsanguosha_client_wasm.wasm": await sha256(replacement["qsanguosha_client_wasm.wasm"]) } };
    await verifyDeployment(newer, replacement);
    await expect(verifyDeploymentBundle(new TextEncoder().encode(canonical(newer)), pinned, replacement))
      .rejects.toThrow("rules_reload_required");
    await expect(verifyDeploymentBundle(original, "", files)).rejects.toThrow("rules_reload_required");
    for (const name of Object.keys(files))
      await expect(verifyDeployment(manifest, { ...files, [name]: new Uint8Array([9]) })).rejects.toThrow("rules_reload_required");
    await expect(verifyDeployment({ ...manifest, bridge_schema: 1 }, files)).rejects.toThrow();
    await expect(verifyDeployment({}, files)).rejects.toThrow();
  });
});

describe("live rules handshake", () => {
  it("waits for runtime before connecting, then signs up with that exact identity", async () => {
    vi.stubGlobal("WebSocket", Socket);
    const session = new LiveSession(); const identity = await bundle();
    let prepareReady!: (value: JsonObject) => void;
    let helloReady!: (value: JsonObject) => void;
    let calls = 0;
    session.setRulesProvider((_session, hello) => new Promise(resolve => {
      if (hello) helloReady = resolve; else prepareReady = resolve;
      ++calls;
    }));
    session.connect(options); await settle(); expect(Socket.instances).toHaveLength(0);
    prepareReady(identity); await settle(); const socket = Socket.instances[0];
    socket.frame(Command.CHECK_VERSION, { schema_version: 1, game_version: "v", mod_name: "sijyu", card_count: 2, rules_bundle: identity });
    await settle(); expect(calls).toBe(2); expect(socket.sent).toHaveLength(0);
    helloReady(identity); await settle();
    expect(socket.sent).toHaveLength(1); expect(socket.sent[0].payload.rules_bundle).toEqual(identity);
    expect(session.phase).toBe("signup");
    socket.frame(Command.SIGNUP, { schema_version: 2, accepted: true, reconnected: false, player_id: "p1", room_id: 1 }, "2", "reply", socket.sent[0].message_id);
    socket.frame(Command.SETUP, { schema_version: 1 }, "3");
    expect(session.phase).toBe("active"); expect(socket.sent[1].command).toBe(Command.READY);
    session.disconnect();
  });
  it("rejects mismatching or missing server identity before SIGNUP or READY", async () => {
    vi.stubGlobal("WebSocket", Socket); const identity = await bundle();
    for (const server of [undefined, await seal({ ...identity, lua_hash: "b".repeat(64) }),
      await seal({ ...identity, interaction_schemas: { "1": h } })]) {
      const session = new LiveSession(); session.setRulesProvider(async () => identity);
      session.connect({ ...options, reconnect: true }); await settle(); const socket = Socket.instances.at(-1)!;
      socket.frame(Command.CHECK_VERSION, { schema_version: 1, card_count: 2, ...(server ? { rules_bundle: server } : {}) });
      await settle();
      expect(session.phase).toBe("failed"); expect(socket.sent).toHaveLength(0); expect(socket.closed).toBe(true);
    }
  });
  it("rejects frames and stale Hello completion while Hello rules remain pending", async () => {
    vi.stubGlobal("WebSocket", Socket); const identity = await bundle();
    let resolveHello!: (value: JsonObject) => void;
    let calls = 0;
    const session = new LiveSession();
    session.setRulesProvider((_session, hello) => {
      ++calls;
      if (!hello) return Promise.resolve(identity);
      return new Promise(resolve => { resolveHello = resolve; });
    });
    session.connect(options); await settle(); const socket = Socket.instances[0];
    socket.frame(Command.CHECK_VERSION, { schema_version: 1, card_count: 2, rules_bundle: identity });
    await settle(); expect(session.phase).toBe("hello"); expect(calls).toBe(2);
    socket.frame(Command.SPEAK, { schema_version: 1, text: "too soon" }, "2");
    await settle(); expect(session.phase).toBe("failed"); expect(socket.sent).toHaveLength(0);
    resolveHello(identity); await settle(); expect(socket.sent).toHaveLength(0);

    const second = new LiveSession(); let resolveSecond!: (value: JsonObject) => void;
    second.setRulesProvider((_session, hello) => hello
      ? new Promise(resolve => { resolveSecond = resolve; }) : Promise.resolve(identity));
    second.connect(options); await settle(); const secondSocket = Socket.instances.at(-1)!;
    secondSocket.frame(Command.CHECK_VERSION, { schema_version: 1, card_count: 2, rules_bundle: identity });
    await settle(); second.disconnect(); resolveSecond(identity); await settle();
    expect(secondSocket.sent).toHaveLength(0);
  });
  it("does not revive a cancelled generation or connect after loader failure", async () => {
    vi.stubGlobal("WebSocket", Socket); const identity = await bundle();
    const session = new LiveSession(); let ready!: (value: JsonObject) => void;
    session.setRulesProvider(() => new Promise(resolve => { ready = resolve; }));
    session.connect(options); await settle(); session.disconnect(); ready(identity); await settle();
    expect(Socket.instances).toHaveLength(0);
    session.setRulesProvider(async () => { throw new Error("rules_reload_required"); });
    session.connect(options); await settle(); expect(Socket.instances).toHaveLength(0);
    expect(session.error).toContain("重新載入");
  });
  it("never silently opts out when runtime provider is absent", async () => {
    vi.stubGlobal("WebSocket", Socket); const session = new LiveSession();
    session.connect(options); await settle(); expect(Socket.instances).toHaveLength(0);
    expect(session.phase).toBe("failed");
  });
});
