import { describe, expect, it } from "vitest";
import { Command } from "../src/protocol";
import { isEnumeratedCommand, isRulesEvaluation } from "../src/rules-client";

function skill(overrides: Record<string, unknown> = {}): Record<string, unknown> {
  return {
    name: "guhuo",
    instance_id: 0,
    available: true,
    status: "available",
    v2: true,
    subcard_min: 1,
    subcard_max: 1,
    usage_scope: "turn",
    usage_used: 0,
    invalid: false,
    response_or_use: true,
    expand_pile: "",
    ...overrides
  };
}

function evaluation(overrides: Record<string, unknown> = {}): Record<string, unknown> {
  return {
    schema_version: 1,
    generation: 0,
    revision: 3,
    request_id: "7",
    known: true,
    reason: "",
    can_confirm: true,
    card_text: "slash:guhuo[heart:5]=12",
    selectable_cards: [12, 13],
    card_zones: { "12": "hand", "13": "expand_pile" },
    selection_min: 1,
    selection_max: 1,
    interaction: { type: "PlayCard", command: Command.PLAY_CARD, payload: {} },
    skills: [skill()],
    declarations: ["slash"],
    declaration_dialog: { type: "guhuo", object_name: "guhuo", parameters: { left: true } },
    next_targets: { candidates: ["sgs2"], max_votes: { sgs2: 1 } },
    wire: { command: Command.RESPONSE_CARD, reply_to: "7", payload: { schema_version: 1 } },
    ...overrides
  };
}

describe("native command classification", () => {
  it("treats guanxing, gongxin and yiji as enumerated prompts", () => {
    for (const command of [Command.SKILL_GUANXING, Command.SKILL_GONGXIN, Command.SKILL_YIJI])
      expect(isEnumeratedCommand(command)).toBe(true);
  });

  it("leaves card-use prompts to the ViewAs path", () => {
    for (const command of [Command.PLAY_CARD, Command.RESPONSE_CARD,
      Command.ASK_PEACH, Command.NULLIFICATION, Command.AMAZING_GRACE])
      expect(isEnumeratedCommand(command)).toBe(false);
  });
});

describe("rules evaluation contract", () => {
  it("accepts a complete native answer", () => {
    expect(isRulesEvaluation(evaluation())).toBe(true);
  });

  it("accepts an enumerated answer with no skills or declarations", () => {
    expect(isRulesEvaluation(evaluation({
      skills: [], declarations: [], declaration_dialog: {}, card_text: "", selectable_cards: [],
      card_zones: {}, selection_min: 0, selection_max: 5,
      interaction: { type: "SkillGuanxing", payload: { cards: [1, 2] } },
      wire: { command: Command.SKILL_GUANXING, reply_to: "7", payload: {} }
    }))).toBe(true);
  });

  it("rejects an answer without the shared interaction request", () => {
    expect(isRulesEvaluation(evaluation({ interaction: undefined }))).toBe(false);
  });

  it("rejects an answer without the declaration dialog shape", () => {
    expect(isRulesEvaluation(evaluation({ declaration_dialog: undefined }))).toBe(false);
    expect(isRulesEvaluation(evaluation({ declaration_dialog: "guhuo" }))).toBe(false);
  });

  it("rejects an answer without card zones", () => {
    expect(isRulesEvaluation(evaluation({ card_zones: undefined }))).toBe(false);
    expect(isRulesEvaluation(evaluation({ card_zones: { "12": 4 } }))).toBe(false);
  });

  it("rejects non-integer selection bounds", () => {
    expect(isRulesEvaluation(evaluation({ selection_min: 1.5 }))).toBe(false);
    expect(isRulesEvaluation(evaluation({ selection_max: "1" }))).toBe(false);
  });

  it("rejects a skill entry missing amount, usage or invalidation", () => {
    for (const field of ["subcard_min", "subcard_max", "usage_used", "usage_scope",
      "invalid", "status", "v2", "response_or_use", "expand_pile"])
      expect(isRulesEvaluation(evaluation({ skills: [skill({ [field]: undefined })] }))).toBe(false);
  });

  it("rejects a skill entry whose amount is not an integer", () => {
    expect(isRulesEvaluation(evaluation({ skills: [skill({ subcard_max: null })] }))).toBe(false);
  });

  it("keeps rejecting a malformed wire envelope", () => {
    expect(isRulesEvaluation(evaluation({ wire: { command: 1, reply_to: 7, payload: {} } }))).toBe(false);
    expect(isRulesEvaluation(evaluation({ wire: null }))).toBe(true);
  });
});
