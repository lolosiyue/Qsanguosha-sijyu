import { describe, expect, it } from "vitest";
import { Command } from "../src/protocol";
import {
  INTERACTION_COMMANDS,
  REPLY_COMMAND,
  cancelReply,
  replyCommand,
  replyForCommand
} from "../src/replies";

describe("replies", () => {
  it("maps every interaction command to a reply command", () => {
    for (const command of INTERACTION_COMMANDS)
      expect(REPLY_COMMAND[command], `missing REPLY_COMMAND for ${command}`).toBeTypeOf("number");
  });

  it("maps PLAY_CARD replies onto RESPONSE_CARD", () => {
    expect(replyCommand(Command.PLAY_CARD)).toBe(Command.RESPONSE_CARD);
  });

  it("sends invoke false when cancelling a skill ask", () => {
    expect(replyForCommand(Command.INVOKE_SKILL, { cancelled: true })).toEqual({
      schema_version: 1,
      invoke: false
    });
  });

  it("builds a card response with schema_version 1", () => {
    expect(replyForCommand(Command.PLAY_CARD, {
      cardText: "@ZhengyuCard=4",
      targets: ["sgs2"],
      skillName: "yj_zhengyu",
      instanceId: 3
    })).toEqual({
      schema_version: 1,
      cancelled: false,
      card_text: "@ZhengyuCard=4",
      targets: ["sgs2"],
      activation_skill_name: "yj_zhengyu",
      activation_skill_instance_id: 3
    });
  });

  it("uses cancelled true for a generic cancel", () => {
    expect(cancelReply()).toEqual({ schema_version: 1, cancelled: true });
  });
});
