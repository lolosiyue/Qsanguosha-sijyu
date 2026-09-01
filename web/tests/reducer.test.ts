import { describe, expect, it } from "vitest";
import { Command } from "../src/protocol";
import { applyNotification } from "../src/reducer";
import { ClientGameState } from "../src/state";

describe("reducer", () => {
  it("marks GAME_START on the game object", () => {
    const state = new ClientGameState();
    const result = applyNotification(state, Command.GAME_START, {
      schema_version: 1,
      card_ids: [1, 2, 3]
    });
    expect(result.success).toBe(true);
    expect(state.gameValue("started")).toBe(true);
    expect(state.gameValue("draw_pile_count")).toBe(3);
  });

  it("raises hand_count on GET_CARD into hand", () => {
    const state = new ClientGameState();
    state.setSelfName("sgs1");
    state.setPlayerValue("sgs1", "hand_count", 0);
    const result = applyNotification(state, Command.GET_CARD, {
      schema_version: 1,
      moves: [{
        from_player: "",
        from_place: 6,
        to_player: "sgs1",
        to_place: 0,
        to_pile: "",
        card_ids: [10, 11],
        open: true
      }]
    });
    expect(result.success).toBe(true);
    expect(state.playerValue("sgs1", "hand_count")).toBe(2);
    expect(state.card(10)?.owner).toBe("sgs1");
    expect(state.card(10)?.place).toBe(0);
  });

  it("rejects an unclassified command", () => {
    const state = new ClientGameState();
    const result = applyNotification(state, 9999, { schema_version: 1 });
    expect(result.success).toBe(false);
    expect(result.disposition).toBe("unclassified");
  });

  it("keeps SET_EMOTION out of eventText", () => {
    const state = new ClientGameState();
    const result = applyNotification(state, Command.SET_EMOTION, {
      schema_version: 1,
      player_name: "sgs3",
      emotion: "light"
    });
    expect(result.success).toBe(true);
    expect(result.eventText).toBe("");
  });
});
