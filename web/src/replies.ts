import { Command, type JsonObject } from "./protocol";
import type { RulesSelection } from "./rules-client";

export type ResponseIntent = JsonObject;

export function optionReply(_command: number, _field: string, value: string): ResponseIntent {
  return { kind: "option", payload: { value } };
}

export function cancelReply(): JsonObject {
  return { kind: "cancel", payload: {} };
}

export function cardIdReply(cardId: number): JsonObject {
  return cardIdsReply([cardId]);
}

export function cardIdsReply(cardIds: number[]): JsonObject {
  return cardResponseReply(cardIds, []);
}

// This is the native evaluator's draft, not CardSelectionData or a wire reply.
// Preserve order, repeated target votes and the selected declaration verbatim.
export function cardsIntent(selection: RulesSelection): ResponseIntent {
  return { kind: "cards", card_ids: [...selection.card_ids], targets: [...selection.targets],
    skill_name: selection.skill_name, skill_instance_id: selection.skill_instance_id,
    user_string: selection.user_string, top: [...selection.top], bottom: [...selection.bottom] };
}

export function cardResponseReply(
  cardIds: number[],
  targets: string[],
  skillName = "",
  instanceId = 0,
  userString = ""
): JsonObject {
  return cardsIntent({ card_ids: cardIds, targets, skill_name: skillName,
    skill_instance_id: instanceId, user_string: userString, top: [], bottom: [] });
}

export function assignmentReply(assignments: Record<string, string>): JsonObject {
  const players = Object.keys(assignments);
  return { kind: "assignment", payload: { names: players, values: players.map((player) => assignments[player] ?? "") } };
}

export function yijiReply(cardIds: number[], target: string): JsonObject {
  return { kind: "distribution", payload: { cards: cardIds, target } };
}

export function guanxingReply(top: number[], bottom: number[]): JsonObject {
  return { kind: "rearrangement", payload: { first: top, second: bottom } };
}

export function playersReply(players: string[]): JsonObject {
  return { kind: "players", payload: { players } };
}

export function arrangeReply(generals: string[]): JsonObject {
  return { kind: "general_arrangement", payload: { generals } };
}

export function qmlReply(value: JsonObject | null): JsonObject {
  if (value === null)
    return { kind: "custom", payload: { schema_version: 1, type: "qml", value: null } };
  return { kind: "custom", payload: { schema_version: 1, type: "qml", value } };
}

export function responseIntent(
  command: number,
  input: {
    cancelled?: boolean;
    option?: string;
    bool?: boolean;
    int?: number;
    cardId?: number;
    cardIds?: number[];
    // Legacy callers may supply this, but only native code constructs a card.
    cardText?: string;
    targets?: string[];
    skillName?: string;
    instanceId?: number;
    userString?: string;
    assignments?: Record<string, string>;
    yijiTarget?: string;
    top?: number[];
    bottom?: number[];
    players?: string[];
    generals?: string[];
    qml?: JsonObject | null;
  }
): JsonObject {
  // Cancellation semantics belong to ClientCore and its reply encoder.
  if (input.cancelled)
    return cancelReply();
  switch (command) {
    case Command.CHOOSE_GENERAL:
    case Command.ASK_GENERAL:
      return optionReply(command, "general", input.option ?? "");
    case Command.CHOOSE_DIRECTION:
      return optionReply(command, "direction", input.option ?? "");
    case Command.MULTIPLE_CHOICE:
      return optionReply(command, "choice", input.option ?? "");
    case Command.CHOOSE_SUIT:
      return optionReply(command, "suit", input.option ?? "");
    case Command.CHOOSE_KINGDOM:
      return optionReply(command, "kingdom", input.option ?? "");
    case Command.TRIGGER_ORDER:
      return optionReply(command, "trigger", input.option ?? "");
    case Command.CHOOSE_ROLE_3V3:
      return optionReply(command, "role", input.option ?? "");
    case Command.CHOOSE_ORDER:
      return optionReply(command, "order", input.option ?? String(input.int ?? 0));
    case Command.INVOKE_SKILL:
      return optionReply(command, "invoke", input.option ?? (input.bool === false ? "no" : "yes"));
    case Command.SURRENDER:
      return optionReply(command, "surrender", input.option ?? (input.bool === false ? "no" : "yes"));
    case Command.LUCK_CARD:
      return optionReply(command, "use_luck_card", input.option ?? (input.bool === false ? "no" : "yes"));
    case Command.CHOOSE_ROLE:
      return assignmentReply(input.assignments ?? {});
    case Command.SKILL_GONGXIN:
    case Command.AMAZING_GRACE:
    case Command.CHOOSE_CARD:
      return cardIdsReply(input.cardIds ?? (input.cardId === undefined ? [] : [input.cardId]));
    case Command.EXCHANGE_CARD:
    case Command.DISCARD_CARD:
      return cardIdsReply(input.cardIds ?? []);
    case Command.PLAY_CARD:
    case Command.RESPONSE_CARD:
    case Command.ASK_PEACH:
    case Command.NULLIFICATION:
    case Command.SHOW_CARD:
    case Command.PINDIAN:
      return cardResponseReply(
        input.cardIds ?? [],
        input.targets ?? [],
        input.skillName ?? "",
        input.instanceId ?? 0,
        input.userString ?? ""
      );
    case Command.SKILL_YIJI:
      return yijiReply(input.cardIds ?? [], input.yijiTarget ?? "");
    case Command.SKILL_GUANXING:
      return guanxingReply(input.top ?? [], input.bottom ?? []);
    case Command.CHOOSE_PLAYER:
      return playersReply(input.players ?? []);
    case Command.ARRANGE_GENERAL:
      return arrangeReply(input.generals ?? []);
    case Command.QML_INTERACT:
      return qmlReply(input.qml ?? null);
    default:
      throw new Error(`unsupported interaction command ${command}`);
  }
}
