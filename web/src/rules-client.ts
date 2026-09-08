import { RULES_BRIDGE_SCHEMA, isRulesIdentity, rulesErrorMessage } from "./rules-identity";
import { cardRecord, installRulesCardCatalog } from "./i18n";
import { Command, asString, isObject, type JsonObject } from "./protocol";
import { INTERACTION_COMMANDS } from "./replies";
import type { LiveSession } from "./session";

export interface RulesSelection {
  card_ids: number[];
  targets: string[];
  skill_name: string;
  skill_instance_id: number;
  user_string: string;
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
  skills: { name: string; instance_id: number; available: boolean }[];
  declarations: string[];
  next_targets: { candidates: string[]; max_votes: Record<string, number> };
  wire: { command: number; reply_to: string; payload: JsonObject } | null;
}

interface Query {
  key: string;
  generation: number;
  revision: number;
  requestId: string;
  bytes: Uint8Array<ArrayBuffer>;
}

const encoder = new TextEncoder();
const decoder = new TextDecoder("utf-8", { fatal: true });
const INPUT_LIMIT = 4 * 1024 * 1024;
const OUTPUT_LIMIT = 8 * 1024 * 1024;

// One Engine per connection, one evaluation in flight, and only the newest
// queued selection. A changed request/state invalidates a preview immediately.
export class RulesController {
  result: RulesEvaluation | null = null;
  status = "idle";
  error = "";
  private worker: Worker | null = null;
  private generation = -1;
  private ready = false;
  private registryCount = 0;
  private desired: Query | null = null;
  private inFlight: { query: Query; id: number } | null = null;
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
    return new Promise((resolve, reject) => {
      this.identityWait = { resolve, reject };
      this.startWorker();
    });
  }

  constructor(private readonly onChange: () => void) {}

  supports(command: number): boolean {
    return [Command.PLAY_CARD, Command.RESPONSE_CARD, Command.ASK_PEACH,
      Command.NULLIFICATION].some((value) => value === command);
  }

  private key(session: LiveSession, selection: RulesSelection): string {
    return JSON.stringify([session.generation, session.revision,
      session.interaction?.messageId, session.interaction?.command, selection]);
  }

  current(session: LiveSession, selection: RulesSelection): boolean {
    return !this.disposed && !session.synchronizing && session.phase === "active"
      && this.result !== null && this.resultKey === this.key(session, selection);
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
      this.result = null;
      this.resultKey = "";
      return;
    }
    const key = this.key(session, selection);
    if (key === this.desired?.key)
      return;
    this.result = null;
    this.resultKey = "";
    // Failures stay visible until an explicit retry or a new connection.
    if (this.status === "failed")
      return;
    const state = session.state;
    const bytes = encoder.encode(JSON.stringify({
      schema_version: 1,
      generation: session.generation,
      revision: session.revision,
      request_id: interaction.messageId,
      command: interaction.command,
      payload: interaction.payload,
      state: {
        connection: state.connection, setup: state.setup, game: state.game,
        self_name: state.selfName, player_names: state.playerNames,
        players: state.playerNames.map((name) => state.players.get(name)), cards: [...state.cards.values()],
        card_id_space: state.cardIdSpace
      },
      selection
    }));
    if (bytes.byteLength > INPUT_LIMIT) {
      this.fail("可見遊戲狀態超過 WASM 輸入上限", false);
      return;
    }
    this.desired = { key, generation: session.generation, revision: session.revision,
      requestId: interaction.messageId, bytes };
    if (!this.worker) {
      this.startWorker();
      return;
    }
    this.sendLatest();
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
      this.sendLatest();
      this.onChange();
      return;
    }
    if (message.type !== "result" || !this.inFlight || message.id !== this.inFlight.id)
      throw new Error("WASM Worker 查詢關聯不符");
    const raw = (message as unknown as { bytes: unknown }).bytes;
    if (!(raw instanceof ArrayBuffer) || raw.byteLength > OUTPUT_LIMIT)
      throw new Error("WASM 規則結果大小或型別錯誤");
    const parsed: unknown = JSON.parse(decoder.decode(raw));
    const query = this.inFlight.query;
    this.inFlight = null;
    this.clearTimeout();
    if (!isEvaluation(parsed) || parsed.generation !== query.generation
        || parsed.revision !== query.revision || parsed.request_id !== query.requestId)
      throw new Error("WASM 規則結果格式或 request 不符");
    // Main-thread state may already have changed before the next animation frame.
    if (this.desired?.key === query.key && this.session?.generation === query.generation
        && this.session.revision === query.revision && !this.session.synchronizing
        && this.session.interaction?.messageId === query.requestId) {
      this.result = parsed;
      this.resultKey = query.key;
      this.status = parsed.known ? "ready" : "unsupported";
      this.error = parsed.known ? "" : parsed.reason;
    }
    this.sendLatest();
    this.onChange();
  }

  private sendLatest(): void {
    if (!this.worker || !this.ready || this.inFlight || !this.desired
        || this.resultKey === this.desired.key)
      return;
    if (this.session?.generation !== this.desired.generation
        || this.session.revision !== this.desired.revision || this.session.synchronizing
        || this.session.interaction?.messageId !== this.desired.requestId) {
      this.desired = null;
      return;
    }
    if (this.session?.state.cardIdSpace !== this.registryCount) {
      this.fail("伺服器卡牌目錄與 builtin WASM 不符；需要相符的規則套件", false);
      return;
    }
    const query = this.desired;
    const id = ++this.sequence;
    this.inFlight = { query, id };
    this.status = "evaluating";
    this.error = "";
    this.armTimeout(10000, "WASM 規則查詢逾時；請重新連線");
    // Transfer a copy; the latest queued input remains owned by this controller.
    const bytes = query.bytes.slice();
    this.worker.postMessage({ schema_version: 1, type: "evaluate",
      generation: this.generation, id, input: bytes.buffer }, [bytes.buffer]);
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
    this.desired = null;
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
    this.releaseWorker();
    this.status = "idle";
  }

  retry(): void {
    if (this.disposed || this.status !== "failed")
      return;
    this.releaseWorker();
    this.status = "idle";
    this.error = "";
    this.onChange();
  }
}

function isEvaluation(value: unknown): value is RulesEvaluation {
  if (!isObject(value) || value.schema_version !== 1 || typeof value.known !== "boolean"
      || typeof value.can_confirm !== "boolean" || typeof value.reason !== "string"
      || typeof value.card_text !== "string" || !Array.isArray(value.selectable_cards)
      || !value.selectable_cards.every((id) => Number.isSafeInteger(id) && Number(id) >= 0)
      || !Array.isArray(value.declarations) || !value.declarations.every((name) => typeof name === "string")
      || !Array.isArray(value.skills) || !value.skills.every((skill) => isObject(skill)
        && typeof skill.name === "string" && Number.isSafeInteger(skill.instance_id)
        && typeof skill.available === "boolean") || !isObject(value.next_targets)
      || !Array.isArray(value.next_targets.candidates)
      || !value.next_targets.candidates.every((name) => typeof name === "string")
      || !isObject(value.next_targets.max_votes)
      || !Object.values(value.next_targets.max_votes).every((votes) => Number.isSafeInteger(votes)
        && Number(votes) >= 0))
    return false;
  return value.wire === null || (isObject(value.wire) && Number.isSafeInteger(value.wire.command)
    && typeof value.wire.reply_to === "string" && isObject(value.wire.payload));
}
