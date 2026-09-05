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

  it("follows cards in and out of the discard pile", () => {
    const state = new ClientGameState();
    state.setSelfName("sgs1");
    const move = (command: number, cardIds: number[], toPlace: number) =>
      applyNotification(state, command, {
        schema_version: 1,
        moves: [{
          from_player: "sgs1",
          from_place: 0,
          to_player: toPlace === 5 ? "" : "sgs1",
          to_place: toPlace,
          to_pile: "",
          card_ids: cardIds,
          open: true
        }]
      });
    // The server marshals the whole pile only on a state sync, so an ordinary
    // discard has to be followed through the move itself.
    move(Command.LOSE_CARD, [11, 12], 5);
    expect(state.gameValue("discard_pile")).toEqual([11, 12]);
    // A card taken back out of the pile leaves it again.
    move(Command.GET_CARD, [11], 0);
    expect(state.gameValue("discard_pile")).toEqual([12]);
  });

  it("keeps the phase name the server sent", () => {
    const state = new ClientGameState();
    const result = applyNotification(state, Command.SET_PROPERTY, {
      schema_version: 1,
      player_name: "sgs1",
      action: "property",
      property_name: "phase",
      string_value: "play"
    });
    expect(result.success).toBe(true);
    // Player::getPhaseString() puts a name on the wire, so coercing it to a
    // number turns every phase into 0.
    expect(state.playerValue("sgs1", "phase")).toBe("play");
    expect(state.gameValue("current_phase")).toBe("play");
    expect(state.gameValue("current_player")).toBe("sgs1");
  });

  it('clears every flag on a "." property update', () => {
    const state = new ClientGameState();
    const setFlag = (flag: string) =>
      applyNotification(state, Command.SET_PROPERTY, {
        schema_version: 1,
        player_name: "sgs1",
        action: "property",
        property_name: "flags",
        string_value: flag
      });
    setFlag("CurrentPlayer");
    setFlag("actioned");
    setFlag("-actioned");
    expect(state.playerValue("sgs1", "flags")).toEqual(["CurrentPlayer"]);
    // The server ends every turn with "." and nothing else ever removes
    // actioned or CurrentPlayer, so appending it would leave them set for the
    // rest of the game.
    setFlag(".");
    expect(state.playerValue("sgs1", "flags")).toEqual([]);
  });

  it("drops a card mark set to zero, and with the flags", () => {
    const state = new ClientGameState();
    const setCardMark = (mark: string, value: number) =>
      applyNotification(state, Command.CARD_MARK, {
        schema_version: 1,
        card_id: 7,
        mark_name: mark,
        value
      });
    setCardMark("kept", 2);
    setCardMark("spent", 1);
    // A card's marks live inside its flags in the engine, so both engine
    // sentinels have to reach this separate map: zero drops the entry, and
    // clearing the flags clears the marks with them.
    setCardMark("spent", 0);
    expect(state.card(7)?.marks).toEqual({ kept: 2 });
    applyNotification(state, Command.CARD_FLAG, {
      schema_version: 1,
      card_id: 7,
      flag: "."
    });
    expect(state.card(7)?.marks).toEqual({});
  });

  it('clears every card flag on a "." CARD_FLAG', () => {
    const state = new ClientGameState();
    const setCardFlag = (flag: string) =>
      applyNotification(state, Command.CARD_FLAG, {
        schema_version: 1,
        card_id: 7,
        flag
      });
    setCardFlag("visible");
    setCardFlag("cardTip:x");
    setCardFlag("-visible");
    expect(state.card(7)?.flags).toEqual(["cardTip:x"]);
    setCardFlag(".");
    expect(state.card(7)?.flags).toEqual([]);
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
