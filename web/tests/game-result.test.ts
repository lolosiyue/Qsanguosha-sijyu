import { describe, expect, it } from "vitest";
import { summarizeGameResult } from "../src/game-result";

describe("game result mapping", () => {
  it("maps role winner tokens through the ordered roles", () => {
    const result = summarizeGameResult({
      standoff: false,
      winner_tokens: ["lord"],
      roles: ["lord", "rebel"]
    }, ["alice", "bob"], "alice");

    expect(result.winners).toEqual([{ objectName: "alice", role: "lord" }]);
    expect(result.selfOutcome).toBe("victory");
  });

  it("maps object name tokens and does not infer from alive state", () => {
    const result = summarizeGameResult({
      standoff: false,
      winner_tokens: ["bob"],
      roles: ["lord", "rebel"]
    }, ["alice", "bob"], "alice");

    expect(result.winners).toEqual([{ objectName: "bob", role: "rebel" }]);
    expect(result.selfOutcome).toBe("defeat");
  });

  it("reports a standoff without declaring victory or defeat", () => {
    const result = summarizeGameResult({
      standoff: true,
      winner_tokens: [],
      roles: ["lord", "rebel"]
    }, ["alice", "bob"], "alice");

    expect(result.draw).toBe(true);
    expect(result.winners).toEqual([]);
    expect(result.selfOutcome).toBe("unknown");
  });

  it("includes dead teammates named by a winning role and does not guess a spectator outcome", () => {
    const result = summarizeGameResult({
      standoff: false, winner_tokens: ["lord", "loyalist"],
      roles: ["lord", "loyalist", "rebel"]
    }, ["alice", "bob", "carol"], "spectator");
    expect(result.winners.map(winner => winner.objectName)).toEqual(["alice", "bob"]);
    expect(result.selfOutcome).toBe("unknown");
  });
});
