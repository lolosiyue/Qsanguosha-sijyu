import { asBool, asStringList, type JsonObject } from "./protocol";

export interface GameResultWinner {
  objectName: string;
  role: string;
}

export interface GameResultSummary {
  draw: boolean;
  winners: GameResultWinner[];
  selfOutcome: "victory" | "defeat" | "unknown";
}

/** Map the server's winner tokens to the ordered player/role result. */
export function summarizeGameResult(
  payload: JsonObject,
  playerNames: string[],
  selfName: string
): GameResultSummary {
  const roles = asStringList(payload.roles);
  const tokens = asStringList(payload.winner_tokens);
  const winners: GameResultWinner[] = [];
  for (let index = 0; index < playerNames.length; index++) {
    const objectName = playerNames[index];
    const role = roles[index] || "";
    if (tokens.includes(objectName) || (role.length > 0 && tokens.includes(role)))
      winners.push({ objectName, role });
  }
  const mappedTokens = new Set(winners.flatMap((winner) => [winner.objectName, winner.role]));
  for (const token of tokens) {
    if (!mappedTokens.has(token))
      winners.push({ objectName: "", role: token });
  }
  const draw = asBool(payload.standoff);
  let selfOutcome: GameResultSummary["selfOutcome"] = "unknown";
  if (draw)
    selfOutcome = "unknown";
  else if (tokens.length > 0 && playerNames.includes(selfName))
    selfOutcome = winners.some((winner) => winner.objectName === selfName)
      ? "victory" : "defeat";
  return { draw, winners, selfOutcome };
}

export function resultRoleLabel(role: string): string {
  const labels: Record<string, string> = {
    lord: "主公",
    loyalist: "忠臣",
    renegade: "內奸",
    rebel: "反賊"
  };
  return labels[role] || role;
}
