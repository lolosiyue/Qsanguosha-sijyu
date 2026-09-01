import { beforeEach, describe, expect, it } from "vitest";
import { formatInteractionPrompt, setTranslationsForTest } from "../src/i18n";
import {
  formatGameEvent,
  formatPresentationEvent,
  formatSkillLog
} from "../src/log-text";
import { Command, GameEvent } from "../src/protocol";

function playerName(objectName: string): string {
  if (objectName === "sgs1")
    return "刘玄德";
  if (objectName === "sgs2")
    return "曹孟德";
  return objectName;
}

describe("log-text", () => {
  beforeEach(() => {
    setTranslationsForTest({
      "#TriggerSkill": "%from 的 %arg 被触发",
      bahu: "跋扈",
      "#Damage": "%from 对 %to 造成了 %arg 点 %arg2 伤害",
      fire: "火焰",
      "#QuitDying": "%from 脱离濒死状态",
      "#UseCardPhrase_use": "%from 使用了 %card",
      "#UseCardPhrase_target": "，目标是 %to",
      "#LogSelf": "自己"
    });
  });

  it("fills skill log player and argument slots", () => {
    const text = formatSkillLog({
      log_type: "#TriggerSkill",
      from_player: "sgs1",
      to_players: [],
      card_string: "",
      arguments: ["bahu", "", "", "", ""]
    }, playerName);
    expect(text).toContain("刘玄德");
    expect(text).toContain("跋扈");
    expect(text).not.toContain("%from");
    expect(text).not.toContain("#TriggerSkill");
  });

  it("keeps an unknown log type as its key", () => {
    const text = formatSkillLog({
      log_type: "#NoSuchLogTypeHere",
      from_player: "sgs1",
      arguments: []
    }, playerName);
    expect(text).toContain("#NoSuchLogTypeHere");
  });

  it("renders the turn separator instead of the raw key", () => {
    expect(formatSkillLog({ log_type: "$AppendSeparator" }, playerName)).toBe("--------");
  });

  it("stays silent for dying game events", () => {
    expect(formatGameEvent({
      event: GameEvent.PLAYER_DYING,
      player_name: "sgs2"
    }, playerName)).toBe("");
  });

  it("announces leaving dying state by name", () => {
    expect(formatGameEvent({
      event: GameEvent.PLAYER_QUITDYING,
      player_name: "sgs2"
    }, playerName)).toContain("曹孟德");
  });

  it("strips chat markup and names the speaker", () => {
    const text = formatPresentationEvent({
      command: Command.SPEAK,
      text: "sgs1: raw",
      payload: {
        speaker: "sgs1",
        text: "<font color=#EEB422>已加入游戏</font>"
      }
    }, playerName);
    expect(text).not.toContain("<");
    expect(text).toContain("已加入游戏");
    expect(text).toContain("刘玄德");
  });

  it("drops emotion from the transcript", () => {
    expect(formatPresentationEvent({
      command: Command.SET_EMOTION,
      text: "sgs3 emotion thunder_slash",
      payload: { player_name: "sgs3", emotion: "thunder_slash" }
    }, playerName)).toBe("");
  });
});

describe("formatInteractionPrompt", () => {
  beforeEach(() => {
    setTranslationsForTest({
      "slash-jink": "%src 对你使用【杀】，你需使用【闪】抵消之",
      "shoot-jink": "%src 使用了【%dest】，请打出一张【闪】",
      pierce_shoot: "贯穿射击"
    });
  });

  it("fills slash-jink %src with the general name", () => {
    const text = formatInteractionPrompt("slash-jink:sgs1", playerName);
    expect(text).toContain("刘玄德");
    expect(text).toContain("闪");
    expect(text).not.toContain("slash-jink");
  });

  it("fills shoot-jink %src and %dest", () => {
    const text = formatInteractionPrompt("shoot-jink:sgs2:pierce_shoot", playerName);
    expect(text).toContain("曹孟德");
    expect(text).toContain("贯穿射击");
    expect(text).not.toContain("%src");
    expect(text).not.toContain("%dest");
    expect(text).not.toContain("shoot-jink");
  });
});
