import { describe, expect, it } from "vitest";
import {
  displayAttackRange,
  playerHandLabel,
  syncedDistanceTo,
  syncedHandMax,
  targetRangeLabel
} from "../src/player-metrics";
import { ClientGameState } from "../src/state";

describe("player metrics", () => {
  it("shows hand count with a synced handMax", () => {
    const state = new ClientGameState();
    state.setSelfName("sgs1");
    state.setPlayerValue("sgs1", "hand_count", 3);
    expect(playerHandLabel(state, "sgs1")).toBe("手3");
    state.setPlayerValue("sgs1", "hand_max", 4);
    expect(syncedHandMax(state, "sgs1")).toEqual({ known: true, value: 4 });
    expect(playerHandLabel(state, "sgs1")).toBe("手3/4");
  });

  it("reads stacked fixed distances and the synced cache, never seat fallback", () => {
    const state = new ClientGameState();
    state.setSelfName("sgs1");
    state.setPlayerNames(["sgs1", "sgs2"]);
    state.setPlayerValue("sgs1", "seat", 1);
    state.setPlayerValue("sgs2", "seat", 2);
    expect(syncedDistanceTo(state, "sgs1", "sgs2").known).toBe(false);
    state.setPlayerValue("sgs1", "fixed_distances", { sgs2: [2, 4] });
    expect(syncedDistanceTo(state, "sgs1", "sgs2")).toEqual({ known: true, value: 2 });
    state.setPlayerValue("sgs1", "fixed_distances", {});
    state.setPlayerValue("sgs1", "distanceTo_sgs2", 3);
    expect(syncedDistanceTo(state, "sgs1", "sgs2")).toEqual({ known: true, value: 3 });
  });

  it("omits attack range until native metrics are known", () => {
    const state = new ClientGameState();
    state.setSelfName("sgs1");
    state.setPlayerNames(["sgs1", "sgs2"]);
    state.setPlayerValue("sgs1", "distanceTo_sgs2", 1);
    expect(displayAttackRange(null, "sgs1").known).toBe(false);
    expect(targetRangeLabel(state, "sgs1", "sgs2")).toBe("距1");
  });
});
