import { Command, type JsonObject } from "./protocol";

export const INTERACTION_COMMANDS = [
  Command.CHOOSE_CARD,
  Command.PLAY_CARD,
  Command.RESPONSE_CARD,
  Command.SHOW_CARD,
  Command.EXCHANGE_CARD,
  Command.DISCARD_CARD,
  Command.INVOKE_SKILL,
  Command.CHOOSE_GENERAL,
  Command.CHOOSE_KINGDOM,
  Command.CHOOSE_SUIT,
  Command.CHOOSE_ROLE,
  Command.CHOOSE_ROLE_3V3,
  Command.CHOOSE_DIRECTION,
  Command.CHOOSE_PLAYER,
  Command.CHOOSE_ORDER,
  Command.ASK_PEACH,
  Command.NULLIFICATION,
  Command.MULTIPLE_CHOICE,
  Command.PINDIAN,
  Command.AMAZING_GRACE,
  Command.SKILL_YIJI,
  Command.SKILL_GUANXING,
  Command.SKILL_GONGXIN,
  Command.ASK_GENERAL,
  Command.ARRANGE_GENERAL,
  Command.LUCK_CARD,
  Command.TRIGGER_ORDER,
  Command.SURRENDER,
  Command.QML_INTERACT
] as const;

export const REPLY_COMMAND: Record<number, number> = {
  [Command.CHOOSE_CARD]: Command.CHOOSE_CARD,
  [Command.PLAY_CARD]: Command.RESPONSE_CARD,
  [Command.RESPONSE_CARD]: Command.RESPONSE_CARD,
  [Command.SHOW_CARD]: Command.RESPONSE_CARD,
  [Command.EXCHANGE_CARD]: Command.DISCARD_CARD,
  [Command.DISCARD_CARD]: Command.DISCARD_CARD,
  [Command.INVOKE_SKILL]: Command.INVOKE_SKILL,
  [Command.CHOOSE_GENERAL]: Command.CHOOSE_GENERAL,
  [Command.CHOOSE_KINGDOM]: Command.CHOOSE_KINGDOM,
  [Command.CHOOSE_SUIT]: Command.CHOOSE_SUIT,
  [Command.CHOOSE_ROLE]: Command.CHOOSE_ROLE,
  [Command.CHOOSE_ROLE_3V3]: Command.CHOOSE_ROLE_3V3,
  [Command.CHOOSE_DIRECTION]: Command.CHOOSE_DIRECTION,
  [Command.CHOOSE_PLAYER]: Command.CHOOSE_PLAYER,
  [Command.CHOOSE_ORDER]: Command.CHOOSE_ORDER,
  [Command.ASK_PEACH]: Command.RESPONSE_CARD,
  [Command.NULLIFICATION]: Command.RESPONSE_CARD,
  [Command.MULTIPLE_CHOICE]: Command.MULTIPLE_CHOICE,
  [Command.PINDIAN]: Command.RESPONSE_CARD,
  [Command.AMAZING_GRACE]: Command.AMAZING_GRACE,
  [Command.SKILL_YIJI]: Command.SKILL_YIJI,
  [Command.SKILL_GUANXING]: Command.SKILL_GUANXING,
  [Command.SKILL_GONGXIN]: Command.SKILL_GONGXIN,
  [Command.SURRENDER]: Command.SURRENDER,
  [Command.ASK_GENERAL]: Command.ASK_GENERAL,
  [Command.ARRANGE_GENERAL]: Command.ARRANGE_GENERAL,
  [Command.LUCK_CARD]: Command.LUCK_CARD,
  [Command.TRIGGER_ORDER]: Command.TRIGGER_ORDER,
  [Command.QML_INTERACT]: Command.QML_INTERACT
};

export function replyCommand(requestCommand: number): number {
  return REPLY_COMMAND[requestCommand] ?? requestCommand;
}

export function optionReply(command: number, field: string, value: string | number | boolean): JsonObject {
  return { schema_version: 1, [field]: value };
}

export function cancelReply(): JsonObject {
  return { schema_version: 1, cancelled: true };
}

export function cardIdReply(cardId: number): JsonObject {
  return { schema_version: 1, cancelled: false, card_id: cardId };
}

export function cardIdsReply(cardIds: number[]): JsonObject {
  return { schema_version: 1, cancelled: false, card_ids: cardIds };
}

export function cardResponseReply(
  cardText: string,
  targets: string[],
  skillName = "",
  instanceId = 0
): JsonObject {
  return {
    schema_version: 1,
    cancelled: false,
    card_text: cardText,
    targets,
    activation_skill_name: skillName,
    activation_skill_instance_id: instanceId
  };
}

export function assignmentReply(assignments: Record<string, string>): JsonObject {
  const players = Object.keys(assignments);
  return {
    schema_version: 1,
    cancelled: false,
    players,
    roles: players.map((player) => assignments[player] ?? "")
  };
}

export function yijiReply(cardIds: number[], target: string): JsonObject {
  return {
    schema_version: 1,
    cancelled: false,
    card_ids: cardIds,
    target_player: target
  };
}

export function guanxingReply(top: number[], bottom: number[]): JsonObject {
  return {
    schema_version: 1,
    top_card_ids: top,
    bottom_card_ids: bottom
  };
}

export function playersReply(players: string[]): JsonObject {
  return { schema_version: 1, cancelled: false, players };
}

export function arrangeReply(generals: string[]): JsonObject {
  return { schema_version: 1, cancelled: false, generals };
}

export function qmlReply(value: JsonObject | null): JsonObject {
  if (value === null)
    return { schema_version: 1, has_value: false };
  return { schema_version: 1, has_value: true, value };
}

export function replyForCommand(
  command: number,
  input: {
    cancelled?: boolean;
    option?: string;
    bool?: boolean;
    int?: number;
    cardId?: number;
    cardIds?: number[];
    cardText?: string;
    targets?: string[];
    skillName?: string;
    instanceId?: number;
    assignments?: Record<string, string>;
    yijiTarget?: string;
    top?: number[];
    bottom?: number[];
    players?: string[];
    generals?: string[];
    qml?: JsonObject | null;
  }
): JsonObject {
  if (input.cancelled) {
    if (command === Command.INVOKE_SKILL)
      return optionReply(command, "invoke", false);
    if (command === Command.SURRENDER)
      return optionReply(command, "surrender", false);
    if (command === Command.LUCK_CARD)
      return optionReply(command, "use_luck_card", false);
    if (command === Command.QML_INTERACT)
      return qmlReply(null);
    return cancelReply();
  }
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
      return optionReply(command, "order", input.int ?? 0);
    case Command.INVOKE_SKILL:
      return optionReply(command, "invoke", input.bool ?? true);
    case Command.SURRENDER:
      return optionReply(command, "surrender", input.bool ?? true);
    case Command.LUCK_CARD:
      return optionReply(command, "use_luck_card", input.bool ?? true);
    case Command.CHOOSE_ROLE:
      return assignmentReply(input.assignments ?? {});
    case Command.SKILL_GONGXIN:
    case Command.AMAZING_GRACE:
    case Command.CHOOSE_CARD:
      return cardIdReply(input.cardId ?? 0);
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
        input.cardText ?? "",
        input.targets ?? [],
        input.skillName ?? "",
        input.instanceId ?? 0
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
