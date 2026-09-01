import { describe, expect, it } from "vitest";
import {
  cardSelectable,
  distanceTo,
  playerSelectable
} from "../src/eligibility";
import { Command, PLACE_EQUIP, PLACE_HAND } from "../src/protocol";
import { ClientGameState } from "../src/state";

function twoSeats(): ClientGameState {
  const state = new ClientGameState();
  state.setSelfName("sgs1");
  state.setPlayerNames(["sgs1", "sgs2"]);
  state.setPlayerValue("sgs1", "seat", 1);
  state.setPlayerValue("sgs2", "seat", 2);
  state.setPlayerValue("sgs1", "hp", 4);
  state.setPlayerValue("sgs1", "max_hp", 4);
  state.setPlayerValue("sgs2", "hp", 4);
  state.setPlayerValue("sgs2", "max_hp", 4);
  return state;
}

function give(state: ClientGameState, id: number, objectName: string, owner = "sgs1"): void {
  state.setCardValue(id, "object_name", objectName);
  state.setCardValue(id, "owner", owner);
  state.setCardValue(id, "place", PLACE_HAND);
}

describe("eligibility", () => {
  it("greys a jink during PLAY_CARD", () => {
    const state = twoSeats();
    give(state, 1, "jink");
    expect(cardSelectable(state, Command.PLAY_CARD, {}, 1, "")).toBe(false);
  });

  it("allows a jink when the response pattern is jink", () => {
    const state = twoSeats();
    give(state, 1, "jink");
    expect(cardSelectable(state, Command.RESPONSE_CARD, { pattern: "jink" }, 1, "")).toBe(true);
  });

  it("rejects a slash as a jink response", () => {
    const state = twoSeats();
    give(state, 2, "slash");
    expect(cardSelectable(state, Command.RESPONSE_CARD, { pattern: "jink" }, 2, "")).toBe(false);
  });

  it("treats adjacent seats as distance 1", () => {
    const state = twoSeats();
    expect(distanceTo(state, "sgs1", "sgs2")).toBe(1);
  });

  it("adds a defensive horse to the distance", () => {
    const state = twoSeats();
    state.setCardValue(8, "object_name", "dilu");
    state.setCardValue(8, "owner", "sgs2");
    state.setCardValue(8, "place", PLACE_EQUIP);
    expect(distanceTo(state, "sgs1", "sgs2")).toBe(2);
  });

  it("blocks a slash Photo that is out of range after a +1 horse", () => {
    const state = twoSeats();
    give(state, 3, "slash");
    state.setCardValue(8, "object_name", "dilu");
    state.setCardValue(8, "owner", "sgs2");
    state.setCardValue(8, "place", PLACE_EQUIP);
    expect(playerSelectable(state, Command.PLAY_CARD, {}, "sgs2", 3, [], "")).toBe(false);
  });
});
