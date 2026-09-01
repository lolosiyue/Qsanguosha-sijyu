import { cardRecord } from "./i18n";
import {
  Command,
  PLACE_EQUIP,
  PLACE_HAND,
  asBool,
  asNumber,
  asString,
  asStringList,
  isObject,
  type JsonObject,
  type JsonValue
} from "./protocol";
import type { ClientGameState } from "./state";

export const Method = {
  None: 0,
  Use: 1,
  Response: 2,
  Discard: 3,
  Recast: 4
} as const;

const METHOD_NAME: Record<number, string> = {
  [Method.Use]: "use",
  [Method.Response]: "response",
  [Method.Discard]: "discard",
  [Method.Recast]: "recast"
};

const WEAPON_RANGE: Record<string, number> = {
  crossbow: 1, double_sword: 2, qinggang_sword: 2, ice_sword: 2,
  guding_blade: 2, blade: 3, spear: 3, axe: 3, fan: 4, halberd: 4,
  kylin_bow: 5
};

const OFFENSIVE_HORSE = new Set(["chitu", "dayuan", "zixing"]);
const DEFENSIVE_HORSE = new Set(["jueying", "dilu", "zhuahuangfeidian", "hualiu"]);

const ANCESTORS: Record<string, string[]> = {
  slash: ["Slash", "BasicCard", "basic"],
  fire_slash: ["FireSlash", "NatureSlash", "Slash", "BasicCard", "basic"],
  thunder_slash: ["ThunderSlash", "NatureSlash", "Slash", "BasicCard", "basic"],
  ice_slash: ["IceSlash", "NatureSlash", "Slash", "BasicCard", "basic"],
  jink: ["Jink", "BasicCard", "basic"],
  peach: ["Peach", "BasicCard", "basic"],
  analeptic: ["Analeptic", "BasicCard", "basic"],
  nullification: ["Nullification", "SingleTargetTrick", "TrickCard", "trick"],
  snatch: ["Snatch", "SingleTargetTrick", "TrickCard", "trick"],
  dismantlement: ["Dismantlement", "SingleTargetTrick", "TrickCard", "trick"],
  duel: ["Duel", "SingleTargetTrick", "TrickCard", "trick"],
  fire_attack: ["FireAttack", "SingleTargetTrick", "TrickCard", "trick"],
  collateral: ["Collateral", "SingleTargetTrick", "TrickCard", "trick"],
  ex_nihilo: ["ExNihilo", "SingleTargetTrick", "TrickCard", "trick"],
  indulgence: ["Indulgence", "DelayedTrick", "TrickCard", "trick"],
  supply_shortage: ["SupplyShortage", "DelayedTrick", "TrickCard", "trick"],
  lightning: ["Lightning", "DelayedTrick", "TrickCard", "trick"],
  amazing_grace: ["AmazingGrace", "GlobalEffect", "TrickCard", "trick"],
  god_salvation: ["GodSalvation", "GlobalEffect", "TrickCard", "trick"],
  archery_attack: ["ArcheryAttack", "AOE", "TrickCard", "trick"],
  savage_assault: ["SavageAssault", "AOE", "TrickCard", "trick"],
  iron_chain: ["IronChain", "TrickCard", "trick"]
};

function className(objectName: string): string {
  return objectName.split("_").filter(Boolean)
    .map((part) => part.charAt(0).toUpperCase() + part.slice(1)).join("");
}

function kinds(objectName: string): string[] {
  const extras = ANCESTORS[objectName] ?? [];
  return [objectName, className(objectName), ...extras];
}

function isKind(objectName: string, typeName: string): boolean {
  if (!typeName || typeName === ".")
    return true;
  const needle = typeName.startsWith("%") ? typeName.slice(1) : typeName;
  return kinds(objectName).some((kind) => kind.toLowerCase() === needle.toLowerCase());
}

export function cardObjectNameOf(state: ClientGameState, cardId: number): string {
  const card = state.card(cardId);
  const catalog = cardRecord(cardId);
  return asString(card?.object_name)
    || asString(card?.card_name)
    || asString(catalog?.object_name);
}

function cardSuit(state: ClientGameState, cardId: number): string {
  const card = state.card(cardId);
  const catalog = cardRecord(cardId);
  const raw = card?.suit ?? catalog?.suit;
  if (typeof raw === "number")
    return ["spade", "club", "heart", "diamond"][raw] ?? "";
  return asString(raw);
}

function cardNumber(state: ClientGameState, cardId: number): number {
  const card = state.card(cardId);
  const catalog = cardRecord(cardId);
  return asNumber(card?.number, asNumber(catalog?.number));
}

function cardColor(suit: string): string {
  if (suit === "heart" || suit === "diamond")
    return "red";
  if (suit === "spade" || suit === "club")
    return "black";
  return "no_suit";
}

function playerFlags(state: ClientGameState, name: string): string[] {
  return asStringList(state.playerValue(name, "flags"));
}

function hasFlag(state: ClientGameState, name: string, flag: string): boolean {
  return playerFlags(state, name).includes(flag);
}

function historyCount(state: ClientGameState, player: string, key: string): number {
  const history = isObject(state.playerValue(player, "history"))
    ? state.playerValue(player, "history") as JsonObject
    : {};
  return asNumber(history[key]);
}

function slashCount(state: ClientGameState, player: string): number {
  return ["Slash", "FireSlash", "ThunderSlash", "IceSlash", "slash",
    "fire_slash", "thunder_slash", "ice_slash"]
    .reduce((sum, key) => sum + historyCount(state, player, key), 0);
}

function aliveNames(state: ClientGameState): string[] {
  return state.playerNames.filter((name) => state.isPlayerAlive(name)
    && !asBool(state.player(name)?.removed));
}

function seatOf(state: ClientGameState, name: string): number {
  const seat = asNumber(state.playerValue(name, "seat"));
  if (seat > 0)
    return seat;
  return state.playerNames.indexOf(name) + 1;
}

function equipNames(state: ClientGameState, player: string): string[] {
  return state.cardsForPlayer(player, PLACE_EQUIP).map((id) => cardObjectNameOf(state, id));
}

function horseCorrect(names: string[], offensive: boolean): number {
  if (offensive)
    return names.some((name) => OFFENSIVE_HORSE.has(name)) ? -1 : 0;
  return names.some((name) => DEFENSIVE_HORSE.has(name)) ? 1 : 0;
}

export function distanceTo(state: ClientGameState, from: string, to: string, distanceFix = 0): number {
  if (!from || !to || from === to)
    return 0;
  const fixed = isObject(state.playerValue(from, "fixed_distances"))
    ? asNumber((state.playerValue(from, "fixed_distances") as JsonObject)[to], -1)
    : -1;
  if (fixed >= 0)
    return Math.max(fixed, 1);
  const alive = aliveNames(state).length;
  const gap = Math.abs(seatOf(state, from) - seatOf(state, to));
  let right = Math.min(alive - gap, gap);
  right += horseCorrect(equipNames(state, from), true);
  right += horseCorrect(equipNames(state, to), false);
  right += distanceFix;
  return Math.max(right, 1);
}

export function attackRange(state: ClientGameState, player: string): number {
  if (hasFlag(state, player, "InfinityAttackRange"))
    return 999;
  let range = 1;
  for (const name of equipNames(state, player)) {
    const weapon = WEAPON_RANGE[name];
    if (weapon !== undefined)
      range = range === 1 ? weapon : Math.max(range, weapon);
  }
  return Math.max(range, 0);
}

export function inAttackRange(state: ClientGameState, from: string, to: string, distanceFix = 0): boolean {
  if (from === to)
    return false;
  if (asStringList(state.playerValue(from, "attack_range_pairs")).includes(to))
    return true;
  return distanceTo(state, from, to, distanceFix) <= attackRange(state, from);
}

function matchFactor(state: ClientGameState, cardId: number, factor: string, self: string): boolean {
  const parts = factor.split("|");
  const objectName = cardObjectNameOf(state, cardId);
  if (parts[0] && parts[0] !== ".") {
    const ok = parts[0].split(",").some((orName) => {
      return orName.split("+").every((token) => {
        const positive = token.startsWith("^");
        const name = positive ? token.slice(1) : token;
        const hit = isKind(objectName, name) || asString(state.card(cardId)?.card_string) === name;
        return positive ? !hit : hit;
      });
    });
    if (!ok)
      return false;
  }
  if (!parts[1] || parts[1] === ".") {
    // continue
  } else {
    const suit = cardSuit(state, cardId);
    const color = cardColor(suit);
    const ok = parts[1].split(",").some((token) => {
      const positive = token.startsWith("^");
      const name = positive ? token.slice(1) : token;
      const hit = suit === name || color === name;
      return positive ? !hit : hit;
    });
    if (!ok)
      return false;
  }
  if (parts[2] && parts[2] !== ".") {
    const number = cardNumber(state, cardId);
    const ok = parts[2].split(",").some((token) => {
      const positive = token.startsWith("^");
      const raw = positive ? token.slice(1) : token;
      let hit = false;
      if (raw.includes("~")) {
        const [fromText, toText] = raw.split("~");
        const from = fromText ? Number(fromText) : 1;
        const to = toText ? Number(toText) : 13;
        hit = number >= from && number <= to;
      } else if (/^\d+$/.test(raw)) {
        hit = number === Number(raw);
      }
      return positive ? !hit : hit;
    });
    if (!ok)
      return false;
  }
  if (parts[3] && parts[3] !== "." && self) {
    const place = asNumber(state.card(cardId)?.place);
    const pile = asString(state.card(cardId)?.pile);
    const ok = parts[3].split(",").some((token) => {
      const positive = token.startsWith("^");
      const name = positive ? token.slice(1) : token;
      let hit = false;
      if (name === "hand")
        hit = place === PLACE_HAND;
      else if (name === "equipped")
        hit = place === PLACE_EQUIP;
      else
        hit = pile === name;
      return positive ? !hit : hit;
    });
    if (!ok)
      return false;
  }
  return true;
}

export function matchPattern(state: ClientGameState, cardId: number, pattern: string, self: string): boolean {
  if (!pattern || pattern === ".")
    return true;
  let exp = pattern;
  if (exp.endsWith("!"))
    exp = exp.slice(0, -1);
  if (/\d$/.test(exp) && !exp.includes("|") && !exp.includes(",") && !exp.includes("#"))
    exp = exp.replace(/\d+$/, "");
  // Engine maps objectName patterns (jink) to class names (Jink).
  if (!/[|#,]/.test(exp) && ANCESTORS[exp])
    exp = kinds(exp).find((kind) => kind[0] === kind[0].toUpperCase() && kind !== exp) || className(exp);
  return exp.split("#").some((one) => matchFactor(state, cardId, one, self));
}

function limitationPattern(raw: string): string {
  return raw.includes("$") ? raw.replace(/\$(0|1)$/, "") : raw;
}

export function isCardLimited(state: ClientGameState, cardId: number, method: number, self: string): boolean {
  const methodName = METHOD_NAME[method];
  if (!methodName)
    return false;
  if (method === Method.Use && isKind(cardObjectNameOf(state, cardId), "Peach")
      && asNumber(isObject(state.player(self)?.marks) ? (state.player(self)?.marks as JsonObject)["Global_PreventPeach"] : 0) > 0)
    return true;
  const limitations = Array.isArray(state.playerValue(self, "card_limitations"))
    ? state.playerValue(self, "card_limitations") as JsonValue[]
    : [];
  for (const item of limitations) {
    if (!isObject(item))
      continue;
    const methods = asStringList(item.methods);
    if (methods.length > 0 && !methods.includes(methodName))
      continue;
    const pattern = limitationPattern(asString(item.pattern));
    if (pattern && matchPattern(state, cardId, pattern, self))
      return true;
  }
  return false;
}

function residueOk(state: ClientGameState, self: string, objectName: string): boolean {
  if (isKind(objectName, "Slash")) {
    const extra = equipNames(state, self).includes("crossbow") ? 999 : 0;
    return slashCount(state, self) <= extra;
  }
  if (isKind(objectName, "Analeptic"))
    return historyCount(state, self, "Analeptic") + historyCount(state, self, "analeptic") <= 0;
  return true;
}

export function isPlayAvailable(state: ClientGameState, cardId: number, self: string): boolean {
  const name = cardObjectNameOf(state, cardId);
  if (!name)
    return true;
  if (isCardLimited(state, cardId, Method.Use, self))
    return false;
  if (isKind(name, "Jink") || isKind(name, "Nullification"))
    return false;
  if (!residueOk(state, self, name))
    return false;
  if (isKind(name, "Peach"))
    return asNumber(state.player(self)?.hp) < asNumber(state.player(self)?.max_hp);
  if (isKind(name, "Slash"))
    return aliveNames(state).some((other) => other !== self && canSlash(state, self, other, cardId, []));
  if (isKind(name, "Snatch") || isKind(name, "SupplyShortage"))
    return aliveNames(state).some((other) => other !== self && distanceTo(state, self, other) <= 1);
  return true;
}

export function isTargetFixed(objectName: string): boolean {
  return isKind(objectName, "Jink")
    || isKind(objectName, "Peach")
    || isKind(objectName, "Analeptic")
    || isKind(objectName, "Nullification")
    || isKind(objectName, "ExNihilo")
    || isKind(objectName, "Lightning")
    || isKind(objectName, "AOE")
    || isKind(objectName, "GlobalEffect")
    || WEAPON_RANGE[objectName] !== undefined
    || OFFENSIVE_HORSE.has(objectName)
    || DEFENSIVE_HORSE.has(objectName)
    || ["eight_diagram", "renwang_shield", "vine", "silver_lion", "wooden_ox"].includes(objectName);
}

function canSlash(state: ClientGameState, self: string, other: string, cardId: number, selected: string[]): boolean {
  if (self === other || !state.isPlayerAlive(other))
    return false;
  if (hasFlag(state, self, "slashDisableExtraTarget") && !hasFlag(state, other, "SlashAssignee"))
    return false;
  if (hasFlag(state, self, "slashTargetFix") && !hasFlag(state, other, "SlashAssignee")
      && aliveNames(state).some((name) => hasFlag(state, name, "SlashAssignee")
        && !selected.includes(name)))
    return false;
  if (selected.length >= 1)
    return false;
  if (hasFlag(state, self, "slashNoDistanceLimit"))
    return true;
  let fix = 0;
  const subs = [cardId];
  const oh = state.cardsForPlayer(self, PLACE_EQUIP)
    .find((id) => OFFENSIVE_HORSE.has(cardObjectNameOf(state, id)));
  if (oh !== undefined && subs.includes(oh))
    fix += 1;
  return inAttackRange(state, self, other, fix);
}

export function canSelectPlayer(
  state: ClientGameState, self: string, other: string, cardId: number, selected: string[]
): boolean {
  if (!state.isPlayerAlive(other) || asBool(state.player(other)?.removed))
    return false;
  const name = cardObjectNameOf(state, cardId);
  if (isTargetFixed(name))
    return false;
  if (isKind(name, "Slash"))
    return canSlash(state, self, other, cardId, selected);
  if (isKind(name, "Snatch") || isKind(name, "SupplyShortage")) {
    if (other === self || selected.length >= 1)
      return false;
    return distanceTo(state, self, other) <= 1;
  }
  if (isKind(name, "Indulgence") || isKind(name, "Dismantlement") || isKind(name, "Duel")
      || isKind(name, "FireAttack") || isKind(name, "Collateral") || isKind(name, "IronChain")) {
    return other !== self && selected.length < (isKind(name, "IronChain") ? 2 : 1);
  }
  return other !== self && selected.length < 1;
}

export function targetsAreFeasible(
  state: ClientGameState, self: string, cardId: number, selected: string[]
): boolean {
  const name = cardObjectNameOf(state, cardId);
  if (isTargetFixed(name))
    return selected.length === 0;
  if (isKind(name, "IronChain"))
    return selected.length >= 1 && selected.length <= 2;
  return selected.length >= 1;
}

export type CardUseMode = "play" | "response" | "discard" | "free";

export function useMode(command: number): CardUseMode {
  if (command === Command.PLAY_CARD)
    return "play";
  if (command === Command.RESPONSE_CARD || command === Command.ASK_PEACH
      || command === Command.NULLIFICATION)
    return "response";
  if (command === Command.DISCARD_CARD || command === Command.EXCHANGE_CARD)
    return "discard";
  return "free";
}

export function responsePattern(command: number, payload: JsonObject): string {
  const raw = asString(payload.pattern);
  if (raw)
    return raw;
  if (command === Command.ASK_PEACH)
    return "peach,analeptic";
  if (command === Command.NULLIFICATION)
    return "nullification";
  return "";
}

export function handlingMethod(command: number, payload: JsonObject): number {
  const raw = asNumber(payload.handling_method, -1);
  if (raw >= 0)
    return raw;
  const mode = useMode(command);
  if (mode === "play")
    return Method.Use;
  if (mode === "response")
    return Method.Response;
  if (mode === "discard")
    return Method.Discard;
  return Method.None;
}

export function cardSelectable(
  state: ClientGameState, command: number, payload: JsonObject, cardId: number, skillName: string
): boolean {
  const self = state.selfName;
  const mode = useMode(command);
  if (mode === "free")
    return true;
  if (skillName && mode === "play")
    return true;
  const method = handlingMethod(command, payload);
  if (mode === "discard") {
    const includeEquip = asBool(payload.include_equip);
    const place = asNumber(state.card(cardId)?.place);
    if (place === PLACE_EQUIP && !includeEquip)
      return false;
    if (isCardLimited(state, cardId, Method.Discard, self))
      return false;
    return matchPattern(state, cardId, asString(payload.pattern) || ".", self);
  }
  if (mode === "response") {
    if (isCardLimited(state, cardId, method || Method.Response, self))
      return false;
    const pattern = responsePattern(command, payload);
    return pattern ? matchPattern(state, cardId, pattern, self) : true;
  }
  return isPlayAvailable(state, cardId, self);
}

export function playerSelectable(
  state: ClientGameState, command: number, payload: JsonObject,
  player: string, cardId: number, selected: string[], skillName: string
): boolean {
  if (command === Command.CHOOSE_PLAYER) {
    const allowed = asStringList(payload.players);
    return allowed.length === 0 || allowed.includes(player);
  }
  if (useMode(command) !== "play")
    return false;
  if (skillName)
    return state.isPlayerAlive(player);
  if (cardId < 0)
    return false;
  return canSelectPlayer(state, state.selfName, player, cardId, selected.filter((name) => name !== player));
}
