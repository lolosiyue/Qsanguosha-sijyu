import type { JsonObject } from "./protocol";

let table: Record<string, string> = {};
let cards: Record<string, JsonObject> = {};

async function loadJson<T>(path: string, fallback: T): Promise<T> {
  try {
    const response = await fetch(path);
    if (!response.ok)
      return fallback;
    return await response.json() as T;
  } catch {
    return fallback;
  }
}

export async function loadTranslations(): Promise<void> {
  const [nextTable, nextCards] = await Promise.all([
    loadJson<Record<string, string>>("/translations.json", {}),
    loadJson<Record<string, JsonObject>>("/cards.json", {})
  ]);
  table = nextTable;
  cards = nextCards;
}

export function tr(key: string): string {
  if (!key)
    return key;
  if (key.includes("\\"))
    return key.split("\\").map((part) => table[part] ?? part).join("");
  return table[key] ?? key;
}

// Wire prompts from askForCard / askForDiscard / askForPlayerChosen:
// key:%src:%dest:%arg:%arg2, matching Client::formatPromptList.
export function formatInteractionPrompt(
  prompt: string,
  playerName: (objectName: string) => string = tr
): string {
  if (!prompt)
    return prompt;
  const texts = prompt.split(":");
  const key = texts[0] ?? "";
  let result = tr(key);
  if (result === key && key.endsWith("-jink")) {
    result = texts.length >= 3
      ? "%src 使用了【%dest】，请打出一张【闪】"
      : "%src 对你使用【杀】，你需使用【闪】抵消之";
  }
  const slotText = (token: string) => token ? tr(token) : token;
  const slotPlayer = (token: string) => {
    if (!token)
      return token;
    if (/^sgs\d+$/.test(token))
      return playerName(token);
    return slotText(token);
  };
  if (texts.length >= 5)
    result = result.replaceAll("%arg2", slotText(texts[4] ?? ""));
  if (texts.length >= 4)
    result = result.replaceAll("%arg", slotText(texts[3] ?? ""));
  if (texts.length >= 3)
    result = result.replaceAll("%dest", slotPlayer(texts[2] ?? ""));
  if (texts.length >= 2)
    result = result.replaceAll("%src", slotPlayer(texts[1] ?? ""));
  return result;
}

export function cardRecord(cardId: number): JsonObject | undefined {
  const entry = cards[String(cardId)];
  return entry;
}
