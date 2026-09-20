import { describe, expect, it } from "vitest";
import { ClientGameState } from "../src/state";
import type { JsonObject } from "../src/protocol";

function snapshot(): JsonObject {
  return {
    connection: { state: "active" }, setup: { mode: "05p" },
    game: { current_player: "p1", resolution_stack: [{ id: "r1" }] },
    self_name: "p1", card_id_space: 100, player_names: ["p2", "p1"],
    players: [
      { object_name: "p1", alive: true, skill_instances: [{ id: 3, name: "skill" }] },
      { object_name: "p2", alive: true, hand_count: 4 }
    ],
    cards: [{ id: 7, owner: "p1", place: 0, flags: ["shown"] }],
    flow_counts: { "19": 2 },
    presentation_events: [{ text: "diagnostic only" }]
  };
}

describe("native state projection", () => {
  it("preserves native fields, roster order and count-only hidden hands", () => {
    const state = new ClientGameState();
    const source = snapshot();
    state.hydrateNativeView(source);
    expect(state.playerNames).toEqual(["p2", "p1"]);
    expect(state.players.get("p1")?.skill_instances).toEqual([{ id: 3, name: "skill" }]);
    expect(state.cardsForPlayer("p1", 0)).toEqual([7]);
    expect(state.cardsForPlayer("p2", 0)).toEqual([]);
    expect(state.players.get("p2")?.hand_count).toBe(4);
    expect(state.presentationEvents).toEqual([]);
    (source.cards as JsonObject[])[0].owner = "p2";
    expect(state.card(7)?.owner).toBe("p1");
  });

  it("replaces removed data and rejects a malformed snapshot atomically", () => {
    const state = new ClientGameState();
    state.hydrateNativeView(snapshot());
    const invalid = snapshot();
    invalid.cards = [{ id: -1, owner: "p2" }];
    expect(() => state.hydrateNativeView(invalid)).toThrow();
    expect(state.cardsForPlayer("p1", 0)).toEqual([7]);
    const next = snapshot();
    next.cards = [];
    next.players = [{ object_name: "p1", alive: true }];
    next.player_names = ["p1"];
    state.hydrateNativeView(next);
    expect(state.players.has("p2")).toBe(false);
    expect(state.card(7)).toBeUndefined();
    expect(state.players.get("p1")?.skill_instances).toBeUndefined();
  });
});
