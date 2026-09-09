import { RULES_BRIDGE_SCHEMA, isRulesIdentity, rulesErrorMessage } from "./rules-identity";
import { cardRecord, installRulesCardCatalog } from "./i18n";
import { Command, asNumber, asString, isObject, type JsonObject } from "./protocol";
import { INTERACTION_COMMANDS } from "./replies";
import type { LiveSession } from "./session";

export interface RulesSelection {
  card_ids: number[];
  targets: string[];
  skill_name: string;
  skill_instance_id: number;
  user_string: string;
  // Rearrangement prompts answer with two ordered lists instead of one set.
  top: number[];
  bottom: number[];
}

// Native detail for one ViewAs candidate. Only `available` decides activation;
// the rest explains a disabled button and sizes the subcard step.
export interface RulesSkill {
  name: string;
  instance_id: number;
  available: boolean;
  status: string;
  v2: boolean;
  subcard_min: number;
  subcard_max: number;
  usage_scope: string;
  usage_used: number;
  invalid: boolean;
  response_or_use: boolean;
  expand_pile: string;
}

export interface RulesEvaluation {
  schema_version: number;
  generation: number;
  revision: number;
  request_id: string;
  known: boolean;
  reason: string;
  can_confirm: boolean;
  card_text: string;
  selectable_cards: number[];
  // Zone of each selectable/selected card: hand, equip, hand_pile, expand_pile
  // or sibling_pile. A shell groups by this instead of guessing from ownership.
  card_zones: Record<string, string>;
  selection_min: number;
  selection_max: number;
  // The shared ClientCore InteractionRequest this answer belongs to.
  interaction: JsonObject;
  skills: RulesSkill[];
  declarations: string[];
  // SkillDialogInfo for the selected skill: guhuo, juguan, tiansuan or empty.
  // The shell implements the shape, never a per-general branch.
  declaration_dialog: JsonObject;
  next_targets: { candidates: string[]; max_votes: Record<string, number> };
  player_metrics?: JsonObject;
  wire: { command: number; reply_to: string; payload: JsonObject } | null;
}

// Prompts whose whole answer is native: a set the Room already sent, its
// count contract and the canonical reply. No ViewAs card is built for these.
const ENUMERATED_COMMANDS: readonly number[] = [
  Command.SKILL_GUANXING, Command.SKILL_GONGXIN, Command.SKILL_YIJI
];

const NATIVE_COMMANDS: readonly number[] = [
  Command.PLAY_CARD, Command.RESPONSE_CARD, Command.ASK_PEACH, Command.NULLIFICATION,
  ...ENUMERATED_COMMANDS
];

export function isEnumeratedCommand(command: number): boolean {
  return ENUMERATED_COMMANDS.includes(command);
}

// The committed native view of the connection. The browser never computes it.
interface NativeStatus {
  generation: number;
  revision: number;
  requestId: string;
  active: boolean;
  synchronizing: boolean;
  failed: boolean;
}

interface Query {
  key: string;
  revision: number;
  requestId: string;
}

const UNBOUND: NativeStatus = { generation: -1, revision: -1, requestId: "",
  active: false, synchronizing: false, failed: false };
// Frames are small and already validated by the native decoder one at a time.
const FRAME_BATCH = 64;
const FRAME_BACKLOG = 4096;

// One Engine per connection, fed the exact transport frames in arrival order.
// Native state is the only rules state: a query carries a selection plus the
// correlation the runtime itself reported, never a browser-composed snapshot.
export class RulesController {
  result: RulesEvaluation | null = null;
  status = "idle";
  error = "";
  private worker: Worker | null = null;
  private generation = -1;
  private ready = false;
  private registryCount = 0;
  private frames: Record<string, unknown>[] = [];
  private desired: { requestId: string; selection: RulesSelection } | null = null;
  private desiredKey = "";
  private rejectedKey = "";
  private native: NativeStatus = UNBOUND;
  private inFlight: { query: Query | null; id: number } | null = null;
  private resultKey = "";
  private sequence = 0;
  private timer: ReturnType<typeof setTimeout> | undefined;
  private session: LiveSession | null = null;
  private disposed = false;
  private identity: JsonObject | null = null;
  private identityWait: { resolve(value: JsonObject): void; reject(error: Error): void } | null = null;

  initialize(session: LiveSession): Promise<JsonObject> {
    this.releaseWorker();
    this.session = session;
    this.generation = session.generation;
    this.error = "";
    if (this.disposed) return Promise.reject(new Error("rules_reload_required"));
    // The socket opens only after this resolves, so no frame can be missed.
    session.setFrameSink((generation, outgoing, frame) => this.observe(generation, outgoing, frame));
    return new Promise((resolve, reject) => {
      this.identityWait = { resolve, reject };
      this.startWorker();
    });
  }

  constructor(private readonly onChange: () => void) {}

  supports(command: number): boolean {
    return NATIVE_COMMANDS.includes(command);
  }

  // True while the shell should read its set and counts from the native
  // structured request rather than from raw wire payload fields.
  enumerated(command: number): boolean {
    return isEnumeratedCommand(command);
  }

  // Correlation is the runtime's own generation/revision/request, not the
  // reducer's: the two count different events and the Worker trails the socket.
  private key(revision: number, requestId: string, selection: RulesSelection): string {
    return JSON.stringify([this.generation, revision, requestId, selection]);
  }

  private idle(): boolean {
    return this.frames.length === 0 && this.inFlight === null;
  }

  current(session: LiveSession, selection: RulesSelection): boolean {
    return !this.disposed && this.result !== null && this.idle()
      && session.phase === "active" && !session.synchronizing
      && this.generation === session.generation
      && this.native.requestId === (session.interaction?.messageId ?? "")
      && this.resultKey === this.key(this.native.revision, this.native.requestId, selection);
  }

  // Exact bytes, in transport order, including every frame this client sent.
  private observe(generation: number, outgoing: boolean, frame: string): void {
    if (this.disposed || generation !== this.generation || !this.worker
        || this.status === "failed")
      return;
    if (this.frames.length >= FRAME_BACKLOG) {
      this.fail("規則串流積壓過多；請重新連線");
      return;
    }
    this.frames.push({ schema_version: 1, action: "frame", generation,
      direction: outgoing ? "outgoing" : "incoming", frame });
    this.pump();
  }

  update(session: LiveSession, selection: RulesSelection): void {
    if (this.disposed)
      return;
    this.session = session;
    if (this.generation !== session.generation) {
      this.releaseWorker();
      this.generation = session.generation;
      this.status = "idle";
      this.error = "";
    }
    const interaction = session.interaction;
    if (session.phase !== "active" || session.synchronizing || !interaction
        || !this.supports(interaction.command)) {
      this.desired = null;
      this.desiredKey = "";
      return;
    }
    const key = JSON.stringify([interaction.messageId, selection]);
    if (key === this.desiredKey)
      return;
    this.desired = { requestId: interaction.messageId, selection };
    this.desiredKey = key;
    // Failures stay visible until a new connection; a fresh Engine cannot be
    // given the frames this one already consumed.
    if (this.status === "failed")
      return;
    this.pump();
  }

  private startWorker(): void {
    this.status = "loading";
    try {
      const worker = new Worker(new URL("./rules-worker.ts", import.meta.url), { type: "module" });
      this.worker = worker;
      worker.addEventListener("message", (event: MessageEvent) => {
        if (this.worker !== worker || this.disposed)
          return;
        try {
          this.receive(event.data);
        } catch (error) {
          this.fail(error instanceof Error ? error.message : String(error));
        }
      });
      worker.addEventListener("error", (event) => {
        if (this.worker === worker)
          this.fail(event.message || "WASM Worker 載入失敗");
      });
      worker.addEventListener("messageerror", () => {
        if (this.worker === worker)
          this.fail("無法讀取 WASM Worker 回覆");
      });
      this.armTimeout(30000, "WASM 載入逾時；請確認 rules 資源已部署");
      worker.postMessage({ schema_version: 1, type: "initialize", generation: this.generation });
    } catch (error) {
      this.fail(error instanceof Error ? error.message : String(error), false);
    }
  }

  private receive(message: unknown): void {
    if (!isObject(message) || message.schema_version !== 1 || message.generation !== this.generation)
      throw new Error("WASM Worker 回覆版本或連線不符");
    if (message.type === "error")
      throw new Error(asString(message.error) || "WASM 規則執行失敗");
    if (message.type === "ready") {
      if (this.ready || !isObject(message.info) || message.info.schema_version !== RULES_BRIDGE_SCHEMA
          || !Number.isSafeInteger(message.info.card_count) || !Array.isArray(message.info.registry))
        throw new Error("WASM 卡牌目錄格式錯誤");
      const count = message.info.card_count as number;
      const registry = message.info.registry;
      if (count <= 0 || registry.length !== count)
        throw new Error("WASM 卡牌目錄不完整");
      const records: JsonObject[] = [];
      for (let id = 0; id < count; ++id) {
        const entry = registry[id];
        if (!isObject(entry) || entry.id !== id || typeof entry.object_name !== "string"
            || typeof entry.suit !== "number" || typeof entry.number !== "number")
          throw new Error("WASM 卡牌目錄識別不符");
        const existing = cardRecord(id);
        const nativeSuit = ["spade", "club", "heart", "diamond", "no_suit_black",
          "no_suit_red", "no_suit"][entry.suit];
        if (existing && (existing.object_name !== entry.object_name || existing.number !== entry.number
            || (existing.suit !== entry.suit && existing.suit !== nativeSuit)))
          throw new Error("cards.json 與 WASM 規則套件不符；請部署同版本資源");
        records.push(entry);
      }
      if (!isRulesIdentity(message.info.rules_bundle)) throw new Error("rules_identity_invalid");
      this.identity = message.info.rules_bundle;
      const supported = new Set<number>(INTERACTION_COMMANDS);
      if (Object.keys(this.identity.interaction_schemas as JsonObject).some(command => !supported.has(Number(command))))
        throw new Error("rules_interaction_unsupported");
      installRulesCardCatalog(records);
      this.registryCount = count;
      this.ready = true;
      this.clearTimeout();
      this.status = "ready";
      const waiting = this.identityWait;
      this.identityWait = null;
      waiting?.resolve(this.identity);
      this.pump();
      this.onChange();
      return;
    }
    if (message.type !== "stream" || !this.inFlight || message.id !== this.inFlight.id)
      throw new Error("WASM Worker 查詢關聯不符");
    const results = (message as unknown as { results: unknown }).results;
    if (!Array.isArray(results) || results.length === 0)
      throw new Error("WASM 規則串流回覆格式錯誤");
    const last: unknown = results[results.length - 1];
    if (!isObject(last) || last.schema_version !== 1 || typeof last.success !== "boolean"
        || typeof last.reason !== "string")
      throw new Error("WASM 規則串流回覆格式錯誤");
    const query = this.inFlight.query;
    this.inFlight = null;
    this.clearTimeout();
    // The runtime reports what it has committed; the browser never assumes it.
    this.native = nativeStatus(last.status);
    if (this.native.generation !== this.generation)
      throw new Error("WASM 規則串流連線不符");
    if (!last.success) {
      // A refused frame leaves native state unusable until a new connection.
      // A refused query only means this exact selection has no committed answer.
      if (query === null) {
        this.fail(last.reason);
        return;
      }
      this.rejectedKey = query.key;
      this.result = null;
      this.resultKey = "";
      this.status = "unsupported";
      this.error = rulesErrorMessage(last.reason);
    } else if (query !== null) {
      const parsed: unknown = last.evaluation;
      if (!isRulesEvaluation(parsed) || parsed.generation !== this.generation
          || parsed.revision !== query.revision || parsed.request_id !== query.requestId)
        throw new Error("WASM 規則結果格式或 request 不符");
      this.result = parsed;
      this.resultKey = query.key;
      this.status = parsed.known ? "ready" : "unsupported";
      this.error = parsed.known ? "" : parsed.reason;
    }
    this.pump();
    this.onChange();
  }

  // Ingest every observed frame before answering anything: a query may only run
  // against fully committed state, and STATE_SYNC is native's own gate.
  private pump(): void {
    if (this.disposed || !this.worker || !this.ready || this.inFlight || this.status === "failed")
      return;
    if (this.frames.length) {
      this.dispatch(this.frames.splice(0, FRAME_BATCH), null);
      return;
    }
    const desired = this.desired;
    if (!desired || this.native.generation !== this.generation || !this.native.active
        || this.native.failed || this.native.synchronizing
        || this.native.requestId !== desired.requestId)
      return;
    const key = this.key(this.native.revision, desired.requestId, desired.selection);
    if (key === this.resultKey || key === this.rejectedKey)
      return;
    if (this.session?.state.cardIdSpace !== this.registryCount) {
      this.fail("伺服器卡牌目錄與 builtin WASM 不符；需要相符的規則套件", false);
      return;
    }
    this.status = "evaluating";
    this.error = "";
    this.dispatch([{ schema_version: 1, action: "query", generation: this.generation,
      revision: this.native.revision, request_id: desired.requestId,
      selection: { ...desired.selection } }],
      { key, revision: this.native.revision, requestId: desired.requestId });
  }

  private dispatch(ops: Record<string, unknown>[], query: Query | null): void {
    const id = ++this.sequence;
    this.inFlight = { query, id };
    this.armTimeout(10000, "WASM 規則串流逾時；請重新連線");
    this.worker?.postMessage({ schema_version: 1, type: "stream",
      generation: this.generation, id, ops });
  }

  private armTimeout(milliseconds: number, message: string): void {
    this.clearTimeout();
    this.timer = setTimeout(() => this.fail(message), milliseconds);
  }

  private clearTimeout(): void {
    if (this.timer !== undefined)
      clearTimeout(this.timer);
    this.timer = undefined;
  }

  private fail(message: string, notify = true): void {
    this.identityWait?.reject(new Error(message));
    this.identityWait = null;
    this.releaseWorker();
    this.status = "failed";
    this.error = rulesErrorMessage(message);
    if (notify)
      this.onChange();
  }

  private releaseWorker(): void {
    this.identityWait?.reject(new Error("rules_reload_required"));
    this.identityWait = null;
    this.identity = null;
    this.clearTimeout();
    const old = this.worker;
    this.worker = null;
    this.ready = false;
    this.frames = [];
    this.native = UNBOUND;
    this.desired = null;
    this.desiredKey = "";
    this.rejectedKey = "";
    this.inFlight = null;
    this.result = null;
    this.resultKey = "";
    if (old) {
      // Give an idle VM a chance to shut down; a stuck native call is bounded.
      const stop = setTimeout(() => old.terminate(), 250);
      old.addEventListener("message", (event) => {
        if (event.data?.type === "disposed") {
          clearTimeout(stop);
          old.terminate();
        }
      });
      try {
        old.postMessage({ schema_version: 1, type: "dispose", generation: this.generation });
      } catch {
        clearTimeout(stop);
        old.terminate();
      }
    }
  }

  dispose(): void {
    this.disposed = true;
    this.session?.setFrameSink(null);
    this.releaseWorker();
    this.status = "idle";
  }
}

function nativeStatus(value: unknown): NativeStatus {
  if (!isObject(value) || !Number.isSafeInteger(value.generation)
      || !Number.isSafeInteger(value.revision) || typeof value.request_id !== "string"
      || typeof value.active !== "boolean" || typeof value.synchronizing !== "boolean"
      || typeof value.failed !== "boolean")
    throw new Error("WASM 規則串流狀態格式錯誤");
  return { generation: asNumber(value.generation), revision: asNumber(value.revision),
    requestId: asString(value.request_id), active: value.active,
    synchronizing: value.synchronizing, failed: value.failed };
}

function isSkill(skill: unknown): boolean {
  return isObject(skill) && typeof skill.name === "string"
    && Number.isSafeInteger(skill.instance_id) && typeof skill.available === "boolean"
    && typeof skill.status === "string" && typeof skill.v2 === "boolean"
    && typeof skill.invalid === "boolean" && typeof skill.response_or_use === "boolean"
    && typeof skill.usage_scope === "string" && typeof skill.expand_pile === "string"
    && Number.isSafeInteger(skill.subcard_min) && Number.isSafeInteger(skill.subcard_max)
    && Number.isSafeInteger(skill.usage_used);
}

export function isRulesEvaluation(value: unknown): value is RulesEvaluation {
  if (!isObject(value) || value.schema_version !== 1 || typeof value.known !== "boolean"
      || typeof value.can_confirm !== "boolean" || typeof value.reason !== "string"
      || typeof value.card_text !== "string" || !Array.isArray(value.selectable_cards)
      || !value.selectable_cards.every((id) => Number.isSafeInteger(id) && Number(id) >= 0)
      || !isObject(value.card_zones)
      || !Object.values(value.card_zones).every((zone) => typeof zone === "string")
      || !Number.isSafeInteger(value.selection_min) || !Number.isSafeInteger(value.selection_max)
      || !isObject(value.interaction)
      || !Array.isArray(value.declarations) || !value.declarations.every((name) => typeof name === "string")
      || !isObject(value.declaration_dialog)
      || !Array.isArray(value.skills) || !value.skills.every(isSkill) || !isObject(value.next_targets)
      || !Array.isArray(value.next_targets.candidates)
      || !value.next_targets.candidates.every((name) => typeof name === "string")
      || !isObject(value.next_targets.max_votes)
      || !Object.values(value.next_targets.max_votes).every((votes) => Number.isSafeInteger(votes)
        && Number(votes) >= 0))
    return false;
  return value.wire === null || (isObject(value.wire) && Number.isSafeInteger(value.wire.command)
    && typeof value.wire.reply_to === "string" && isObject(value.wire.payload));
}
