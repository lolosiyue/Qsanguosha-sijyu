import {
  asBool,
  asNumber,
  asString,
  asStringList,
  asNumberList,
  Command,
  SELF_REFERENCE,
  isObject,
  type JsonObject,
  type JsonValue
} from "./protocol";
import { ClientGameState, isRecord } from "./state";
import {
  autoTableBgUrl,
  imagePathToUrl,
  isLightbox,
  lightboxBackgroundUrl
} from "./backdrop";

export type FlowDisposition =
  | "state"
  | "presentation"
  | "session"
  | "text-irrelevant"
  | "unclassified";

export interface StateReduction {
  success: boolean;
  disposition: FlowDisposition;
  detail: string;
  eventText: string;
}

function strings(value: JsonValue | undefined): string[] {
  return asStringList(value);
}

function integers(value: JsonValue | undefined): number[] {
  return asNumberList(value);
}

function resolvePlayerName(state: ClientGameState, value: JsonValue | undefined): string {
  const name = asString(value);
  return name === SELF_REFERENCE ? state.selfName : name;
}

function adjustHandCount(state: ClientGameState, player: string, delta: number): void {
  if (!player || delta === 0)
    return;
  const count = Math.max(0, asNumber(state.playerValue(player, "hand_count")) + delta);
  state.setPlayerValue(player, "hand_count", count);
}

function applyCardMovement(state: ClientGameState, command: number, object: JsonObject): void {
  const moves = Array.isArray(object.moves) ? object.moves : [];
  for (const entry of moves) {
    if (!isObject(entry))
      continue;
    const fromPlayer = resolvePlayerName(state, entry.from_player);
    const owner = resolvePlayerName(state, entry.to_player);
    const fromPlace = asNumber(entry.from_place);
    const place = asNumber(entry.to_place);
    const pile = asString(entry.to_pile);
    const cardIds = integers(entry.card_ids);
    if (command === Command.LOSE_CARD && fromPlace === 0)
      adjustHandCount(state, fromPlayer, -cardIds.length);
    if (command === Command.GET_CARD && place === 0)
      adjustHandCount(state, owner, cardIds.length);
    for (const cardId of cardIds) {
      state.setCardValue(cardId, "owner", owner);
      state.setCardValue(cardId, "place", place);
      state.setCardValue(cardId, "pile", pile);
      state.setCardValue(cardId, "open", entry.open ?? false);
    }
  }
}

function appendOrRemove(values: string[], value: string, add: boolean): string[] {
  if (add && !values.includes(value))
    values.push(value);
  else if (!add)
    return values.filter((item) => item !== value);
  return values;
}

function appendSkill(state: ClientGameState, player: string, skill: string): void {
  if (!player || !skill)
    return;
  const skills = strings(state.playerValue(player, "skills"));
  if (!skills.includes(skill))
    skills.push(skill);
  state.setPlayerValue(player, "skills", skills);
}

function removeSkill(state: ClientGameState, player: string, skill: string): void {
  if (!player || !skill)
    return;
  state.setPlayerValue(
    player,
    "skills",
    strings(state.playerValue(player, "skills")).filter((item) => item !== skill)
  );
}

function skillInstanceKey(skill: string, instanceId: number): string {
  return `${skill}#${instanceId}`;
}

function storeSkillInstance(state: ClientGameState, entry: JsonObject): void {
  const player = resolvePlayerName(state, entry.owner_name);
  const skill = asString(entry.skill_name);
  const instanceId = asNumber(entry.instance_id);
  if (!player || !skill || instanceId <= 0)
    return;
  const instances = isRecord(state.playerValue(player, "skill_instances"))
    ? { ...state.playerValue(player, "skill_instances") as JsonObject }
    : {};
  const stored = { ...entry, owner_name: player };
  instances[skillInstanceKey(skill, instanceId)] = stored;
  state.setPlayerValue(player, "skill_instances", instances);
  if (asBool(entry.visible, true))
    appendSkill(state, player, skill);
}

function setVisibleCards(
  state: ClientGameState,
  player: string,
  cardIds: JsonValue | undefined,
  visibility: string
): void {
  const cards = integers(cardIds);
  state.setPlayerValue(player, visibility, cards);
  for (const cardId of cards) {
    state.setCardValue(cardId, "owner", player);
    state.setCardValue(cardId, visibility, true);
  }
}

function applyPlayerProperty(state: ClientGameState, object: JsonObject): void {
  const action = asString(object.action);
  let player = resolvePlayerName(state, object.player_name);
  if (action === "tag") {
    const tag = asString(object.tag_name);
    if (!player || !tag)
      return;
    const tags = isRecord(state.playerValue(player, "tags"))
      ? { ...state.playerValue(player, "tags") as JsonObject }
      : {};
    if (asString(object.value_kind) === "removed")
      delete tags[tag];
    else
      tags[tag] = object.value ?? null;
    state.setPlayerValue(player, "tags", tags);
    return;
  }
  if (action === "general_pile") {
    const pile = asString(object.pile_name);
    if (!player || !pile)
      return;
    const piles = isRecord(state.playerValue(player, "general_piles"))
      ? { ...state.playerValue(player, "general_piles") as JsonObject }
      : {};
    let generals = strings(piles[pile]);
    for (const general of strings(object.general_names))
      generals = appendOrRemove(generals, general, asBool(object.add));
    piles[pile] = generals;
    state.setPlayerValue(player, "general_piles", piles);
    return;
  }
  if (action !== "property")
    return;
  const property = asString(object.property_name);
  const value = object.string_value;
  if (property === "objectName") {
    const objectName = asString(value);
    if (objectName) {
      if (asString(object.player_name) === SELF_REFERENCE)
        state.setSelfName(objectName);
      player = objectName;
    }
  }
  if (!player || !property)
    return;

  let stateKey = property;
  if (property === "objectName")
    stateKey = "object_name";
  else if (property === "maxhp")
    stateKey = "max_hp";
  else if (property === "general2")
    stateKey = "deputy_general";
  else if (property === "player_seat")
    stateKey = "seat";
  else if (property === "handcard_num")
    stateKey = "hand_count";

  const integerProperties = new Set(["hp", "maxhp", "seat", "player_seat", "phase", "handcard_num"]);
  const booleanProperties = new Set([
    "alive", "chained", "faceup", "removed", "owner", "hasjudgearea", "RestPlayer"
  ]);
  let projected: JsonValue = value ?? "";
  if (integerProperties.has(property))
    projected = asNumber(value);
  else if (booleanProperties.has(property))
    projected = asBool(value);

  if (property === "flags") {
    let flags = strings(state.playerValue(player, "flags"));
    const flag = asString(value);
    const remove = flag.startsWith("-");
    flags = appendOrRemove(flags, remove ? flag.slice(1) : flag, !remove);
    state.setPlayerValue(player, "flags", flags);
    if (flag === "CurrentPlayer")
      state.setGameValue("current_player", player);
    else if (flag === "-CurrentPlayer" && asString(state.gameValue("current_player")) === player)
      state.setGameValue("current_player", "");
    return;
  }

  state.setPlayerValue(player, stateKey, projected);
  if (property === "phase") {
    state.setGameValue("current_phase", projected);
    state.setGameValue("current_player", player);
  }
}

const STATE_COMMANDS = new Set<number>([
  Command.ADD_PLAYER, Command.ADD_PLAYER_DYNAMIC, Command.REMOVE_PLAYER,
  Command.START_IN_X_SECONDS, Command.ARRANGE_SEATS, Command.GAME_START,
  Command.GAME_OVER, Command.CHANGE_HP, Command.CHANGE_MAXHP, Command.KILL_PLAYER,
  Command.REVIVE_PLAYER, Command.SHOW_CARD, Command.SHOW_VIRTUAL_CARD,
  Command.CARD_PROVENANCE, Command.UPDATE_PLAYER_UI_STATE, Command.UPDATE_CARD,
  Command.SET_MARK, Command.ATTACH_SKILL, Command.SKILL_INSTANCE, Command.MOVE_FOCUS,
  Command.SHOW_ALL_CARDS, Command.SKILL_GONGXIN, Command.ADD_HISTORY,
  Command.FIXED_DISTANCE, Command.ATTACK_RANGE, Command.CARD_LIMITATION,
  Command.NULLIFICATION_ASKED, Command.ENABLE_SURRENDER, Command.EXCHANGE_KNOWN_CARDS,
  Command.SET_KNOWN_CARDS, Command.SWITCH_CONTEXT, Command.VIEW_GENERALS,
  Command.UPDATE_BOSS_LEVEL, Command.UPDATE_STATE_ITEM, Command.AVAILABLE_CARDS,
  Command.GET_CARD, Command.LOSE_CARD, Command.SET_PROPERTY, Command.RESET_PILE,
  Command.UPDATE_PILE, Command.SYNCHRONIZE_DISCARD_PILE, Command.SYNC_PILE,
  Command.CARD_MARK, Command.CARD_FLAG, Command.WEAPON_RANGE,
  Command.FILL_AMAZING_GRACE, Command.TAKE_AMAZING_GRACE, Command.CLEAR_AMAZING_GRACE,
  Command.FILL_GENERAL, Command.TAKE_GENERAL, Command.RECOVER_GENERAL,
  Command.REVEAL_GENERAL, Command.UPDATE_SKILL, Command.ADD_ROUND,
  Command.SKILL_DESCRIPTION_SWAP, Command.ADD_EQUIP_AREA, Command.SET_EQUIP_AREA_COUNT,
  Command.UPDATE_CARD_DESC, Command.ANYTIME_SKILL_DONE, Command.SET_SHOWN_HANDCARD,
  Command.SET_BROKEN_EQUIP, Command.PRESHOW, Command.MIRROR_GUANXING_STEP
]);

export function classifyNotification(command: number): FlowDisposition {
  if (command === Command.NETWORK_DELAY_TEST
      || command === Command.OPERATION_TIMEOUT
      || command === Command.STATE_SYNC)
    return "session";
  if (command === Command.WARN || command === Command.SPEAK
      || command === Command.LOG_SKILL || command === Command.LOG_EVENT
      || command === Command.SET_EMOTION || command === Command.CHANGE_TABLE_BG
      || command === Command.INVOKE_SKILL)
    return "presentation";
  if (command === Command.ANIMATE || command === Command.PLAY_AUDIO)
    return "text-irrelevant";
  if (STATE_COMMANDS.has(command))
    return "state";
  return "unclassified";
}

export function applyNotification(
  state: ClientGameState,
  command: number,
  payload: JsonValue
): StateReduction {
  const result: StateReduction = {
    success: false,
    disposition: classifyNotification(command),
    detail: "",
    eventText: ""
  };
  if (result.disposition === "unclassified") {
    result.detail = `unclassified room notification ${command}`;
    return result;
  }
  if (!isObject(payload)) {
    result.detail = `room notification ${command} payload is not a typed object`;
    return result;
  }
  const schemaVersion = asNumber(payload.schema_version, -1);
  if (schemaVersion <= 0) {
    result.detail = `room notification ${command} has no valid schema_version`;
    return result;
  }
  state.recordFlow(command, payload);
  if (result.disposition === "presentation" || result.disposition === "text-irrelevant") {
    if (command === Command.SPEAK)
      result.eventText = `${asString(payload.speaker)}: ${asString(payload.text)}`;
    else if (command === Command.WARN)
      result.eventText = asString(payload.message);
    else if (command === Command.LOG_SKILL)
      result.eventText = `${asString(payload.log_type)} ${asString(payload.from_player)}`;
    else if (command === Command.SET_EMOTION)
      result.eventText = "";
    else if (command === Command.INVOKE_SKILL)
      result.eventText = `${asString(payload.player_name)} invoked ${asString(payload.skill_name)}`;
    else if (command === Command.LOG_EVENT)
      result.eventText = asString(payload.skill_name) || `event ${asNumber(payload.event)}`;
    else
      result.eventText = "";
    if (command === Command.CHANGE_TABLE_BG) {
      const path = imagePathToUrl(asString(payload.path));
      if (path) {
        state.setGameValue("table_bg", path);
        state.setGameValue("table_bg_locked", true);
      }
    }
    if (command === Command.ANIMATE && isLightbox(asNumber(payload.animation))) {
      const next = lightboxBackgroundUrl(asString(payload.first_argument));
      if (next) {
        state.setGameValue("table_bg", next);
        state.setGameValue("table_bg_locked", true);
      }
    }
    if (result.eventText)
      state.appendPresentationEvent(command, result.eventText, payload);
    result.success = true;
    return result;
  }

  switch (command) {
    case Command.ADD_PLAYER:
    case Command.ADD_PLAYER_DYNAMIC: {
      const name = resolvePlayerName(state, payload.player_name);
      state.addPlayer(name);
      state.setPlayerValue(name, "screen_name", asString(payload.screen_name));
      state.setPlayerValue(name, "avatar", asString(payload.avatar));
      break;
    }
    case Command.REMOVE_PLAYER:
      state.removePlayer(resolvePlayerName(state, payload.player_name));
      break;
    case Command.ARRANGE_SEATS: {
      const names = strings(payload.player_names);
      state.setPlayerNames(names);
      names.forEach((name, index) => state.setPlayerValue(name, "seat", index + 1));
      break;
    }
    case Command.START_IN_X_SECONDS:
      state.setGameValue("starts_in_seconds", asNumber(payload.seconds));
      break;
    case Command.GAME_START:
      state.setGameValue("started", true);
      state.setGameValue("game_over", false);
      state.setGameValue("status", "active");
      state.setGameValue("draw_pile", payload.card_ids ?? []);
      state.setGameValue("draw_pile_count", integers(payload.card_ids).length);
      state.setGameValue("table_bg", autoTableBgUrl(state));
      state.setGameValue("table_bg_locked", false);
      break;
    case Command.GAME_OVER:
      state.setGameValue("game_over", true);
      state.setGameValue("status", "game_over");
      state.setGameValue("result", payload);
      break;
    case Command.CHANGE_HP: {
      const name = resolvePlayerName(state, payload.player_name);
      state.setPlayerValue(name, "hp", asNumber(state.playerValue(name, "hp")) + asNumber(payload.delta));
      break;
    }
    case Command.CHANGE_MAXHP: {
      const name = resolvePlayerName(state, payload.player_name);
      state.setPlayerValue(
        name, "max_hp", asNumber(state.playerValue(name, "max_hp")) + asNumber(payload.delta)
      );
      break;
    }
    case Command.KILL_PLAYER:
      state.setPlayerAlive(resolvePlayerName(state, payload.player_name), false);
      break;
    case Command.REVIVE_PLAYER:
      state.setPlayerAlive(resolvePlayerName(state, payload.player_name), true);
      break;
    case Command.SET_MARK:
      state.setPlayerMark(
        resolvePlayerName(state, payload.player_name),
        asString(payload.mark_name),
        asNumber(payload.value)
      );
      break;
    case Command.SHOW_CARD:
    case Command.SHOW_ALL_CARDS:
      setVisibleCards(state, resolvePlayerName(state, payload.player_name), payload.card_ids, "shown_cards");
      break;
    case Command.SHOW_VIRTUAL_CARD:
      state.setGameValue("last_virtual_card", payload);
      break;
    case Command.CARD_PROVENANCE:
      state.setGameValue("last_card_provenance", payload);
      break;
    case Command.SKILL_GONGXIN:
      state.setGameValue("gongxin", payload);
      break;
    case Command.UPDATE_PLAYER_UI_STATE: {
      const player = resolvePlayerName(state, payload.player_name);
      const uiState = isRecord(payload.state) ? payload.state : {};
      state.setPlayerValue(player, "ui_state", uiState);
      state.setPlayerValue(player, "hand_max", uiState.handMax ?? 0);
      state.setPlayerValue(player, "offensive_distance", uiState.offensiveDistance ?? 0);
      state.setPlayerValue(player, "defensive_distance", uiState.defensiveDistance ?? 0);
      for (const field of ["maxCardsSkills", "offensiveSkills", "defensiveSkills", "viewAsEquipSkills"])
        for (const skill of strings(uiState[field]))
          appendSkill(state, player, skill);
      break;
    }
    case Command.ATTACH_SKILL:
      appendSkill(state, resolvePlayerName(state, payload.player_name), asString(payload.skill_name));
      break;
    case Command.SKILL_INSTANCE: {
      const action = asString(payload.action);
      if (action === "snapshot") {
        for (const player of state.playerNames)
          state.setPlayerValue(player, "skill_instances", {});
        const entries = Array.isArray(payload.entries) ? payload.entries : [];
        for (const entry of entries)
          if (isObject(entry))
            storeSkillInstance(state, entry);
      } else if (action === "upsert" && isObject(payload.entry)) {
        storeSkillInstance(state, payload.entry);
      } else {
        const player = resolvePlayerName(state, payload.owner_name);
        const skill = asString(payload.skill_name);
        const instanceId = asNumber(payload.instance_id);
        const instances = isRecord(state.playerValue(player, "skill_instances"))
          ? { ...state.playerValue(player, "skill_instances") as JsonObject }
          : {};
        const key = skillInstanceKey(skill, instanceId);
        if (action === "remove") {
          delete instances[key];
          const stillPresent = Object.values(instances).some(
            (value) => isObject(value) && asString(value.skill_name) === skill
          );
          if (!stillPresent)
            removeSkill(state, player, skill);
        } else {
          const entry = isRecord(instances[key]) ? { ...instances[key] } : {};
          if (action === "amount") {
            entry.has_amount_override = payload.has_amount_override ?? false;
            entry.amount = payload.amount ?? 0;
          } else if (action === "correct_state" || action === "state") {
            const field = action === "state" ? "state" : "correct_state";
            let values = isRecord(entry[field]) ? { ...entry[field] } : {};
            const operation = asString(payload.operation);
            const stateKey = asString(payload.key);
            if (operation === "clear")
              values = {};
            else if (operation === "replace" && isObject(payload.value))
              values = { ...payload.value };
            else if (operation === "remove")
              delete values[stateKey];
            else
              values[stateKey] = payload.value ?? null;
            entry[field] = values;
          }
          if (Object.keys(entry).length > 0)
            instances[key] = entry;
        }
        state.setPlayerValue(player, "skill_instances", instances);
      }
      break;
    }
    case Command.SET_PROPERTY:
      applyPlayerProperty(state, payload);
      if (asBool(state.gameValue("started"))
          && !asBool(state.gameValue("table_bg_locked"))) {
        const property = asString(payload.property_name);
        if (property === "kingdom" || property === "role")
          state.setGameValue("table_bg", autoTableBgUrl(state));
      }
      break;
    case Command.GET_CARD:
    case Command.LOSE_CARD:
      applyCardMovement(state, command, payload);
      break;
    case Command.UPDATE_CARD: {
      const cardId = asNumber(payload.card_id);
      if (asString(payload.action) === "reset") {
        state.setCardValue(cardId, "modified", false);
      } else {
        for (const [key, value] of Object.entries(payload))
          state.setCardValue(cardId, key, value);
        state.setCardValue(cardId, "modified", true);
      }
      break;
    }
    case Command.CARD_MARK: {
      const cardId = asNumber(payload.card_id);
      const marks = isRecord(state.card(cardId)?.marks)
        ? { ...state.card(cardId)!.marks as JsonObject }
        : {};
      marks[asString(payload.mark_name)] = payload.value ?? 0;
      state.setCardValue(cardId, "marks", marks);
      break;
    }
    case Command.CARD_FLAG: {
      const cardId = asNumber(payload.card_id);
      let flags = strings(state.card(cardId)?.flags);
      const flag = asString(payload.flag);
      flags = appendOrRemove(flags, flag.startsWith("-") ? flag.slice(1) : flag, !flag.startsWith("-"));
      state.setCardValue(cardId, "flags", flags);
      break;
    }
    case Command.UPDATE_PILE:
      state.setGameValue("draw_pile_count", asNumber(payload.count));
      break;
    case Command.RESET_PILE:
      state.setGameValue("swap_count", asNumber(payload.swap_count));
      break;
    case Command.SYNCHRONIZE_DISCARD_PILE:
      state.setGameValue("discard_pile", payload.card_ids ?? []);
      break;
    case Command.SYNC_PILE: {
      const player = resolvePlayerName(state, payload.player_name);
      const piles = isRecord(state.playerValue(player, "piles"))
        ? { ...state.playerValue(player, "piles") as JsonObject }
        : {};
      piles[asString(payload.pile_name)] = payload.card_ids ?? [];
      state.setPlayerValue(player, "piles", piles);
      break;
    }
    case Command.SET_KNOWN_CARDS:
      setVisibleCards(state, resolvePlayerName(state, payload.player_name), payload.card_ids, "known_cards");
      break;
    case Command.EXCHANGE_KNOWN_CARDS: {
      const first = resolvePlayerName(state, payload.first_player);
      const second = resolvePlayerName(state, payload.second_player);
      const firstCards = state.playerValue(first, "known_cards") ?? [];
      state.setPlayerValue(first, "known_cards", state.playerValue(second, "known_cards") ?? []);
      state.setPlayerValue(second, "known_cards", firstCards);
      break;
    }
    case Command.SET_SHOWN_HANDCARD:
      setVisibleCards(state, resolvePlayerName(state, payload.player_name), payload.card_ids, "shown_hand_cards");
      break;
    case Command.SET_BROKEN_EQUIP:
      state.setPlayerValue(
        resolvePlayerName(state, payload.player_name),
        "broken_equipment",
        payload.card_ids ?? []
      );
      break;
    case Command.MOVE_FOCUS: {
      const players = strings(payload.player_names).map((name) => resolvePlayerName(state, name));
      state.setGameValue("focus", players);
      state.setGameValue("focus_countdown", payload.countdown ?? null);
      break;
    }
    case Command.ADD_HISTORY: {
      let player = resolvePlayerName(state, payload.player_name);
      if (!player)
        player = state.selfName;
      const history = isRecord(state.playerValue(player, "history"))
        ? { ...state.playerValue(player, "history") as JsonObject }
        : {};
      const name = asString(payload.history_name);
      const times = asNumber(payload.times);
      if (name === ".")
        Object.keys(history).forEach((key) => delete history[key]);
      else if (times === 0)
        delete history[name];
      else
        history[name] = asNumber(history[name]) + times;
      state.setPlayerValue(player, "history", history);
      break;
    }
    case Command.FIXED_DISTANCE: {
      const from = resolvePlayerName(state, payload.from_player);
      const to = resolvePlayerName(state, payload.to_player);
      const distances = isRecord(state.playerValue(from, "fixed_distances"))
        ? { ...state.playerValue(from, "fixed_distances") as JsonObject }
        : {};
      if (asBool(payload.set))
        distances[to] = asNumber(payload.distance);
      else
        delete distances[to];
      state.setPlayerValue(from, "fixed_distances", distances);
      break;
    }
    case Command.ATTACK_RANGE: {
      const from = resolvePlayerName(state, payload.from_player);
      const to = resolvePlayerName(state, payload.to_player);
      const pairs = appendOrRemove(
        strings(state.playerValue(from, "attack_range_pairs")),
        to,
        asBool(payload.set)
      );
      state.setPlayerValue(from, "attack_range_pairs", pairs);
      break;
    }
    case Command.CARD_LIMITATION: {
      const player = state.selfName;
      let limitations = Array.isArray(state.playerValue(player, "card_limitations"))
        ? [...state.playerValue(player, "card_limitations") as JsonValue[]]
        : [];
      const action = asString(payload.action);
      if (action === "clear") {
        if (!asBool(payload.single_turn))
          limitations = [];
        else
          limitations = limitations.filter(
            (item) => !(isObject(item) && asBool(item.single_turn))
          );
      } else if (action === "remove_by_reason") {
        const reason = asString(payload.reason);
        limitations = limitations.filter(
          (item) => !(isObject(item) && asString(item.reason) === reason)
        );
      } else {
        const entry = { ...payload };
        delete entry.schema_version;
        delete entry.action;
        const matches = (value: JsonValue) => {
          if (!isObject(value))
            return false;
          const candidate = { ...value };
          delete candidate.schema_version;
          delete candidate.action;
          return JSON.stringify(candidate) === JSON.stringify(entry);
        };
        if (action === "set" && !limitations.some(matches))
          limitations.push(entry);
        else if (action === "remove")
          limitations = limitations.filter((item) => !matches(item));
      }
      state.setPlayerValue(player, "card_limitations", limitations);
      break;
    }
    case Command.NULLIFICATION_ASKED:
      state.setGameValue("nullification_trick", asString(payload.trick_name));
      break;
    case Command.ENABLE_SURRENDER:
      state.setGameValue("surrender_enabled", asBool(payload.enabled));
      break;
    case Command.OPERATION_TIMEOUT:
      state.setGameValue("operation_timeout_ms", asNumber(payload.timeout_ms));
      break;
    case Command.SWITCH_CONTEXT:
      state.setGameValue("current_player", resolvePlayerName(state, payload.player_name));
      break;
    case Command.VIEW_GENERALS:
      state.setGameValue("view_generals", payload);
      break;
    case Command.FILL_AMAZING_GRACE:
      state.setGameValue("amazing_grace", payload);
      break;
    case Command.TAKE_AMAZING_GRACE: {
      state.setGameValue("last_amazing_grace_take", payload);
      const grace = isRecord(state.gameValue("amazing_grace"))
        ? { ...state.gameValue("amazing_grace") as JsonObject }
        : {};
      const cards = integers(grace.card_ids).filter((id) => id !== asNumber(payload.card_id));
      grace.card_ids = cards;
      state.setGameValue("amazing_grace", grace);
      break;
    }
    case Command.CLEAR_AMAZING_GRACE:
      state.setGameValue("amazing_grace", {});
      break;
    case Command.FILL_GENERAL:
      state.setGameValue("general_pool", payload.general_names ?? []);
      break;
    case Command.TAKE_GENERAL: {
      const pool = strings(state.gameValue("general_pool")).filter(
        (name) => name !== asString(payload.general_name)
      );
      state.setGameValue("general_pool", pool);
      state.setGameValue("last_general_take", payload);
      break;
    }
    case Command.RECOVER_GENERAL: {
      const pool = strings(state.gameValue("general_pool"));
      const index = asNumber(payload.index);
      if (index >= 0 && index < pool.length)
        pool[index] = asString(payload.general_name);
      else
        pool.push(asString(payload.general_name));
      state.setGameValue("general_pool", pool);
      break;
    }
    case Command.REVEAL_GENERAL:
      state.setPlayerValue(
        resolvePlayerName(state, payload.player_name),
        "revealed_general",
        asString(payload.general_name)
      );
      break;
    case Command.UPDATE_SKILL:
      state.setGameValue("last_updated_skill", asString(payload.skill_name));
      break;
    case Command.ADD_ROUND:
      state.setGameValue("round", asNumber(state.gameValue("round")) + 1);
      break;
    case Command.AVAILABLE_CARDS:
      state.setGameValue("available_cards", payload.card_ids ?? []);
      break;
    case Command.UPDATE_STATE_ITEM:
      state.setGameValue("state_item", payload.state ?? null);
      break;
    case Command.UPDATE_BOSS_LEVEL:
      state.setGameValue("boss_level", asNumber(payload.level));
      break;
    case Command.SKILL_DESCRIPTION_SWAP: {
      const player = resolvePlayerName(state, payload.player_name);
      const descriptions = isRecord(state.playerValue(player, "skill_descriptions"))
        ? { ...state.playerValue(player, "skill_descriptions") as JsonObject }
        : {};
      const skillKey = skillInstanceKey(asString(payload.skill_name), asNumber(payload.instance_id));
      const values = isRecord(descriptions[skillKey]) ? { ...descriptions[skillKey] } : {};
      values[asString(payload.key)] = payload.value ?? null;
      descriptions[skillKey] = values;
      state.setPlayerValue(player, "skill_descriptions", descriptions);
      break;
    }
    case Command.ADD_EQUIP_AREA: {
      const player = resolvePlayerName(state, payload.player_name);
      const areas = isRecord(state.playerValue(player, "equip_areas"))
        ? { ...state.playerValue(player, "equip_areas") as JsonObject }
        : {};
      areas[asString(payload.area)] = 1;
      state.setPlayerValue(player, "equip_areas", areas);
      break;
    }
    case Command.SET_EQUIP_AREA_COUNT: {
      const player = resolvePlayerName(state, payload.player_name);
      const areas = isRecord(state.playerValue(player, "equip_areas"))
        ? { ...state.playerValue(player, "equip_areas") as JsonObject }
        : {};
      areas[asString(payload.area)] = asNumber(payload.count);
      state.setPlayerValue(player, "equip_areas", areas);
      break;
    }
    case Command.UPDATE_CARD_DESC: {
      const player = resolvePlayerName(state, payload.player_name);
      const descriptions = isRecord(state.playerValue(player, "card_descriptions"))
        ? { ...state.playerValue(player, "card_descriptions") as JsonObject }
        : {};
      const cardName = asString(payload.card_name);
      const values = isRecord(descriptions[cardName]) ? { ...descriptions[cardName] } : {};
      values[asString(payload.key)] = payload.value ?? null;
      descriptions[cardName] = values;
      state.setPlayerValue(player, "card_descriptions", descriptions);
      break;
    }
    case Command.ANYTIME_SKILL_DONE: {
      const pending = strings(state.playerValue(state.selfName, "pending_anytime_skills"))
        .filter((name) => name !== asString(payload.skill_name));
      state.setPlayerValue(state.selfName, "pending_anytime_skills", pending);
      break;
    }
    case Command.WEAPON_RANGE: {
      const ranges = isRecord(state.gameValue("weapon_ranges"))
        ? { ...state.gameValue("weapon_ranges") as JsonObject }
        : {};
      ranges[asString(payload.weapon_name)] = asNumber(payload.range);
      state.setGameValue("weapon_ranges", ranges);
      break;
    }
    case Command.MIRROR_GUANXING_STEP: {
      const action = asString(payload.action);
      if (action === "finish") {
        state.setGameValue("guanxing", {});
      } else {
        let guanxing = isRecord(state.gameValue("guanxing"))
          ? { ...state.gameValue("guanxing") as JsonObject }
          : {};
        if (action === "start") {
          guanxing = { ...payload, moves: [] };
        } else if (action === "move") {
          const moves = Array.isArray(guanxing.moves) ? [...guanxing.moves] : [];
          moves.push(payload);
          guanxing.moves = moves;
        }
        state.setGameValue("guanxing", guanxing);
      }
      break;
    }
    case Command.PRESHOW:
      state.setPlayerValue(
        resolvePlayerName(state, payload.player_name),
        "preshow",
        payload.states ?? {}
      );
      break;
    case Command.STATE_SYNC:
      state.setConnectionValue("sync_id", asString(payload.sync_id));
      state.setConnectionValue("sync_phase", asString(payload.phase));
      break;
    case Command.NETWORK_DELAY_TEST:
      state.setConnectionValue("delay_nonce", asString(payload.nonce));
      break;
    default:
      break;
  }

  result.success = true;
  return result;
}
