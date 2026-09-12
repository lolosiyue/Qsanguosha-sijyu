import { afterEach, describe, expect, it, vi } from "vitest";
import { LiveSession } from "../src/session";
import { Command, decodeMessage, encodeMessage, type JsonObject, type MessageType } from "../src/protocol";

// The rules provider is isolated here; production identity verification has its own suite.
const hash = "a".repeat(64);
const identity: JsonObject = {
  schema_version: 1, protocol_version: 2, bridge_schema: 2, ruleset: "sijyu",
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
afterEach(() => { Socket.instances = []; vi.useRealTimers(); vi.unstubAllGlobals(); });

describe("session game completion", () => {
  it("keeps the authoritative result after normal close and refuses stale game actions", async () => {
    const { session, socket } = await connected();
    socket.frame(Command.GAME_START, { schema_version: 1, card_ids: [] });
    const requestId = socket.frame(Command.CHOOSE_PLAYER, { schema_version: 1, players: ["sgs2"] }, "request");
    const result = { schema_version: 1, standoff: false, winner_tokens: ["sgs2"], roles: ["lord", "renegade"] };
    socket.frame(Command.GAME_OVER, result);
    expect(session.phase).toBe("finished");
    expect(session.interaction).toBeNull();
    expect(session.state.gameValue("result")).toEqual(result);
    const sent = socket.sent.length;
    expect(() => session.sendReply(Command.CHOOSE_PLAYER, requestId, {})).toThrow();
    expect(() => session.surrender()).toThrow("對局已結束");
    socket.frame(Command.CHOOSE_PLAYER, { schema_version: 1, players: ["sgs2"] }, "request");
    expect(session.interaction).toBeNull();
    expect(socket.sent).toHaveLength(sent);
    socket.close(); socket.dispatchEvent(new Event("close"));
    expect(session.phase).toBe("finished");
    expect(session.error).toBe("");
    expect(session.state.gameValue("result")).toEqual(result);
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
    socket.frame(Command.STATE_SYNC, { schema_version: 1, phase: "begin", sync_id: "done" });
    socket.frame(Command.GAME_OVER, { schema_version: 1, standoff: true, winner_tokens: [], roles: [] });
    expect(session.phase).toBe("active");
    expect(session.state.gameValue("game_over")).toBeUndefined();
    socket.frame(Command.STATE_SYNC, { schema_version: 1, phase: "end", sync_id: "done" });
    expect(session.phase).toBe("finished");
    expect(session.state.gameValue("game_over")).toBe(true);
    session.disconnect();
  });
});

describe("interaction expiry", () => {
  const prompt = { schema_version: 1, players: ["sgs2"] };
  const focus = (socket: Socket, countdown: JsonObject, players = ["sgs1"], command: number = Command.MOVE_FOCUS) =>
    socket.frame(Command.MOVE_FOCUS, { schema_version: 1, player_names: players, countdown, command });
  const fakeClock = () => vi.useFakeTimers({ toFake: ["setTimeout", "clearTimeout", "performance"] });

  it("expires at the server focus deadline even when the request arrives later", async () => {
    fakeClock();
    const { session, socket } = await connected();
    focus(socket, { type: 1, current: 1000, maximum: 5000 });
    vi.advanceTimersByTime(1000);
    const id = socket.frame(Command.CHOOSE_PLAYER, prompt, "request");
    vi.advanceTimersByTime(2999);
    expect(session.interaction?.messageId).toBe(id);
    vi.advanceTimersByTime(1);
    expect(session.interaction).toBeNull();
    const sent = socket.sent.length;
    expect(() => session.sendReply(Command.CHOOSE_PLAYER, id, {})).toThrow();
    expect(socket.sent).toHaveLength(sent);
    session.disconnect();
  });

  it("rejects a late reply even if the browser has not dispatched its timer", async () => {
    fakeClock();
    const { session, socket } = await connected();
    focus(socket, { type: 1, current: 0, maximum: 5000 });
    const id = socket.frame(Command.CHOOSE_PLAYER, prompt, "request");
    const sent = socket.sent.length;
    const now = vi.spyOn(performance, "now").mockReturnValue(5001);
    expect(() => session.sendReply(Command.CHOOSE_PLAYER, id, {})).toThrow("詢問已逾時");
    expect(session.interaction).toBeNull();
    expect(socket.sent).toHaveLength(sent);
    now.mockRestore(); session.disconnect();
  });

  it("clears an old prompt when focus leaves self but preserves multi-player races", async () => {
    const { session, socket } = await connected();
    const id = socket.frame(Command.CHOOSE_PLAYER, prompt, "request");
    focus(socket, { type: 0 }, ["sgs1", "sgs2"]);
    expect(session.interaction?.messageId).toBe(id);
    focus(socket, { type: 0 }, ["sgs2"]);
    expect(session.interaction).toBeNull();
    expect(() => session.sendReply(Command.CHOOSE_PLAYER, id, {})).toThrow();
    session.disconnect();
  });

  it.each([{ type: 0 }, { type: 2 }, { type: 1, current: 0, maximum: 0 }])(
    "does not guess a deadline for no-limit or unresolved countdown %j", async countdown => {
      fakeClock();
      const { session, socket } = await connected();
      focus(socket, countdown);
      const id = socket.frame(Command.CHOOSE_PLAYER, prompt, "request");
      vi.advanceTimersByTime(60000);
      expect(session.interaction?.messageId).toBe(id);
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
    session.disconnect();
  });

  it("does not display a request whose specified countdown already elapsed", async () => {
    fakeClock();
    const { session, socket } = await connected();
    focus(socket, { type: 1, current: 5000, maximum: 5000 });
    socket.frame(Command.CHOOSE_PLAYER, prompt, "request");
    expect(session.interaction).toBeNull();
    session.disconnect();
  });

  it("clears timers at snapshot begin and does not restore them from historical focus", async () => {
    fakeClock();
    const { session, socket } = await connected();
    focus(socket, { type: 1, current: 0, maximum: 1000 });
    socket.frame(Command.CHOOSE_PLAYER, prompt, "request");
    socket.frame(Command.STATE_SYNC, { schema_version: 1, phase: "begin", sync_id: "next" });
    focus(socket, { type: 1, current: 0, maximum: 1000 });
    socket.frame(Command.STATE_SYNC, { schema_version: 1, phase: "end", sync_id: "next" });
    const id = socket.frame(Command.CHOOSE_PLAYER, prompt, "request");
    vi.advanceTimersByTime(1000);
    expect(session.interaction?.messageId).toBe(id);
    session.disconnect();
  });
});
