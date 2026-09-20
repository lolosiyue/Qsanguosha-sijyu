import {
  asBool,
  asNumber,
  asString,
  asStringList,
  Command,
  SELF_REFERENCE,
  decodeMessage,
  encodeMessage,
  isObject,
  nextId,
  type JsonObject,
  type ProtocolMessage
} from "./protocol";
import { isRulesIdentity, rulesCompatibilityError, rulesErrorMessage } from "./rules-identity";
import { ClientGameState, type PresentationEvent } from "./state";
import { autoTableBgUrl, imagePathToUrl, isLightbox, lightboxBackgroundUrl } from "./backdrop";

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
  private listeners = new Set<Listener>();
  private transportClosed = false;
  private nativeCaughtUp = true;
  private sending = false;
  private drainingTransport = false;
  private deferredTransport: (() => void)[] = [];
  private submitting: ActiveInteraction | null = null;
  private tableBackground: string | null = null;
  private rulesBundle: JsonObject | null = null;
  private rulesProvider: ((session: LiveSession, hello?: JsonObject) => Promise<JsonObject | null>) | null = null;
  private frameSink: FrameSink | null = null;
  private focusDeadline: number | null = null;
  private focusCommand = Command.MOVE_FOCUS as number;
  private interactionDeadline: number | null = null;
  private nativeSubmitter: ((requestId: string, intent: JsonObject) => Promise<JsonObject>) | null = null;

  /** Display hint only; the native ClientCore owns request expiry. */
  remainingInteractionMs(): number | null {
    return this.interaction && this.interactionDeadline !== null
      ? Math.max(0, this.interactionDeadline - performance.now()) : null;
  }

  private clearInteraction(): void {
    this.interactionDeadline = null;
    this.interaction = null;
    this.interactionError = "";
  }

  setFrameSink(sink: FrameSink | null): void {
    this.frameSink = sink;
  }

  setRulesProvider(provider: (session: LiveSession, hello?: JsonObject) => Promise<JsonObject | null>): void {
    this.rulesProvider = provider;
  }

  setNativeSubmitter(submitter: ((requestId: string, intent: JsonObject) => Promise<JsonObject>) | null): void {
    this.nativeSubmitter = submitter;
  }

  hydrateNativeView(view: JsonObject, requestId?: string, events?: readonly PresentationEvent[]): void {
    // The controller only publishes a snapshot after all observed frames have
    // drained. A raw end marker never commits a second, browser-owned snapshot.
    if (this.syncId) return;
    const wsUrl = this.state.connectionValue("ws_url");
    this.state.hydrateNativeView(view);
    // Publish native text together with its committed view before notifying
    // either the ordinary battle log or the accessible snapshot presenter.
    if (events !== undefined) this.state.presentationEvents = structuredClone([...events]);
    if (wsUrl !== undefined) this.state.setConnectionValue("ws_url", wsUrl);
    this.state.setGameValue("table_bg", this.tableBackground ?? autoTableBgUrl(this.state));
    this.state.setGameValue("table_bg_locked", this.tableBackground !== null);
    this.syncActive = false;
    if (requestId !== undefined && this.interaction?.messageId !== requestId)
      this.clearInteraction();
    if (asBool(this.state.gameValue("game_over"))) {
      this.focusDeadline = null;
      this.clearInteraction();
      this.phase = "finished";
      this.error = "";
    }
    this.notify();
  }

  nativeStreamDrained(): void {
    this.nativeCaughtUp = true;
    if (this.transportClosed && this.phase !== "finished" && this.phase !== "failed") {
      this.phase = "failed";
      this.error = this.error || "連線已關閉";
      this.clearInteraction();
      this.notify();
    }
  }

  nativeStreamFailed(detail: string): void {
    if (this.phase !== "finished") this.fail(detail);
  }

  private observeFrame(outgoing: boolean, frame: string): void {
    this.nativeCaughtUp = this.frameSink === null;
    this.frameSink?.(this.generation, outgoing, frame);
  }

  private deliverTransport(deliver: () => void): void {
    // Local transports can synchronously answer send(). Observe the successful
    // outgoing frame first, then let those replies enter the same native queue.
    this.deferredTransport.push(deliver);
    this.flushTransport();
  }

  private flushTransport(): void {
    if (this.sending || this.drainingTransport) return;
    this.drainingTransport = true;
    try {
      // A handler can send again. Append its synchronous replies behind all
      // already-received events rather than recursively reordering the queue.
      while (this.deferredTransport.length) this.deferredTransport.shift()!();
    } finally {
      this.drainingTransport = false;
    }
  }

  private transportEnded(socket: SessionTransport, detail: string): void {
    if (this.socket !== socket) return;
    this.socket = null;
    this.transportClosed = true;
    if (this.phase !== "finished") this.error = this.error || detail;
    // Do not advance generation: the last GAME_OVER may still be in the Worker.
    if (this.nativeCaughtUp) this.nativeStreamDrained();
    this.notify();
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
    this.transportClosed = false;
    this.nativeCaughtUp = true;
    this.tableBackground = null;
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
    socket.addEventListener("message", (event) => this.deliverTransport(() => {
      if (this.socket !== socket)
        return;
      if (typeof event.data !== "string") {
        this.fail("binary frames are rejected");
        return;
      }
      // Deliver the exact bytes before this reducer forms any opinion of them.
      this.observeFrame(false, event.data);
      try {
        this.handle(decodeMessage(event.data), options);
      } catch (error) {
        this.fail(error instanceof Error ? error.message : String(error));
      }
    }));
    socket.addEventListener("error", () => this.deliverTransport(() => {
      this.transportEnded(socket, this.local ? "單機對局執行失敗" : "WebSocket 連線失敗");
      socket.close();
    }));
    socket.addEventListener("close", () => this.deliverTransport(() => {
      this.transportEnded(socket, "連線已關閉");
    }));
    this.notify();
  }

  disconnect(): void {
    const socket = this.socket;
    this.socket = null;
    socket?.close();
    this.syncActive = false;
    this.syncId = "";
    this.transportClosed = false;
    this.nativeCaughtUp = true;
    this.deferredTransport = [];
    this.submitting = null;
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
    if (this.submitting) throw new Error("native_submission_pending");
    if (this.nativeSubmitter) {
      const generation = this.generation;
      const intent = payload;
      const interaction = this.interaction;
      this.submitting = interaction;
      void this.nativeSubmitter(replyTo, intent).then(wire => {
        if (generation !== this.generation || this.interaction !== interaction
            || this.phase !== "active" || this.syncActive)
          throw new Error("詢問已更新，請重新選擇");
        if (!wire || wire.type !== "reply" || !Number.isSafeInteger(wire.command)
            || wire.reply_to !== replyTo || wire.has_payload !== true
            || !isObject(wire.payload)) throw new Error("native_reply_invalid");
        this.send({ v: 2, type: "reply", source: "client", destination: "room",
          message_id: nextId(this.outgoing), reply_to: wire.reply_to,
          command: wire.command as number, payload: wire.payload });
        if (this.interaction === interaction) this.clearInteraction();
        this.notify();
      }).catch(error => {
        if (generation === this.generation && this.interaction === interaction) {
          this.interactionError = error instanceof Error ? error.message : String(error);
          this.notify();
        }
      }).finally(() => {
        if (this.submitting === interaction) this.submitting = null;
      });
      return;
    }
    throw new Error("native_submitter_unavailable");
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
    this.sending = true;
    try {
      this.socket.send(frame);
      this.observeFrame(true, frame);
    } finally {
      this.sending = false;
      this.flushTransport();
    }
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
        this.phase = "signup";
        this.send({
          v: 2,
          type: "request",
          source: "client",
          destination: "lobby",
          message_id: this.signupId,
          command: Command.SIGNUP,
          payload: signup
        });
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
      this.phase = "active";
      this.send({
        v: 2,
        type: "notification",
        source: "client",
        destination: "room",
        message_id: nextId(this.outgoing),
        command: Command.READY,
        payload: { schema_version: 1, ready: true }
      });
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
      this.notify();
      return;
    }

    if (message.command === Command.STATE_SYNC) {
      const phase = asString(message.payload.phase);
      const syncId = asString(message.payload.sync_id);
      if (phase === "begin") {
        if (this.syncId)
          throw new Error("state snapshot already active");
        this.syncActive = true;
        this.focusDeadline = null;
        this.clearInteraction();
        this.syncId = syncId;
        this.tableBackground = null;
      } else if (phase !== "end" || !this.syncId || syncId !== this.syncId) {
        throw new Error("STATE_SYNC end does not match begin");
      } else {
        // Keep synchronizing=true until the native committed view arrives.
        this.syncId = "";
      }
    }

    if (message.type === "notification") {
      if (message.command === Command.GAME_START)
        this.tableBackground = null;
      if (message.command === Command.CHANGE_TABLE_BG) {
        const path = imagePathToUrl(asString(message.payload.path));
        if (path) this.tableBackground = path;
      }
      if (message.command === Command.ANIMATE && isLightbox(asNumber(message.payload.animation))) {
        const path = lightboxBackgroundUrl(asString(message.payload.first_argument));
        if (path !== undefined) this.tableBackground = path;
      }
      // Native ingress is the sole gameplay reducer. The shell only keeps
      // transport focus/deadline state until its native projection arrives.
      if (message.command === Command.MOVE_FOCUS && !this.syncActive) {
        this.focusDeadline = null;
        this.focusCommand = asNumber(message.payload.command, Command.MOVE_FOCUS);
        if (asStringList(message.payload.player_names)
            .some(name => name === this.state.selfName || name === SELF_REFERENCE)) {
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
