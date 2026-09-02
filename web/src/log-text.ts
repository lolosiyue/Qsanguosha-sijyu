import { cardRecord, tr } from "./i18n";
import {
  Command,
  GameEvent,
  PLACE_DELAYED_TRICK,
  PLACE_DISCARD,
  PLACE_DRAW,
  PLACE_EQUIP,
  PLACE_HAND,
  PLACE_JUDGE,
  PLACE_SPECIAL,
  PLACE_TABLE,
  asBool,
  asNumber,
  asNumberList,
  asString,
  asStringList,
  isObject,
  type JsonObject
} from "./protocol";
import type { ClientGameState, PresentationEvent } from "./state";

export type PlayerNameResolver = (objectName: string) => string;

interface LogRecord {
  type: string;
  from: string;
  tos: string[];
  cardString: string;
  arg: string;
  arg2: string;
  arg3?: string;
  arg4?: string;
  arg5?: string;
}

const REASON_PUT = 0x0a;
const REASON_EXCLUSIVE = 0x68;
const REASON_TURNOVER = 0x18;
const REASON_PREVIEW = 0x38;
const REASON_SHUFFLE = 0x5a;
const REASON_PUT_END = 0x6a;
const REASON_TRANSFER = 0x09;
const UNKNOWN_CARD_ID = -1;

function phrase(key: string, fallback: string): string {
  const translated = tr(key);
  return !translated || translated === key ? fallback : translated;
}

function cardIdsOf(move: JsonObject): number[] {
  const value = move.card_ids;
  if (typeof value === "string" && value.length > 0)
    return value.split("+").map((token) => Number(token));
  return asNumberList(value);
}

function joinCardIds(ids: number[]): string {
  return ids.map(String).join("+");
}

function reasonMap(move: JsonObject): JsonObject {
  return isObject(move.reason) ? move.reason : {};
}

function shouldIgnoreDisplayMove(move: JsonObject): boolean {
  if (asString(move.to_pile).startsWith("#") || asString(move.from_pile).startsWith("#"))
    return true;
  if (asNumber(move.to_place) === PLACE_DISCARD) {
    const fromPlace = asNumber(move.from_place);
    return fromPlace === PLACE_TABLE || fromPlace === PLACE_JUDGE;
  }
  return false;
}

function record(
  type: string,
  from: string,
  tos: string[],
  cardString: string,
  arg = "",
  arg2 = ""
): LogRecord {
  return { type, from, tos, cardString, arg, arg2 };
}

function toSkillLogMap(value: LogRecord): JsonObject {
  return {
    schema_version: 1,
    log_type: value.type,
    from_player: value.from,
    to_players: value.tos,
    card_string: value.cardString,
    arguments: [value.arg, value.arg2, value.arg3 ?? "", value.arg4 ?? "", value.arg5 ?? ""]
  };
}

function synthesizeLoseCardLogs(move: JsonObject): LogRecord[] {
  if (shouldIgnoreDisplayMove(move))
    return [];
  if (asNumber(move.from_place) !== PLACE_EQUIP)
    return [];
  return [record("#Uninstall", asString(move.from_player), [], joinCardIds(cardIdsOf(move)))];
}

function synthesizeGetCardLogs(move: JsonObject): LogRecord[] {
  if (shouldIgnoreDisplayMove(move))
    return [];
  const logs: LogRecord[] = [];
  const ids = cardIdsOf(move);
  const cardString = joinCardIds(ids);
  const count = String(ids.length);
  const fromPlayer = asString(move.from_player);
  const toPlayer = asString(move.to_player);
  const fromPlace = asNumber(move.from_place);
  const toPlace = asNumber(move.to_place);
  const reason = asNumber(reasonMap(move).reason);
  const unknown = ids.includes(UNKNOWN_CARD_ID);
  const hasFrom = fromPlayer.length > 0;

  if (toPlace === PLACE_HAND) {
    if (fromPlace === PLACE_DRAW)
      logs.push(record("$DrawCards", toPlayer, [], cardString, count));
    else if (fromPlace === PLACE_DISCARD)
      logs.push(record("$RecycleCard", toPlayer, [], cardString));
    else if (fromPlace === PLACE_SPECIAL)
      logs.push(record("#GotNCardFromPile", toPlayer, [fromPlayer], cardString,
        asString(move.from_pile), count));
    else if (fromPlace === PLACE_TABLE || fromPlace === PLACE_JUDGE) {
      if (reason !== REASON_PREVIEW && !unknown) {
        logs.push(record(reason === REASON_EXCLUSIVE ? "$TakeAG" : "$GotCardBack",
          toPlayer, [], cardString));
      }
    } else if (hasFrom) {
      if (fromPlayer === toPlayer && fromPlace === PLACE_EQUIP)
        logs.push(record("$GotCardBack", toPlayer, [], cardString));
      else if (unknown)
        logs.push(record("#MoveNCards", fromPlayer, [toPlayer], "", count));
      else
        logs.push(record("$MoveCard", fromPlayer, [toPlayer], cardString));
    }
  } else if (toPlace === PLACE_SPECIAL) {
    const pile = asString(move.to_pile);
    if (!pile.startsWith("#")) {
      if (unknown)
        logs.push(record("#RemoveFromGame", toPlayer, [], "", pile, count));
      else
        logs.push(record("$AddToPile", toPlayer, [], cardString, pile));
    }
  } else if (hasFrom) {
    if (toPlace === PLACE_DELAYED_TRICK) {
      let type = "$LightningMove";
      if (fromPlace !== PLACE_DELAYED_TRICK && reason !== REASON_TRANSFER)
        type = "$PasteCard";
      if (unknown)
        type = "#LightningMove";
      logs.push(record(type, fromPlayer, [toPlayer], cardString, count));
    } else if (toPlace === PLACE_DRAW) {
      if (reason === REASON_PUT && asString(reasonMap(move).skill_name) === "luck_card")
        return logs;
      let type = "$PutCard";
      if (reason === REASON_SHUFFLE)
        type = "$ShuffleCard";
      else if (reason === REASON_PUT_END)
        type = "$PutCardEnd";
      logs.push(record(type, fromPlayer, [], cardString, count));
    }
  }

  if (toPlace === PLACE_EQUIP) {
    if (hasFrom && fromPlayer !== toPlayer) {
      if (unknown)
        logs.push(record("#MoveNCards", fromPlayer, [toPlayer], "", count));
      else
        logs.push(record("$MoveCard", fromPlayer, [toPlayer], cardString));
    }
    logs.push(record("#Install", toPlayer, [], cardString));
  }
  if (reason === REASON_TURNOVER)
    logs.push(record("$TurnOver", asString(reasonMap(move).player_id), [], cardString));
  return logs;
}

function synthesizeRenPileLogs(command: number, move: JsonObject, renPile?: number[]): LogRecord[] {
  if (!renPile)
    return [];
  if (command === Command.LOSE_CARD) {
    if (asNumber(move.from_place) !== PLACE_TABLE || renPile.length === 0)
      return [];
    const ids: number[] = [];
    for (const id of cardIdsOf(move)) {
      if (!renPile.includes(id))
        continue;
      for (let i = renPile.length - 1; i >= 0; --i) {
        if (renPile[i] === id)
          renPile.splice(i, 1);
      }
      ids.push(id);
    }
    if (ids.length === 0)
      return [];
    return [record("$removeRenPile", "", [], joinCardIds(ids), String(ids.length), "ren_pile")];
  }
  if (command === Command.GET_CARD
      && asNumber(move.to_place) === PLACE_TABLE
      && asString(move.to_pile) === "ren_pile") {
    const ids = cardIdsOf(move);
    renPile.push(...ids);
    return [record("$addRenPile", asString(reasonMap(move).player_id), [],
      joinCardIds(ids), String(ids.length), "ren_pile")];
  }
  return [];
}

function synthesizeHpChangeLogs(payload: JsonObject, hpAfter: number, maxHp: number): LogRecord[] {
  const who = asString(payload.player_name);
  const delta = asNumber(payload.delta);
  const nature = asNumber(payload.nature, 0);
  const lostHp = asNumber(payload.lost_hp);
  const logs: LogRecord[] = [];
  if (delta <= 0) {
    if (nature < 0)
      logs.push(record("#LoseHp", who, [], "", String(-delta)));
  } else {
    logs.push(record("#Recover", who, [], "", String(delta)));
  }
  logs.push(record("#GetHp", who, [], "", String(hpAfter + lostHp), String(maxHp)));
  return logs;
}

function synthesizeMaxHpChangeLogs(who: string, hp: number, maxHpAfter: number): LogRecord[] {
  return [record("#GetHp", who, [], "", String(hp), String(maxHpAfter))];
}

export function appendSynthesizedLogs(
  state: ClientGameState,
  command: number,
  payload: JsonObject,
  renPile?: number[]
): void {
  let records: LogRecord[] = [];
  if (command === Command.GET_CARD || command === Command.LOSE_CARD) {
    for (const entry of Array.isArray(payload.moves) ? payload.moves : []) {
      if (!isObject(entry))
        continue;
      records = records.concat(synthesizeRenPileLogs(command, entry, renPile));
      records = records.concat(
        command === Command.LOSE_CARD ? synthesizeLoseCardLogs(entry) : synthesizeGetCardLogs(entry)
      );
    }
  } else if (command === Command.CHANGE_HP) {
    const who = asString(payload.player_name);
    records = synthesizeHpChangeLogs(
      payload,
      asNumber(state.playerValue(who, "hp")),
      asNumber(state.playerValue(who, "max_hp"))
    );
  } else if (command === Command.CHANGE_MAXHP) {
    const who = asString(payload.player_name);
    records = synthesizeMaxHpChangeLogs(
      who,
      asNumber(state.playerValue(who, "hp")),
      asNumber(state.playerValue(who, "max_hp"))
    );
  } else {
    return;
  }
  for (const value of records) {
    const map = toSkillLogMap(value);
    const line = formatSkillLog(map, (name) => logPlayerName(state, name));
    if (line)
      state.appendPresentationEvent(Command.LOG_SKILL, line, map);
  }
}

export function logPlayerName(state: ClientGameState, objectName: string): string {
  const player = state.player(objectName);
  let general = asString(player?.general);
  if (!general)
    general = asString(player?.avatar);
  if (!general)
    return objectName;
  let name = tr(general);
  const deputy = asString(player?.deputy_general);
  if (deputy)
    name += `/${tr(deputy)}`;
  return name;
}

function cardDisplay(id: number): string {
  const card = cardRecord(id);
  if (!card)
    return `牌 ${id}`;
  const name = tr(asString(card.object_name)) || asString(card.object_name) || `牌 ${id}`;
  const suitKey = asString(card.suit);
  const suit = suitKey && suitKey !== "no_suit" ? (tr(suitKey) || suitKey) : "";
  const number = asNumber(card.number);
  const numberText = number > 0 ? String(number) : "";
  if (!suit && !numberText)
    return name;
  return `${name}[${suit}${numberText}]`;
}

function dollarCards(cardString: string): string {
  return cardString
    .split("+")
    .filter((token) => token.length > 0)
    .map((token) => cardDisplay(Number(token)))
    .join("、");
}

function virtualCardName(cardString: string): { drop: boolean; name: string; skill: string; subIds: number[] } | null {
  if (/^-?\d+(?:\+-?\d+)*$/.test(cardString))
    return { drop: false, name: "", skill: "", subIds: cardString.split("+").map(Number) };
  const match = /^(#|@)([^\[=:]+)(?:\[[^\]]*\])?(?:=|:)?(.*)$/.exec(cardString);
  if (!match)
    return null;
  const rawName = match[2];
  if (rawName.startsWith("#"))
    return { drop: true, name: "", skill: "", subIds: [] };
  const rest = match[3].replace(/:$/, "");
  const subIds = rest && rest !== "."
    ? rest.split("+").filter((token) => /^-?\d+$/.test(token)).map(Number)
    : [];
  const objectName = rawName.charAt(0) === rawName.charAt(0).toUpperCase()
    ? rawName.replace(/Card$/u, "").toLowerCase()
    : rawName.replace(/Card$/u, "");
  return { drop: false, name: objectName, skill: objectName, subIds };
}

function useCardSentence(type: string, cardString: string, from: string, tos: string[]): string {
  const parsed = virtualCardName(cardString);
  if (!parsed || parsed.drop)
    return "";
  const usingText = phrase("#UseCardPhrase_using", "使用");
  const playingText = phrase("#UseCardPhrase_playing", "打出");
  const recastingText = phrase("#UseCardPhrase_recasting", "重铸");
  const useSkillText = phrase("#UseCardPhrase_useSkill", "发动");
  let reason = usingText;
  if (type.endsWith("_Resp"))
    reason = playingText;
  if (type.endsWith("_Recast"))
    reason = recastingText;
  const cardName = parsed.subIds.length === 1 && !parsed.name
    ? cardDisplay(parsed.subIds[0])
    : parsed.subIds.length > 0 && !parsed.name
      ? parsed.subIds.map(cardDisplay).join("、")
      : tr(parsed.name) || parsed.name;
  let log: string;
  if (parsed.name) {
    const skill = tr(parsed.skill) || parsed.skill;
    const sub = parsed.subIds.map(cardDisplay).join("、");
    if (sub) {
      log = phrase("#UseCardPhrase_skillCost", "%from %3了 [%1]%4，消耗为 %2")
        .replace("%1", skill)
        .replace("%2", sub)
        .replace("%3", useSkillText)
        .replace("%4", "");
    } else {
      log = phrase("#UseCardPhrase_skill", "%from %2了 [%1]%3")
        .replace("%1", skill)
        .replace("%2", useSkillText)
        .replace("%3", "");
    }
  } else {
    log = phrase("#UseCardPhrase_plain", "%from %2了 %1")
      .replace("%1", cardName)
      .replace("%2", reason);
  }
  if (tos.length > 0)
    log += phrase("#UseCardPhrase_target", "，目标是 %to");
  void from;
  return log;
}

export function formatSkillLog(payload: JsonObject, playerName: PlayerNameResolver): string {
  const type = asString(payload.log_type);
  if (!type)
    return "";
  if (type === "$AppendSeparator")
    return "--------";
  const from = asString(payload.from_player);
  const tos = asStringList(payload.to_players);
  const cardString = asString(payload.card_string);
  let log = "";
  if (type.startsWith("#UseCard") && from) {
    log = useCardSentence(type, cardString, from, tos);
    if (!log)
      return "";
  } else {
    log = tr(type) || type;
    if (cardString) {
      const cards = type.startsWith("$") || /^-?\d+(?:\+-?\d+)*$/.test(cardString)
        ? dollarCards(cardString)
        : virtualCardName(cardString)?.name
          ? tr(virtualCardName(cardString)!.name)
          : dollarCards(cardString);
      log = log.replaceAll("%card", cards);
    }
  }
  if (from)
    log = log.replaceAll("%from", playerName(from) || from);
  if (tos.length > 0) {
    const names = tos.map((to) => (to === from
      ? phrase("#LogSelf", "自己")
      : playerName(to) || to));
    log = log.replaceAll("%to", names.join("、"));
  }
  const argumentsList = asStringList(payload.arguments);
  const placeholders = ["%arg", "%arg2", "%arg3", "%arg4", "%arg5"];
  for (let i = placeholders.length - 1; i >= 0; --i) {
    if (!log.includes(placeholders[i]))
      continue;
    const key = argumentsList[i] ?? "";
    log = log.replaceAll(placeholders[i], tr(key) || key);
  }
  return log.trim();
}

export function formatGameEvent(payload: JsonObject, playerName: PlayerNameResolver): string {
  const request: JsonObject = {
    log_type: "",
    from_player: asString(payload.player_name),
    to_players: [],
    card_string: "",
    arguments: ["", "", "", "", ""]
  };
  switch (asNumber(payload.event, -1)) {
    case GameEvent.PLAYER_QUITDYING:
      request.log_type = "#QuitDying";
      break;
    case GameEvent.PLAYER_REFORM:
      request.log_type = "#PlayerReform";
      break;
    case GameEvent.CHANGE_HERO:
      if (asBool(payload.send_log))
        return "";
      request.log_type = "#ChangeHero";
      request.arguments = [asString(payload.general_name), "", "", "", ""];
      break;
    case GameEvent.HUASHEN:
      request.log_type = "#HuaShen";
      request.arguments = [
        asString(payload.skill_name),
        asString(payload.general_name),
        "", "", ""
      ];
      break;
    default:
      return "";
  }
  return formatSkillLog(request, playerName);
}

function stripMarkup(text: string): string {
  return text.replace(/<[^>]*>/g, "").trim();
}

export function formatPresentationEvent(
  event: PresentationEvent,
  playerName: PlayerNameResolver
): string {
  const payload = event.payload ?? {};
  switch (event.command) {
    case Command.LOG_SKILL:
      return formatSkillLog(payload, playerName);
    case Command.LOG_EVENT:
      return formatGameEvent(payload, playerName);
    case Command.SPEAK: {
      const said = stripMarkup(asString(payload.text));
      if (!said)
        return "";
      return `${playerName(asString(payload.speaker)) || asString(payload.speaker)}: ${said}`;
    }
    case Command.ANIMATE:
    case Command.SET_EMOTION:
      return "";
    default:
      return event.text;
  }
}
