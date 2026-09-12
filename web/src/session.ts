import {
  asBool,
  asNumber,
  asString,
  asStringList,
  Command,
  decodeMessage,
  encodeMessage,
  isObject,
  nextId,
  type JsonObject,
  type ProtocolMessage
} from "./protocol";
import { isRulesIdentity, rulesCompatibilityError, rulesErrorMessage } from "./rules-identity";
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
  | "finished"
  | "failed";

export interface SessionOptions {
  wsUrl: string;
  screenName: string;
  avatar: string;
  reconnect: boolean;
  roomId?: number;
  local?: boolean;
  transportFactory?: () => SessionTransport;
}

// Both transports carry the same Protocol V2 frames and native rules ingress.
export interface SessionTransport {
  readonly readyState: number;
  send(frame: string): void;
  close(): void;
  addEventListener(type: "message", listener: (event: MessageEvent) => void): void;
  addEventListener(type: "error" | "close", listener: () => void): void;
}

export interface ActiveInteraction {
  command: number;
  messageId: string;
  payload: JsonObject;
}

type Listener = () => void;

// The native rules runtime observes exact transport bytes in arrival order.
export type FrameSink = (generation: number, outgoing: boolean, frame: string) => void;

export class LiveSession {
  readonly state = new ClientGameState();
  generation = 0;
  phase: SessionPhase = "idle";
  error = "";
  interaction: ActiveInteraction | null = null;
  interactionError = "";
  private socket: SessionTransport | null = null;
  private local = false;
  private outgoing = { value: 0n };
  private lastIncoming = 0n;
  private signupId = "";
  private syncActive = false;
  private syncId = "";
  private pending: ClientGameState | null = null;
  private listeners = new Set<Listener>();
  private renPile: number[] = [];
  private rulesBundle: JsonObject | null = null;
  private rulesProvider: ((session: LiveSession, hello?: JsonObject) => Promise<JsonObject | null>) | null = null;
  private frameSink: FrameSink | null = null;
  private focusDeadline: number | null = null;
  private focusCommand = Command.MOVE_FOCUS as number;
  private interactionDeadline: number | null = null;
  private interactionTimer: ReturnType<typeof setTimeout> | undefined;

  private clearInteraction(): void {
    if (this.interactionTimer !== undefined)
      clearTimeout(this.interactionTimer);
    this.interactionTimer = undefined;
    this.interactionDeadline = null;
    this.interaction = null;
    this.interactionError = "";
  }

  private armInteractionExpiry(): void {
    if (!this.interaction || this.interactionDeadline === null)
      return;
    const generation = this.generation;
    const requestId = this.interaction.messageId;
    const remaining = this.interactionDeadline - performance.now();
    if (remaining <= 0) {
      this.clearInteraction();
      return;
    }
    this.interactionTimer = setTimeout(() => {
      if (generation !== this.generation || this.interaction?.messageId !== requestId)
        return;
      this.interactionTimer = undefined;
      this.armInteractionExpiry();
      this.notify();
    }, Math.min(remaining, 2147483647));
  }

  setFrameSink(sink: FrameSink | null): void {
    this.frameSink = sink;
  }

  setRulesProvider(provider: (session: LiveSession, hello?: JsonObject) => Promise<JsonObject | null>): void {
    this.rulesProvider = provider;
  }

  get synchronizing(): boolean { return this.syncActive; }
  get isLocal(): boolean { return this.local; }

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
    this.local = options.local === true;
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
    this.rulesBundle = null;
    this.notify();
    const generation = this.generation;
    // Prepare code and optionally warm the last successful content before connecting.
    void Promise.resolve().then(() => {
      if (generation !== this.generation) return null;
      if (!this.rulesProvider) throw new Error("rules_reload_required");
      return this.rulesProvider(this);
    }).then(identity => {
      if (generation !== this.generation) return;
      if (identity !== null && !isRulesIdentity(identity)) throw new Error("rules_identity_invalid");
      this.rulesBundle = identity;
      this.openSocket(options);
    }).catch(error => {
      if (generation === this.generation)
        this.fail(error instanceof Error ? error.message : String(error));
    });
  }

  private openSocket(options: SessionOptions): void {
    const socket: SessionTransport = options.transportFactory?.() ?? new WebSocket(options.wsUrl);
    this.socket = socket;
    socket.addEventListener("message", (event) => {
      if (this.socket !== socket)
        return;
      if (typeof event.data !== "string") {
        this.fail("binary frames are rejected");
        return;
      }
      // Deliver the exact bytes before this reducer forms any opinion of them.
      this.frameSink?.(this.generation, false, event.data);
      try {
        this.handle(decodeMessage(event.data), options);
      } catch (error) {
        this.fail(error instanceof Error ? error.message : String(error));
      }
    });
    socket.addEventListener("error", () => {
      if (this.socket === socket && this.phase === "finished") {
        this.disconnect();
        this.notify();
      } else if (this.socket === socket)
        this.fail(this.local ? "單機對局執行失敗" : "WebSocket 連線失敗");
    });
    socket.addEventListener("close", () => {
      if (this.socket !== socket || this.phase === "failed")
        return;
      // A completed game remains readable after the server closes its socket.
      if (this.phase === "finished") {
        this.disconnect();
        this.notify();
        return;
      }
      this.phase = "failed";
      this.error = this.error || "連線已關閉";
      this.disconnect();
      this.notify();
    });
    this.notify();
  }

  disconnect(): void {
    const socket = this.socket;
    this.socket = null;
    socket?.close();
    this.syncActive = false;
    this.pending = null;
    ++this.generation;
    this.focusDeadline = null;
    this.clearInteraction();
  }

  sendControl(command: number, payload: JsonObject): void {
    if (this.phase === "finished")
      throw new Error("對局已結束");
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
    // DOM handlers and Worker previews can outlive a request or STATE_SYNC.
    if (this.phase !== "active" || this.syncActive || !this.interaction
        || this.interaction.command !== command || this.interaction.messageId !== replyTo)
      throw new Error("詢問已更新，請重新選擇");
    // Timers may be throttled in background tabs; never send after the deadline.
    if (this.interactionDeadline !== null && performance.now() >= this.interactionDeadline) {
      this.clearInteraction();
      this.notify();
      throw new Error("詢問已逾時");
    }
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
    this.clearInteraction();
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
    const frame = encodeMessage(message);
    this.socket.send(frame);
    this.frameSink?.(this.generation, true, frame);
  }

  private fail(detail: string): void {
    // Keep the wire/runtime reason available when the UI groups compatibility errors.
    console.warn("Web session failed:", detail);
    this.phase = "failed";
    this.error = rulesErrorMessage(detail);
    this.disconnect();
    this.notify();
  }

  private handle(message: ProtocolMessage, options: SessionOptions): void {
    const incoming = BigInt(message.message_id);
    if (incoming <= this.lastIncoming)
      throw new Error("message_id must increase");

    if (message.command === Command.WARN && message.type === "notification")
      throw new Error(asString(message.payload.code) || asString(message.payload.message));

    if (this.phase === "connecting") {
      if (message.command !== Command.CHECK_VERSION
          || message.type !== "notification"
          || message.source !== "lobby")
        throw new Error("first frame must be SERVER_HELLO");
      this.lastIncoming = incoming;
      this.phase = "hello";
      this.state.setCardIdSpace(asNumber(message.payload.card_count));
      const generation = this.generation;
      void Promise.resolve().then(() => {
        if (generation !== this.generation || this.phase !== "hello") return null;
        if (!this.rulesProvider) throw new Error("rules_reload_required");
        return this.rulesProvider(this, message.payload);
      }).then(identity => {
        if (generation !== this.generation || this.phase !== "hello") return;
        const compatibility = rulesCompatibilityError(message.payload.rules_bundle, identity);
        if (compatibility) throw new Error(compatibility);
        this.rulesBundle = identity;
        const signup: JsonObject = {
          schema_version: 2,
          reconnect_requested: options.reconnect,
          screen_name: options.screenName,
          avatar: options.avatar,
          rules_bundle: identity
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
      }).catch(error => {
        if (generation === this.generation)
          this.fail(error instanceof Error ? error.message : String(error));
      });
      return;
    }

    if (this.phase === "hello")
      throw new Error("unexpected frame before rules negotiation completed");

    if (this.phase === "signup") {
      if (message.command !== Command.SIGNUP || message.type !== "reply"
          || message.reply_to !== this.signupId)
        throw new Error("expected correlated SIGNUP reply");
      this.lastIncoming = incoming;
      if (!asBool(message.payload.accepted)) {
        throw new Error(asString(message.payload.error_code) || asString(message.payload.message));
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
      if (this.phase === "finished")
        return;
      this.clearInteraction();
      this.interaction = {
        command: message.command,
        messageId: message.message_id,
        payload: message.payload
      };
      // MOVE_FOCUS precedes the request; consume its deadline only once.
      this.interactionDeadline = this.focusCommand === Command.MOVE_FOCUS || this.focusCommand === message.command
        ? this.focusDeadline : null;
      this.focusDeadline = null;
      this.armInteractionExpiry();
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
        this.focusDeadline = null;
        this.clearInteraction();
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
      if (message.command === Command.GAME_OVER)
        this.clearInteraction();
      if (message.command === Command.GAME_START)
        this.renPile = [];
      const reduction = applyNotification(target, message.command, message.payload);
      if (!reduction.success)
        throw new Error(reduction.detail);
      if (message.command === Command.MOVE_FOCUS && !this.syncActive) {
        this.focusDeadline = null;
        this.focusCommand = asNumber(message.payload.command, Command.MOVE_FOCUS);
        if (!asStringList(target.gameValue("focus")).includes(this.state.selfName)) {
          this.clearInteraction();
        } else {
          const countdown = message.payload.countdown;
          // Countdown::USE_SPECIFIED carries milliseconds; zero maximum is unlimited.
          // NO_LIMIT and unresolved USE_DEFAULT must not invent a local timeout.
          if (isObject(countdown) && countdown.type === 1
              && Number.isSafeInteger(countdown.maximum) && Number(countdown.maximum) > 0
              && Number.isSafeInteger(countdown.current) && Number(countdown.current) >= 0)
            this.focusDeadline = performance.now()
              + Math.max(0, Number(countdown.maximum) - Number(countdown.current));
        }
      }
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
    // Wait for an atomic snapshot commit before presenting a restored result.
    if (!this.syncActive && asBool(this.state.gameValue("game_over"))) {
      this.focusDeadline = null;
      this.clearInteraction();
      this.phase = "finished";
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
