import { RulesController } from "../../../web/src/rules-client";
import { LiveSession } from "../../../web/src/session";
import { Command, decodeMessage, encodeMessage, type JsonObject } from "../../../web/src/protocol";
import { canonical, sha256, rulesErrorMessage } from "../../../web/src/rules-identity";

const root = new URL("../", location.href);
const config = await (await fetch(new URL("case.json", root))).json();
const rules = new RulesController(() => {});
const session = new LiveSession();
let identity: JsonObject;
let serverHello: JsonObject | undefined;
const checks: string[] = [];
const ensure = (ok: unknown, label: string) => { if (!ok) throw new Error(label); checks.push(label); };
async function seal(value: JsonObject) {
  const unsigned = { ...value }; delete unsigned.bundle_id; delete unsigned.code_id;
  const code = { protocol_version: unsigned.protocol_version, bridge_schema: unsigned.bridge_schema,
    cpp_hash: unsigned.cpp_hash, bindings_abi: unsigned.bindings_abi,
    interaction_schemas: unsigned.interaction_schemas };
  unsigned.code_id = await sha256(new TextEncoder().encode(`qsan-rules-code-v1\0${canonical(code)}`));
  return { ...unsigned, bundle_id: await sha256(new TextEncoder().encode(`qsan-rules-bundle-v1\0${canonical(unsigned)}`)) };
}
async function rejected(name: string, value: JsonObject | null, expected: string, reconnect = false) {
  await new Promise<void>((resolve, reject) => {
    const socket = new WebSocket(config.ws);
    const timer = setTimeout(() => finish(new Error(`${name}: timeout`)), 10000);
    let finished = false;
    function finish(error?: Error) {
      if (finished) return; finished = true; clearTimeout(timer); socket.close();
      if (error) reject(error); else resolve();
    }
    socket.onmessage = event => {
      try {
        const frame = decodeMessage(event.data);
        if (frame.command === Command.CHECK_VERSION) {
          socket.send(encodeMessage({ v: 2, type: "request", source: "client", destination: "lobby",
            message_id: "1", command: Command.SIGNUP, payload: { schema_version: 2, screen_name: name,
              avatar: "caocao", reconnect_requested: reconnect,
              ...(value === null ? {} : { rules_bundle: value }) } }));
        } else {
          ensure(frame.command !== Command.SETUP, `${name}: no room setup`);
          if (frame.command === Command.SIGNUP) {
            ensure(frame.payload.accepted === false && frame.payload.error_code === expected, `${name}: ${expected}`);
            finish();
          } else if (frame.command === Command.WARN) finish(new Error(`${name}: no correlated signup rejection`));
        }
      } catch (error) { finish(error as Error); }
    };
    socket.onerror = () => finish(new Error(`${name}: websocket failure`));
    socket.onclose = () => { if (!finished) finish(new Error(`${name}: closed without rejection`)); };
  });
}

try {
  session.setRulesProvider(async (active, hello) => {
    serverHello = hello;
    if (hello && config.expect_code_mismatch)
      hello = { ...hello, rules_bundle: await seal({ ...(hello.rules_bundle as JsonObject), cpp_hash: "f".repeat(64) }) };
    const loaded = await rules.initialize(active, hello);
    if (loaded !== null) identity = loaded;
    return loaded;
  });
  await new Promise<void>((resolve, reject) => {
    const timer = setTimeout(() => { off(); reject(new Error("live handshake timeout")); }, 45000);
    const off = session.onChange(() => {
      if (session.phase === "active" || session.phase === "failed") {
        off(); clearTimeout(timer);
        if (session.phase === "active" || config.expect_reload === true || config.expect_code_mismatch === true) resolve();
        else reject(new Error(session.error));
      }
    });
    session.connect({ wsUrl: config.ws, screenName: "w2-web", avatar: "caocao", reconnect: false });
  });
  if (config.expect_code_mismatch === true) {
    ensure(session.phase === "failed" && session.error === rulesErrorMessage("rules_version_mismatch"),
      "valid different code_id rejected before content fetch");
  } else if (config.expect_reload === true) {
    ensure(session.phase === "failed" && session.error === rulesErrorMessage("rules_reload_required"),
      config.corrupt_content ? "changed content byte rejected before admission"
        : "old Web loader rejects new paired WASM before admission");
  } else {
    ensure(session.phase === "active", "real server/WASM signup reaches active");
    ensure(serverHello !== undefined && serverHello.rules_content !== undefined,
      "server hello carries declared rules_content");
    ensure(canonical(identity!) === canonical(config.native.rules_bundle), "native/WASM exporter identity equality");
    session.disconnect(); rules.dispose();
    await rejected("old-web", null, "rules_identity_required");
    await rejected("bad-seal", { ...identity!, lua_hash: "0".repeat(64) }, "rules_identity_invalid");
    await rejected("code-id", { ...identity!, code_id: "0".repeat(64) }, "rules_identity_invalid");
    await rejected("card-order", config.variants["card-order"], "rules_version_mismatch");
    await rejected("lua-content", config.variants["lua-content"], "rules_version_mismatch");
    await rejected("old-bridge", await seal({ ...identity!, bridge_schema: 1 }), "rules_version_mismatch");
    await rejected("unsupported", await seal({ ...identity!, content_profile: "extension-v1" }), "rules_content_unsupported");
    const missing = { ...(identity!.interaction_schemas as JsonObject) }; delete missing[Object.keys(missing)[0]];
    await rejected("missing-interaction", await seal({ ...identity!, interaction_schemas: missing }), "rules_interaction_unsupported");
    await rejected("reconnect-mismatch", await seal({ ...identity!, lua_hash: "0".repeat(64) }), "rules_version_mismatch", true);
  }
  session.disconnect(); rules.dispose();
  await fetch(new URL("report", root), { method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ schema_version: 1, status: "PASS", checks, ...((config.expect_reload || config.expect_code_mismatch) ? {} : { identity: identity! }) }) });
} catch (error) {
  session.disconnect(); rules.dispose();
  await fetch(new URL("report", root), { method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ schema_version: 1, status: "FAIL", error: String(error), checks }) });
}
