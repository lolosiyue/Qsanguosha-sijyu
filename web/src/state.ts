import { asBool, asNumber, asString, type JsonObject, type JsonValue } from "./protocol";

export interface CardState {
  id: number;
  [key: string]: JsonValue;
}

export interface PlayerState {
  object_name: string;
  alive: boolean;
  removed?: boolean;
  [key: string]: JsonValue | undefined;
}

export interface PresentationEvent {
  command: number;
  text: string;
  payload?: JsonObject;
}

export class ClientGameState {
  connection: JsonObject = {};
  setup: JsonObject = {};
  game: JsonObject = {};
  selfName = "";
  playerNames: string[] = [];
  players = new Map<string, PlayerState>();
  cards = new Map<number, CardState>();
  latestPayloads = new Map<number, JsonValue>();
  flowCounts = new Map<number, number>();
  presentationEvents: PresentationEvent[] = [];
  cardIdSpace = 0;

  clone(): ClientGameState {
    const copy = new ClientGameState();
    copy.connection = structuredClone(this.connection);
    copy.setup = structuredClone(this.setup);
    copy.game = structuredClone(this.game);
    copy.selfName = this.selfName;
    copy.playerNames = [...this.playerNames];
    copy.players = new Map(
      [...this.players.entries()].map(([key, value]) => [key, structuredClone(value)])
    );
    copy.cards = new Map(
      [...this.cards.entries()].map(([key, value]) => [key, structuredClone(value)])
    );
    copy.latestPayloads = new Map(this.latestPayloads);
    copy.flowCounts = new Map(this.flowCounts);
    copy.presentationEvents = this.presentationEvents.map((event) => ({ ...event }));
    copy.cardIdSpace = this.cardIdSpace;
    return copy;
  }

  reset(): void {
    this.connection = {};
    this.setup = {};
    this.selfName = "";
    this.cardIdSpace = 0;
    this.resetGameplayState();
  }

  resetGameplayState(): void {
    this.game = {};
    this.playerNames = [];
    this.players.clear();
    this.cards.clear();
    this.latestPayloads.clear();
    this.flowCounts.clear();
    this.presentationEvents = [];
  }

  setConnectionValue(key: string, value: JsonValue): void {
    this.connection[key] = value;
  }

  connectionValue(key: string): JsonValue | undefined {
    return this.connection[key];
  }

  setGameValue(key: string, value: JsonValue): void {
    this.game[key] = value;
  }

  gameValue(key: string): JsonValue | undefined {
    return this.game[key];
  }

  setSelfName(name: string): void {
    this.selfName = name;
    if (name)
      this.ensurePlayer(name);
  }

  ensurePlayer(name: string): PlayerState {
    let player = this.players.get(name);
    if (!player) {
      player = { object_name: name, alive: true };
      this.players.set(name, player);
    }
    player.object_name = name;
    if (!this.playerNames.includes(name))
      this.playerNames.push(name);
    if (player.alive === undefined)
      player.alive = true;
    return player;
  }

  setPlayerNames(names: string[]): void {
    this.playerNames = [];
    for (const name of names) {
      if (!name || this.playerNames.includes(name))
        continue;
      this.playerNames.push(name);
      this.ensurePlayer(name);
    }
  }

  addPlayer(name: string): void {
    if (name)
      this.ensurePlayer(name);
  }

  removePlayer(name: string): void {
    this.playerNames = this.playerNames.filter((item) => item !== name);
    const player = this.ensurePlayer(name);
    player.removed = true;
    player.alive = false;
  }

  hasPlayer(name: string): boolean {
    const player = this.players.get(name);
    return !!player && player.removed !== true;
  }

  setPlayerValue(name: string, key: string, value: JsonValue): void {
    if (name)
      this.ensurePlayer(name)[key] = value;
  }

  playerValue(name: string, key: string): JsonValue | undefined {
    return this.players.get(name)?.[key];
  }

  player(name: string): PlayerState | undefined {
    return this.players.get(name);
  }

  setPlayerMark(name: string, mark: string, value: number): void {
    const player = this.ensurePlayer(name);
    const marks = isRecord(player.marks) ? { ...player.marks } : {};
    if (value === 0)
      delete marks[mark];
    else
      marks[mark] = value;
    player.marks = marks;
  }

  setPlayerAlive(name: string, alive: boolean): void {
    this.setPlayerValue(name, "alive", alive);
  }

  isPlayerAlive(name: string): boolean {
    return asBool(this.players.get(name)?.alive, true);
  }

  setCardIdSpace(count: number): void {
    this.cardIdSpace = Math.max(0, count);
  }

  ensureCard(cardId: number): CardState {
    let card = this.cards.get(cardId);
    if (!card) {
      card = { id: cardId };
      this.cards.set(cardId, card);
    }
    card.id = cardId;
    return card;
  }

  setCardValue(cardId: number, key: string, value: JsonValue): void {
    if (cardId >= 0)
      this.ensureCard(cardId)[key] = value;
  }

  card(cardId: number): CardState | undefined {
    return this.cards.get(cardId);
  }

  cardsForPlayer(playerName: string, place = -1): number[] {
    const result: number[] = [];
    for (const [id, card] of this.cards) {
      if (asString(card.owner) === playerName
          && (place < 0 || asNumber(card.place) === place))
        result.push(id);
    }
    return result;
  }

  cardsAtPlace(place: number, pile?: string): number[] {
    const result: number[] = [];
    for (const [id, card] of this.cards) {
      if (asNumber(card.place) !== place)
        continue;
      if (pile !== undefined && asString(card.pile) !== pile)
        continue;
      result.push(id);
    }
    return result;
  }

  recordFlow(command: number, payload: JsonValue): void {
    this.latestPayloads.set(command, payload);
    this.flowCounts.set(command, (this.flowCounts.get(command) ?? 0) + 1);
  }

  appendPresentationEvent(command: number, text: string, payload?: JsonObject): void {
    this.presentationEvents.push({ command, text, payload });
    while (this.presentationEvents.length > 200)
      this.presentationEvents.shift();
  }
}

export function isRecord(value: JsonValue | undefined): value is JsonObject {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}
