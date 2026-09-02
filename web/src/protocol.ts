export const PROTOCOL_VERSION = 2;
export const SELF_REFERENCE = "MG_SELF";
export const PLACE_HAND = 0;
export const PLACE_EQUIP = 1;
export const PLACE_DELAYED_TRICK = 2;
export const PLACE_JUDGE = 3;
export const PLACE_SPECIAL = 4;
export const PLACE_DISCARD = 5;
export const PLACE_DRAW = 6;
export const PLACE_TABLE = 7;
export const MAX_FRAME_BYTES = 65535;

export const Command = {
  UNKNOWN: 0,
  CHOOSE_CARD: 1,
  PLAY_CARD: 2,
  RESPONSE_CARD: 3,
  SHOW_CARD: 4,
  SHOW_ALL_CARDS: 5,
  EXCHANGE_CARD: 6,
  DISCARD_CARD: 7,
  INVOKE_SKILL: 8,
  MOVE_FOCUS: 9,
  CHOOSE_GENERAL: 10,
  CHOOSE_KINGDOM: 11,
  CHOOSE_SUIT: 12,
  CHOOSE_ROLE: 13,
  CHOOSE_ROLE_3V3: 14,
  CHOOSE_DIRECTION: 15,
  CHOOSE_PLAYER: 16,
  CHOOSE_ORDER: 17,
  ASK_PEACH: 18,
  SET_MARK: 19,
  SET_FLAG: 20,
  CARD_MARK: 21,
  CARD_FLAG: 22,
  NULLIFICATION: 23,
  MULTIPLE_CHOICE: 24,
  PINDIAN: 25,
  AMAZING_GRACE: 26,
  SKILL_YIJI: 27,
  SKILL_GUANXING: 28,
  SKILL_GONGXIN: 29,
  SET_PROPERTY: 30,
  CHANGE_HP: 31,
  CHANGE_MAXHP: 32,
  CHEAT: 33,
  SURRENDER: 34,
  ENABLE_SURRENDER: 35,
  GAME_OVER: 36,
  GAME_START: 37,
  MOVE_CARD: 38,
  GET_CARD: 39,
  LOSE_CARD: 40,
  LOG_EVENT: 41,
  LOG_SKILL: 42,
  UPDATE_CARD: 43,
  CARD_LIMITATION: 44,
  ADD_HISTORY: 45,
  SET_EMOTION: 46,
  FILL_AMAZING_GRACE: 47,
  CLEAR_AMAZING_GRACE: 48,
  TAKE_AMAZING_GRACE: 49,
  FIXED_DISTANCE: 50,
  ATTACK_RANGE: 51,
  KILL_PLAYER: 52,
  REVIVE_PLAYER: 53,
  ATTACH_SKILL: 54,
  NULLIFICATION_ASKED: 55,
  EXCHANGE_KNOWN_CARDS: 56,
  SET_KNOWN_CARDS: 57,
  UPDATE_PILE: 58,
  RESET_PILE: 59,
  SYNCHRONIZE_DISCARD_PILE: 60,
  UPDATE_BOSS_LEVEL: 61,
  UPDATE_STATE_ITEM: 62,
  PRESHOW: 63,
  SPEAK: 64,
  ASK_GENERAL: 65,
  ARRANGE_GENERAL: 66,
  FILL_GENERAL: 67,
  TAKE_GENERAL: 68,
  RECOVER_GENERAL: 69,
  REVEAL_GENERAL: 70,
  AVAILABLE_CARDS: 71,
  ANIMATE: 72,
  LUCK_CARD: 73,
  VIEW_GENERALS: 74,
  CHECK_VERSION: 75,
  SETUP: 76,
  NETWORK_DELAY_TEST: 77,
  ADD_PLAYER: 78,
  REMOVE_PLAYER: 79,
  START_IN_X_SECONDS: 80,
  ARRANGE_SEATS: 81,
  WARN: 82,
  TRUST: 83,
  PAUSE: 84,
  READY: 85,
  ADD_ROBOT: 86,
  SIGNUP: 87,
  UPDATE_SKILL: 88,
  ADD_ROUND: 89,
  CHANGE_TABLE_BG: 90,
  SKILL_DESCRIPTION_SWAP: 91,
  OPERATION_TIMEOUT: 92,
  WEAPON_RANGE: 93,
  PLAY_AUDIO: 94,
  ADD_EQUIP_AREA: 95,
  SET_EQUIP_AREA_COUNT: 96,
  UPDATE_CARD_DESC: 97,
  ADD_PLAYER_DYNAMIC: 98,
  TRIGGER_ORDER: 99,
  SHOW_VIRTUAL_CARD: 100,
  ANYTIME_SKILL: 101,
  ANYTIME_SKILL_DONE: 102,
  QML_INTERACT: 103,
  SET_SHOWN_HANDCARD: 104,
  SET_BROKEN_EQUIP: 105,
  MIRROR_GUANXING_STEP: 106,
  SWITCH_CONTEXT: 128,
  SYNC_PILE: 129,
  SKILL_INSTANCE: 130,
  CARD_PROVENANCE: 131,
  UPDATE_PLAYER_UI_STATE: 132,
  STATE_SYNC: 133
} as const;

export const GameEvent = {
  PLAYER_DYING: 0,
  PLAYER_QUITDYING: 1,
  PLAY_EFFECT: 2,
  JUDGE_RESULT: 3,
  DETACH_SKILL: 4,
  ACQUIRE_SKILL: 5,
  ADD_SKILL: 6,
  LOSE_SKILL: 7,
  PREPARE_SKILL: 8,
  UPDATE_SKILL: 9,
  HUASHEN: 10,
  CHANGE_GENDER: 11,
  CHANGE_HERO: 12,
  PLAYER_REFORM: 13,
  SKILL_INVOKED: 14,
  PAUSE: 15,
  REVEAL_PINDIAN: 16,
  CHANGE_BGM: 17
} as const;

export type CommandId = (typeof Command)[keyof typeof Command];
export type MessageType = "request" | "reply" | "notification";
export type Endpoint = "room" | "lobby" | "client";
export type JsonValue =
  | null
  | boolean
  | number
  | string
  | JsonValue[]
  | { [key: string]: JsonValue };
export type JsonObject = { [key: string]: JsonValue };

export interface ProtocolMessage {
  v: 2;
  type: MessageType;
  source: Endpoint;
  destination: Endpoint;
  message_id: string;
  reply_to?: string;
  command: number;
  payload: JsonObject;
}

export function encodeMessage(message: ProtocolMessage): string {
  const object: JsonObject = {
    v: PROTOCOL_VERSION,
    type: message.type,
    source: message.source,
    destination: message.destination,
    message_id: message.message_id,
    command: message.command,
    payload: message.payload
  };
  if (message.type === "reply") {
    if (!message.reply_to)
      throw new Error("reply_to is required");
    object.reply_to = message.reply_to;
  }
  const text = JSON.stringify(object);
  if (text.length > MAX_FRAME_BYTES)
    throw new Error("frame exceeds 65535 UTF-8 bytes");
  if (text.includes("\n") || text.includes("\r"))
    throw new Error("frame contains CR or LF");
  return text;
}

export function decodeMessage(text: string): ProtocolMessage {
  if (text.length > MAX_FRAME_BYTES)
    throw new Error("frame exceeds 65535 UTF-8 bytes");
  if (text.includes("\n") || text.includes("\r"))
    throw new Error("frame contains CR or LF");
  const value = JSON.parse(text) as JsonObject;
  if (value.v !== 2)
    throw new Error("v must be 2");
  const type = value.type;
  const source = value.source;
  const destination = value.destination;
  if (type !== "request" && type !== "reply" && type !== "notification")
    throw new Error("invalid type");
  if (source !== "room" && source !== "lobby" && source !== "client")
    throw new Error("invalid source");
  if (destination !== "room" && destination !== "lobby" && destination !== "client")
    throw new Error("invalid destination");
  if (typeof value.message_id !== "string" || !/^[1-9]\d*$/.test(value.message_id))
    throw new Error("message_id must be a positive decimal string");
  if (typeof value.command !== "number" || !Number.isInteger(value.command))
    throw new Error("command must be an integer");
  if (!isObject(value.payload))
    throw new Error("payload must be an object");
  const message: ProtocolMessage = {
    v: 2,
    type,
    source,
    destination,
    message_id: value.message_id,
    command: value.command,
    payload: value.payload
  };
  if (type === "reply") {
    if (typeof value.reply_to !== "string" || !/^[1-9]\d*$/.test(value.reply_to))
      throw new Error("reply_to must be a positive decimal string");
    message.reply_to = value.reply_to;
  }
  return message;
}

export function isObject(value: JsonValue | undefined): value is JsonObject {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

export function asString(value: JsonValue | undefined, fallback = ""): string {
  return typeof value === "string" ? value : fallback;
}

export function asNumber(value: JsonValue | undefined, fallback = 0): number {
  if (typeof value === "number" && Number.isFinite(value))
    return value;
  if (typeof value === "string" && value.trim() !== "") {
    const parsed = Number(value);
    if (Number.isFinite(parsed))
      return parsed;
  }
  return fallback;
}

export function asBool(value: JsonValue | undefined, fallback = false): boolean {
  if (typeof value === "boolean")
    return value;
  if (typeof value === "string") {
    const text = value.trim().toLowerCase();
    if (text === "true" || text === "1")
      return true;
    if (text === "false" || text === "0")
      return false;
  }
  return fallback;
}

export function asStringList(value: JsonValue | undefined): string[] {
  if (Array.isArray(value))
    return value.map((entry) => asString(entry));
  if (typeof value === "string" && value.length > 0)
    return [value];
  return [];
}

export function asNumberList(value: JsonValue | undefined): number[] {
  if (!Array.isArray(value))
    return [];
  return value.map((entry) => asNumber(entry));
}

export function nextId(counter: { value: bigint }): string {
  counter.value += 1n;
  return counter.value.toString(10);
}
