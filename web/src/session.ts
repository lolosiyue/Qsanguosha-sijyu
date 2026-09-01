import {
  asBool,
  asNumber,
  asString,
  Command,
  decodeMessage,
  encodeMessage,
  isObject,
  nextId,
  type JsonObject,
  type ProtocolMessage
} from "./protocol";
import { applyNotification } from "./reducer";
import { appendSynthesizedLogs } from "./log-text";
import { ClientGameState } from "./state";
import { replyCommand } from "./replies";

export type SessionPhase =
  | "idle"
  | "connecting"
  | "hello"
  | "signup"
  | "setup"
  | "active"
  | "failed";

export interface SessionOptions {
  wsUrl: string;
  screenName: string;
  avatar: string;
  reconnect: boolean;
  roomId?: number;
}

export interface ActiveInteraction {
  command: number;
  messageId: string;
  payload: JsonObject;
}

type Listener = () => void;

export class LiveSession {
  readonly state = new ClientGameState();
  phase: SessionPhase = "idle";
  error = "";
  interaction: ActiveInteraction | null = null;
  interactionError = "";
  private socket: WebSocket | null = null;
  private outgoing = { value: 0n };
  private lastIncoming = 0n;
  private signupId = "";
  private syncActive = false;
  private syncId = "";
  private pending: ClientGameState | null = null;
  private listeners = new Set<Listener>();
  private renPile: number[] = [];

  onChange(listener: Listener): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  private notify(): void {
    for (const listener of this.listeners)
      listener();
  }

  connect(options: SessionOptions): void {
    this.disconnect();
    this.outgoing = { value: 0n };
    this.lastIncoming = 0n;
    this.signupId = "";
    this.syncActive = false;
    this.syncId = "";
    this.pending = null;
    this.renPile = [];
    this.phase = "connecting";
    this.error = "";
    this.interaction = null;
    this.interactionError = "";
    this.state.reset();
    this.state.setConnectionValue("ws_url", options.wsUrl);
    this.state.setConnectionValue("screen_name", options.screenName);
    const socket = new WebSocket(options.wsUrl);
    this.socket = socket;
    socket.addEventListener("message", (event) => {
      if (this.socket !== socket)
        return;
      if (typeof event.data !== "string") {
        this.fail("binary frames are rejected");
        return;
      }
      try {
        this.handle(decodeMessage(event.data), options);
      } catch (error) {
        this.fail(error instanceof Error ? error.message : String(error));
      }
    });
    socket.addEventListener("error", () => {
      if (this.socket === socket)
        this.fail("WebSocket 連線失敗");
    });
    socket.addEventListener("close", () => {
      if (this.socket !== socket || this.phase === "failed")
        return;
      this.phase = "failed";
      this.error = this.error || "連線已關閉";
      this.notify();
    });
    this.notify();
  }

  disconnect(): void {
    this.socket?.close();
    this.socket = null;
    this.syncActive = false;
    this.pending = null;
  }

  sendControl(command: number, payload: JsonObject): void {
    this.send({
      v: 2,
      type: "notification",
      source: "client",
      destination: "room",
      message_id: nextId(this.outgoing),
      command,
      payload
    });
  }

  sendReply(command: number, replyTo: string, payload: JsonObject): void {
    this.send({
      v: 2,
      type: "reply",
      source: "client",
      destination: "room",
      message_id: nextId(this.outgoing),
      reply_to: replyTo,
      command: replyCommand(command),
      payload
    });
    this.interaction = null;
    this.interactionError = "";
    this.notify();
  }

  setReady(ready: boolean): void {
    this.sendControl(Command.READY, { schema_version: 1, ready });
  }

  addRobots(): void {
    this.sendControl(Command.ADD_ROBOT, {
      schema_version: 1,
      fill_remaining: true,
      count: 0
    });
  }

  chat(text: string): void {
    this.sendControl(Command.SPEAK, { schema_version: 1, text });
  }

  trust(trusted: boolean): void {
    this.sendControl(Command.TRUST, { schema_version: 1, trusted });
  }

  surrender(): void {
    this.sendControl(Command.SURRENDER, { schema_version: 1, requested: true });
  }

  private send(message: ProtocolMessage): void {
    if (!this.socket || this.socket.readyState !== WebSocket.OPEN)
      throw new Error("WebSocket is not open");
    this.socket.send(encodeMessage(message));
  }

  private fail(detail: string): void {
    this.phase = "failed";
    this.error = detail;
    this.disconnect();
    this.notify();
  }

  private handle(message: ProtocolMessage, options: SessionOptions): void {
    const incoming = BigInt(message.message_id);
    if (incoming <= this.lastIncoming)
      throw new Error("message_id must increase");

    if (this.phase === "connecting" || this.phase === "hello") {
      if (message.command !== Command.CHECK_VERSION
          || message.type !== "notification"
          || message.source !== "lobby")
        throw new Error("first frame must be SERVER_HELLO");
      this.lastIncoming = incoming;
      this.phase = "hello";
      this.state.setCardIdSpace(asNumber(message.payload.card_count));
      const signup: JsonObject = {
        schema_version: 2,
        reconnect_requested: options.reconnect,
        screen_name: options.screenName,
        avatar: options.avatar
      };
      if (options.roomId !== undefined)
        signup.room_id = options.roomId;
      this.signupId = nextId(this.outgoing);
      this.send({
        v: 2,
        type: "request",
        source: "client",
        destination: "lobby",
        message_id: this.signupId,
        command: Command.SIGNUP,
        payload: signup
      });
      this.phase = "signup";
      this.notify();
      return;
    }

    if (this.phase === "signup") {
      if (message.command !== Command.SIGNUP || message.type !== "reply"
          || message.reply_to !== this.signupId)
        throw new Error("expected correlated SIGNUP reply");
      this.lastIncoming = incoming;
      if (!asBool(message.payload.accepted)) {
        throw new Error(asString(message.payload.message, asString(message.payload.error_code)));
      }
      this.state.setSelfName(asString(message.payload.player_id));
      this.state.setConnectionValue("reconnected", asBool(message.payload.reconnected));
      this.state.setConnectionValue("room_id", asNumber(message.payload.room_id));
      this.phase = "setup";
      this.notify();
      return;
    }

    if (this.phase === "setup") {
      if (message.command !== Command.SETUP || message.type !== "notification")
        throw new Error("expected SETUP");
      this.lastIncoming = incoming;
      this.state.setup = message.payload;
      this.send({
        v: 2,
        type: "notification",
        source: "client",
        destination: "room",
        message_id: nextId(this.outgoing),
        command: Command.READY,
        payload: { schema_version: 1, ready: true }
      });
      this.phase = "active";
      this.notify();
      return;
    }

    this.lastIncoming = incoming;
    // Command results (e.g. delay-test ack) share the command id but carry
    // CommandResultPayload, not NetworkDelayPayload. Echo only the probe.
    if (message.type === "reply") {
      this.notify();
      return;
    }

    if (message.command === Command.NETWORK_DELAY_TEST
        && message.type === "notification"
        && isObject(message.payload)) {
      const nonce = asString(message.payload.nonce)
        || (typeof message.payload.nonce === "number" ? String(message.payload.nonce) : "");
      if (!nonce)
        throw new Error("NETWORK_DELAY_TEST nonce is missing");
      this.send({
        v: 2,
        type: "request",
        source: "client",
        destination: "room",
        message_id: nextId(this.outgoing),
        command: Command.NETWORK_DELAY_TEST,
        payload: { schema_version: 1, nonce }
      });
    }

    if (message.type === "request" && message.destination === "client") {
      this.interaction = {
        command: message.command,
        messageId: message.message_id,
        payload: message.payload
      };
      this.interactionError = "";
      this.notify();
      return;
    }

    let target = this.state;
    if (message.command === Command.STATE_SYNC) {
      const phase = asString(message.payload.phase);
      const syncId = asString(message.payload.sync_id);
      if (phase === "begin") {
        if (this.syncActive)
          throw new Error("state snapshot already active");
        this.pending = this.state.clone();
        this.pending.resetGameplayState();
        this.syncActive = true;
        this.syncId = syncId;
        this.renPile = [];
        target = this.pending;
      } else if (!this.syncActive || syncId !== this.syncId) {
        throw new Error("STATE_SYNC end does not match begin");
      } else {
        target = this.pending ?? this.state;
      }
    } else if (this.syncActive && this.pending) {
      target = this.pending;
    }

    if (message.type === "notification") {
      if (message.command === Command.GAME_START)
        this.renPile = [];
      const reduction = applyNotification(target, message.command, message.payload);
      if (!reduction.success)
        throw new Error(reduction.detail);
      appendSynthesizedLogs(target, message.command, message.payload, this.renPile);
    }

    if (message.command === Command.STATE_SYNC && asString(message.payload.phase) === "end") {
      if (this.pending) {
        const connection = this.state.connection;
        this.state.reset();
        Object.assign(this.state, this.pending);
        this.state.connection = { ...connection, ...this.pending.connection };
      }
      this.syncActive = false;
      this.syncId = "";
      this.pending = null;
    }
    this.notify();
  }
}

export function defaultWsUrl(search = window.location.search): string {
  const query = new URLSearchParams(search).get("ws");
  if (query)
    return query;
  const host = window.location.hostname || "127.0.0.1";
  return `ws://${host}:9528`;
}

export function parseRoute(pathname = window.location.pathname): {
  roomId?: number;
  reconnect: boolean;
} {
  const match = pathname.match(/\/room\/(\d+)\/?$/);
  const reconnect = new URLSearchParams(window.location.search).get("reconnect") === "1";
  if (!match)
    return { reconnect };
  return { roomId: Number(match[1]), reconnect };
}

export function roomShareUrl(roomId: number): string {
  return `${window.location.origin}/room/${roomId}`;
}
