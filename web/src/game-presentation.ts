import { asBool, asString, isObject, type JsonObject } from "./protocol";
import type { RulesEvaluation, RulesSelection } from "./rules-client";

export interface GameActionEntry {
  id: string;
  label: string;
  enabled: boolean;
  selected: boolean;
  reason: string;
}

// This mirrors GameActionModel::toJson. It is only a projection of the current
// native evaluation; it never makes a rule decision or creates a second draft.
export interface GameActionModel {
  session_generation: string;
  presentation_revision: string;
  request_id: string;
  request: JsonObject;
  supported: boolean;
  unsupported_reason: string;
  prompt: string;
  actions: GameActionEntry[];
  action_context: string;
  cards: GameActionEntry[];
  players: GameActionEntry[];
  skills: GameActionEntry[];
  top_cards: GameActionEntry[];
  bottom_cards: GameActionEntry[];
  arranging_cards: boolean;
  can_move_to_top: boolean;
  can_move_to_bottom: boolean;
  min_selection: number;
  max_selection: number;
  can_confirm: boolean;
  can_cancel: boolean;
  can_finish: boolean;
}

export interface GamePresentationEvent {
  generation: string;
  sequence: string;
  command: number;
  text: string;
}

export interface SharedPresentation {
  schema_version: 1;
  session_generation: string;
  presentation_revision: string;
  request_id: string;
  view_state: JsonObject;
  plain_text: string;
  events: GamePresentationEvent[];
  event_cursor: string;
}

function entry(id: string, label: string, enabled: boolean, selected: boolean, reason = ""): GameActionEntry {
  return { id, label, enabled, selected, reason };
}

function numericIds(value: unknown): number[] {
  return Array.isArray(value) ? value.filter((id): id is number => Number.isSafeInteger(id) && Number(id) >= 0) : [];
}

function stringList(value: unknown): string[] {
  return Array.isArray(value) ? value.filter((item): item is string => typeof item === "string" && item.length > 0) : [];
}

export function gameActionModel(evaluation: RulesEvaluation | null, selection: RulesSelection,
                                sessionGeneration = String(evaluation?.generation ?? 0),
                                presentationRevision = String(evaluation?.revision ?? 0)): GameActionModel | null {
  if (!evaluation) return null;
  const known = evaluation.known;
  const request: JsonObject = { ...evaluation.interaction, request_id: evaluation.request_id };
  const payload = isObject(request.payload) ? request.payload : {};
  const selectedCards = new Set(selection.card_ids);
  const selectedPlayers = new Set(selection.targets);
  const legalCards = new Set(evaluation.selectable_cards);
  const selectableCards = new Set<number>([...evaluation.selectable_cards, ...selection.card_ids]);
  const disclosed = numericIds(payload.visible_cards);
  const requestedCards = numericIds(payload.cards ?? payload.card_ids);
  const cardIds = disclosed.length ? disclosed : requestedCards.length ? requestedCards : [...selectableCards];
  const cards = cardIds.map((id) => {
    const enabled = known && legalCards.has(id);
    return entry(String(id), `卡牌 ${id}`, enabled, selectedCards.has(id), enabled ? "" : known ? "目前不能選擇此牌" : "規則尚未判定");
  });
  const legalPlayers = new Set(evaluation.next_targets.candidates);
  const requestedPlayers = stringList(payload.target_players ?? payload.players ?? payload.candidate_players);
  const playerNames = [...new Set([...evaluation.next_targets.candidates, ...requestedPlayers])];
  const players = playerNames.map((name) => {
    const enabled = known && legalPlayers.has(name);
    return entry(name, name, enabled, selectedPlayers.has(name),
      enabled ? "" : known ? "目前不能選擇此玩家" : "規則尚未判定");
  });
  const skills = evaluation.skills.map((skill) => {
    const id = JSON.stringify([skill.name, skill.instance_id]);
    const selected = skill.name === selection.skill_name && skill.instance_id === selection.skill_instance_id;
    return entry(id, skill.name, known && skill.available, selected,
      !known ? "規則尚未判定" : skill.available ? "" : skill.status);
  });
  const options = Array.isArray(payload.options) ? payload.options : [];
  const actions = options.flatMap((raw): GameActionEntry[] => {
    if (!isObject(raw)) return [];
    const id = asString(raw.value);
    if (!id) return [];
    const enabled = known && asBool(raw.enabled, true);
    return [entry(id, asString(raw.label) || id, enabled, id === selection.skill_name,
      enabled ? "" : known ? "目前不能選擇此項目" : "規則尚未判定")];
  });
  for (const declaration of evaluation.declarations) {
    actions.push(entry(declaration, declaration, known, declaration === selection.user_string,
      known ? "" : "規則尚未判定"));
  }
  const topCards = selection.top.map((id, index) => entry(String(id), `${index + 1}. 卡牌 ${id}`, known, false,
    known ? "" : "規則尚未判定"));
  const bottomCards = selection.bottom.map((id, index) => entry(String(id), `${index + 1}. 卡牌 ${id}`, known, false,
    known ? "" : "規則尚未判定"));
  const interactionType = asString(request.type);
  const arranging = interactionType === "skill_guanxing";
  return {
    session_generation: sessionGeneration,
    presentation_revision: presentationRevision,
    request_id: evaluation.request_id,
    request,
    supported: known,
    unsupported_reason: evaluation.reason || (known ? "" : "規則尚未判定"),
    prompt: asString(request.prompt),
    actions,
    action_context: arranging ? "rearrangement" : evaluation.declarations.length ? "declaration"
      : actions.length ? "request-options" : "rules-evaluation",
    cards,
    players,
    skills,
    top_cards: topCards,
    bottom_cards: bottomCards,
    arranging_cards: arranging,
    can_move_to_top: known && arranging && asString(payload.mode) !== "down_only",
    can_move_to_bottom: known && arranging && asString(payload.mode) !== "up_only",
    min_selection: evaluation.selection_min,
    max_selection: evaluation.selection_max,
    can_confirm: known && evaluation.can_confirm,
    can_cancel: known && asBool(request.cancelable),
    can_finish: known && interactionType === "play_card" && evaluation.can_confirm
      && selection.card_ids.length === 0 && selection.skill_name === ""
  };
}

export function isSharedPresentation(value: unknown): value is SharedPresentation {
  return isObject(value) && value.schema_version === 1
    && typeof value.session_generation === "string"
    && typeof value.presentation_revision === "string"
    && typeof value.request_id === "string"
    && isObject(value.view_state) && typeof value.plain_text === "string"
    && typeof value.event_cursor === "string" && Array.isArray(value.events)
    && value.events.every((event) => isObject(event)
      && typeof event.generation === "string" && typeof event.sequence === "string"
      && Number.isSafeInteger(event.command) && typeof event.text === "string");
}
