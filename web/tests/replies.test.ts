import { describe, expect, it } from "vitest";
import { Command } from "../src/protocol";
import { cardsIntent, cancelReply, responseIntent } from "../src/replies";
import type { RulesSelection } from "../src/rules-client";

describe("response intents", () => {
  it("represents cancellation without a transport command", () => {
    expect(responseIntent(Command.INVOKE_SKILL, { cancelled: true })).toEqual({ kind: "cancel", payload: {} });
  });
  it("uses native card selection fields", () => {
    expect(responseIntent(Command.PLAY_CARD, {
      cardIds: [12], targets: ["sgs2"], skillName: "yj_zhengyu", instanceId: 3, userString: "slash"
    })).toEqual({ kind: "cards", card_ids: [12], targets: ["sgs2"], skill_name: "yj_zhengyu",
      skill_instance_id: 3, user_string: "slash", top: [], bottom: [] });
  });
  it("clones the canonical selection arrays", () => {
    const selection: RulesSelection = { card_ids: [1], targets: ["sgs2"], skill_name: "",
      skill_instance_id: 0, user_string: "", top: [], bottom: [] };
    const intent = cardsIntent(selection);
    selection.card_ids.push(2);
    expect(intent.card_ids).toEqual([1]);
  });
  it("keeps cancellation factory transport-free", () => {
    expect(cancelReply()).toEqual({ kind: "cancel", payload: {} });
  });
});
