import { afterEach, describe, expect, it, vi } from "vitest";
import { LiveSession } from "../src/session";
import { Command, decodeMessage, encodeMessage, type JsonObject, type MessageType } from "../src/protocol";

// The rules provider is isolated here; production identity verification has its own suite.
const hash = "a".repeat(64);
const identity: JsonObject = {
  schema_version: 1, protocol_version: 2, bridge_schema: 3, ruleset: "sijyu",
  content_profile: "declared-v2", bundle_id: hash, code_id: hash, cpp_hash: hash,
  card_registry_hash: hash, lua_hash: hash, bindings_abi: hash,
  packages: ["standard"], interaction_schemas: { "1": hash }
};
class Socket extends EventTarget {
  static OPEN = 1;
  static instances: Socket[] = [];
  readyState = 1;
  sent: ReturnType<typeof decodeMessage>[] = [];
  sequence = 0;
  constructor(_url: string) { super(); Socket.instances.push(this); }
  send(frame: string) { this.sent.push(decodeMessage(frame)); }
  close() { this.readyState = 3; }
  frame(command: number, payload: JsonObject, type: MessageType = "notification", replyTo?: string) {
    const id = String(++this.sequence);
    this.dispatchEvent(new MessageEvent("message", { data: encodeMessage({
      v: 2, type, source: command === Command.CHECK_VERSION ? "lobby" : "room", destination: "client",
      message_id: id, command, payload, ...(replyTo ? { reply_to: replyTo } : {})
    }) }));
    return id;
  }
}
const options = { wsUrl: "ws://localhost:9528", screenName: "web", avatar: "caocao", reconnect: false };
const settle = async () => { for (let i = 0; i < 8; ++i) await Promise.resolve(); };
async function connected() {
  vi.stubGlobal("WebSocket", Socket);
  const session = new LiveSession();
  session.setRulesProvider(async () => identity);
  session.connect(options); await settle();
  const socket = Socket.instances.at(-1)!;
  socket.frame(Command.CHECK_VERSION, { schema_version: 1, card_count: 0, rules_bundle: identity });
  await settle();
  socket.frame(Command.SIGNUP, { schema_version: 2, accepted: true, player_id: "sgs1", room_id: 1 },
    "reply", socket.sent[0].message_id);
  socket.frame(Command.SETUP, { schema_version: 1 });
  return { session, socket };
}
// A committed native projection, not a browser replay of the received frames.
function nativeView(game: JsonObject = {}): JsonObject {
  return { connection: {}, setup: {}, game, self_name: "sgs1", card_id_space: 0,
    player_names: ["sgs1", "sgs2"],
    players: [{ object_name: "sgs1", alive: true }, { object_name: "sgs2", alive: true }], cards: [] };
}
afterEach(() => { Socket.instances = []; vi.useRealTimers(); vi.unstubAllGlobals(); });

describe("session game completion", () => {
  it("publishes native battle text and filter metadata with the committed view", async () => {
    const { session, socket } = await connected();
    const events = [{ command: Command.LOG_SKILL, text: "Native formatted event",
      payload: { log_type: "#UseSkill", from_player: "sgs1", to_players: ["sgs2"] } }];
    let seen = "";
    const unsubscribe = session.onChange(() => {
      seen = session.state.presentationEvents[0]?.text ?? "";
    });
    socket.frame(Command.STATE_SYNC, { schema_version: 1, phase: "begin", sync_id: "log" });
    session.hydrateNativeView(nativeView(), "", events);
    expect(session.state.presentationEvents).toEqual([]);
    socket.frame(Command.STATE_SYNC, { schema_version: 1, phase: "end", sync_id: "log" });
    session.hydrateNativeView(nativeView(), "", events);
    expect(seen).toBe("Native formatted event");
    expect(session.state.presentationEvents).toEqual(events);
    events[0].payload.to_players.push("later");
    expect(session.state.presentationEvents[0].payload?.to_players).toEqual(["sgs2"]);
    unsubscribe();
    session.disconnect();
  });

  it("drains a final native result after close and refuses stale game actions", async () => {
    const { session, socket } = await connected();
    // Model a Worker that has observed the bytes but has not published its view.
    session.setFrameSink(() => {});
    const generation = session.generation;
    socket.frame(Command.GAME_START, { schema_version: 1, card_ids: [] });
    const requestId = socket.frame(Command.CHOOSE_PLAYER, { schema_version: 1, players: ["sgs2"] }, "request");
    const result = { schema_version: 1, standoff: false, winner_tokens: ["sgs2"], roles: ["lord", "renegade"] };
    socket.frame(Command.GAME_OVER, result);
    expect(session.phase).toBe("active");
    expect(session.state.gameValue("result")).toBeUndefined();
    const sent = socket.sent.length;
    socket.close(); socket.dispatchEvent(new Event("close"));
    expect(session.phase).toBe("active");
    expect(session.generation).toBe(generation);
    session.hydrateNativeView(nativeView({ started: true, game_over: true, result }), "");
    session.nativeStreamDrained();
    expect(session.phase).toBe("finished");
    expect(session.error).toBe("");
    expect(session.state.gameValue("result")).toEqual(result);
    expect(session.interaction).toBeNull();
    expect(() => session.sendReply(Command.CHOOSE_PLAYER, requestId, {})).toThrow();
    expect(() => session.surrender()).toThrow();
    expect(socket.sent).toHaveLength(sent);
    session.connect(options); await settle();
    expect(session.state.gameValue("result")).toBeUndefined();
    session.disconnect();
  });

  it("does not treat a disconnect before GAME_OVER as a completed game", async () => {
    const { session, socket } = await connected();
    socket.close(); socket.dispatchEvent(new Event("close"));
    expect(session.phase).toBe("failed");
    expect(session.error).toBe("連線已關閉");
    expect(session.state.gameValue("game_over")).toBeUndefined();
  });

  it("presents a reconnected result only after STATE_SYNC commits", async () => {
    const { session, socket } = await connected();
    session.hydrateNativeView(nativeView({ started: true }), "");
    const result = { schema_version: 1, standoff: true, winner_tokens: [], roles: [] };
    socket.frame(Command.STATE_SYNC, { schema_version: 1, phase: "begin", sync_id: "done" });
    socket.frame(Command.GAME_OVER, result);
    expect(session.phase).toBe("active");
    expect(session.state.gameValue("game_over")).toBeUndefined();
    expect(session.synchronizing).toBe(true);
    expect(session.state.playerNames).toEqual(["sgs1", "sgs2"]);
    session.hydrateNativeView(nativeView({ started: true, game_over: true, result }), "");
    expect(session.synchronizing).toBe(true);
    expect(session.state.gameValue("result")).toBeUndefined();
    socket.frame(Command.STATE_SYNC, { schema_version: 1, phase: "end", sync_id: "done" });
    expect(session.phase).toBe("active");
    expect(session.state.gameValue("game_over")).toBeUndefined();
    expect(session.synchronizing).toBe(true);
    session.hydrateNativeView(nativeView({ started: true, game_over: true, result }), "");
    expect(session.synchronizing).toBe(false);
    expect(session.phase).toBe("finished");
    expect(session.state.gameValue("result")).toEqual(result);
    session.disconnect();
  });
});

describe("native submission and countdown presentation", () => {
  const prompt = { schema_version: 1, players: ["sgs2"] };
  const focus = (socket: Socket, countdown: JsonObject, players = ["sgs1"], command: number = Command.MOVE_FOCUS) =>
    socket.frame(Command.MOVE_FOCUS, { schema_version: 1, player_names: players, countdown, command });
  const fakeClock = () => vi.useFakeTimers({ toFake: ["setTimeout", "clearTimeout", "performance"] });

  it("does not send transport when native rejects an intent", async () => {
    const { session, socket } = await connected();
    const id = socket.frame(Command.CHOOSE_PLAYER, prompt, "request");
    session.setNativeSubmitter(() => Promise.reject(new Error("native_rejected")));
    const sent = socket.sent.length;
    session.sendReply(Command.CHOOSE_PLAYER, id, { kind: "players", payload: { players: ["sgs2"] } });
    await settle();
    expect(socket.sent).toHaveLength(sent);
    expect(session.interaction?.messageId).toBe(id);
    session.disconnect();
  });

  it("does not send an async native reply after the interaction becomes stale", async () => {
    const { session, socket } = await connected();
    const id = socket.frame(Command.CHOOSE_PLAYER, prompt, "request");
    let resolve!: (wire: JsonObject) => void;
    session.setNativeSubmitter(() => new Promise<JsonObject>(done => { resolve = done; }));
    const sent = socket.sent.length;
    session.sendReply(Command.CHOOSE_PLAYER, id, { kind: "players", payload: { players: ["sgs2"] } });
    session.disconnect();
    resolve({ type: "reply", command: Command.CHOOSE_PLAYER, reply_to: id,
      has_payload: true, payload: { schema_version: 1, players: ["sgs2"] } });
    await settle();
    expect(socket.sent).toHaveLength(sent);
  });

  it("rejects duplicate native submissions while the first is pending", async () => {
    const { session, socket } = await connected();
    const id = socket.frame(Command.CHOOSE_PLAYER, prompt, "request");
    session.setNativeSubmitter(() => new Promise<JsonObject>(() => {}));
    session.sendReply(Command.CHOOSE_PLAYER, id, { kind: "players", payload: { players: ["sgs2"] } });
    expect(() => session.sendReply(Command.CHOOSE_PLAYER, id,
      { kind: "players", payload: { players: ["sgs2"] } })).toThrow("native_submission_pending");
    session.disconnect();
  });

  it("subtracts request arrival delay from the hint without locally expiring the request", async () => {
    fakeClock();
    const { session, socket } = await connected();
    focus(socket, { type: 1, current: 1000, maximum: 5000 });
    vi.advanceTimersByTime(1000);
    const id = socket.frame(Command.CHOOSE_PLAYER, prompt, "request");
    expect(session.remainingInteractionMs()).toBe(3000);
    vi.advanceTimersByTime(2999);
    expect(session.interaction?.messageId).toBe(id);
    vi.advanceTimersByTime(1);
    expect(session.interaction?.messageId).toBe(id);
    expect(session.remainingInteractionMs()).toBe(0);
    const sent = socket.sent.length;
    expect(() => session.sendReply(Command.CHOOSE_PLAYER, id, {})).toThrow("native_submitter_unavailable");
    expect(socket.sent).toHaveLength(sent);
    session.disconnect();
  });

  it("lets native validation reject expiry even when the browser clock jumps", async () => {
    fakeClock();
    const { session, socket } = await connected();
    focus(socket, { type: 1, current: 0, maximum: 5000 });
    const id = socket.frame(Command.CHOOSE_PLAYER, prompt, "request");
    const sent = socket.sent.length;
    const submit = vi.fn(() => Promise.reject(new Error("expired")));
    session.setNativeSubmitter(submit);
    const now = vi.spyOn(performance, "now").mockReturnValue(5001);
    expect(session.remainingInteractionMs()).toBe(0);
    session.sendReply(Command.CHOOSE_PLAYER, id, { kind: "players", payload: { players: ["sgs2"] } });
    await settle();
    expect(submit).toHaveBeenCalledOnce();
    expect(session.interactionError).toBe("expired");
    expect(session.interaction?.messageId).toBe(id);
    expect(socket.sent).toHaveLength(sent);
    now.mockRestore(); session.disconnect();
  });

  it("does not infer cancellation from focus; only the native request projection retires it", async () => {
    const { session, socket } = await connected();
    const id = socket.frame(Command.CHOOSE_PLAYER, prompt, "request");
    focus(socket, { type: 0 }, ["sgs1", "sgs2"]);
    expect(session.interaction?.messageId).toBe(id);
    focus(socket, { type: 0 }, ["sgs2"]);
    expect(session.interaction?.messageId).toBe(id);
    session.hydrateNativeView(nativeView({ focus: ["sgs2"] }), id);
    expect(session.interaction?.messageId).toBe(id);
    session.hydrateNativeView(nativeView({ focus: ["sgs2"] }), "");
    expect(session.interaction).toBeNull();
    expect(() => session.sendReply(Command.CHOOSE_PLAYER, id, {})).toThrow();
    session.disconnect();
  });

  const unlimitedCountdowns: JsonObject[] = [{ type: 0 }, { type: 2 }, { type: 1, current: 0, maximum: 0 }];
  it.each(unlimitedCountdowns)(
    "does not guess a deadline for no-limit or unresolved countdown %j", async countdown => {
      fakeClock();
      const { session, socket } = await connected();
      focus(socket, countdown);
      const id = socket.frame(Command.CHOOSE_PLAYER, prompt, "request");
      vi.advanceTimersByTime(60000);
      expect(session.interaction?.messageId).toBe(id);
      expect(session.remainingInteractionMs()).toBeNull();
      session.disconnect();
    }
  );

  it("does not reuse the previous focus timer for a replacement request", async () => {
    fakeClock();
    const { session, socket } = await connected();
    focus(socket, { type: 1, current: 0, maximum: 1000 });
    socket.frame(Command.CHOOSE_PLAYER, prompt, "request");
    vi.advanceTimersByTime(500);
    const replacement = socket.frame(Command.CHOOSE_PLAYER, prompt, "request");
    expect(session.remainingInteractionMs()).toBeNull();
    vi.advanceTimersByTime(1000);
    expect(session.interaction?.messageId).toBe(replacement);
    session.disconnect();
    vi.advanceTimersByTime(10000);
    expect(session.interaction).toBeNull();
  });

  it("ignores a countdown explicitly associated with another command", async () => {
    fakeClock();
    const { session, socket } = await connected();
    focus(socket, { type: 1, current: 0, maximum: 1000 }, ["sgs1"], Command.NULLIFICATION);
    const id = socket.frame(Command.CHOOSE_PLAYER, prompt, "request");
    vi.advanceTimersByTime(1000);
    expect(session.interaction?.messageId).toBe(id);
    expect(session.remainingInteractionMs()).toBeNull();
    session.disconnect();
  });

  it("keeps an already elapsed request visible until native state retires it", async () => {
    fakeClock();
    const { session, socket } = await connected();
    focus(socket, { type: 1, current: 5000, maximum: 5000 });
    socket.frame(Command.CHOOSE_PLAYER, prompt, "request");
    expect(session.interaction?.messageId).toBeDefined();
    expect(session.remainingInteractionMs()).toBe(0);
    session.disconnect();
  });

  it("clears countdown hints at snapshot begin and does not restore historical focus timers", async () => {
    fakeClock();
    const { session, socket } = await connected();
    focus(socket, { type: 1, current: 0, maximum: 1000 });
    socket.frame(Command.CHOOSE_PLAYER, prompt, "request");
    socket.frame(Command.STATE_SYNC, { schema_version: 1, phase: "begin", sync_id: "next" });
    focus(socket, { type: 1, current: 0, maximum: 1000 });
    socket.frame(Command.STATE_SYNC, { schema_version: 1, phase: "end", sync_id: "next" });
    expect(session.synchronizing).toBe(true);
    session.hydrateNativeView(nativeView({ focus: ["sgs1"] }), "");
    expect(session.synchronizing).toBe(false);
    const id = socket.frame(Command.CHOOSE_PLAYER, prompt, "request");
    vi.advanceTimersByTime(1000);
    expect(session.interaction?.messageId).toBe(id);
    expect(session.remainingInteractionMs()).toBeNull();
    session.disconnect();
  });
});
