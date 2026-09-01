import type { LiveSession } from "./session";

export interface UiState {
  name: string;
  avatar: string;
  ws: string;
  reconnect: boolean;
  selectedCards: number[];
  selectedPlayers: string[];
  selectedOption: string;
  top: number[];
  bottom: number[];
  assignments: Record<string, string>;
  qmlText: string;
  skillInstance: number;
  logPinned: boolean;
  hiddenIndex: number;
}

export interface UiBind {
  session: LiveSession;
  ui: UiState;
  route: { roomId?: number; reconnect: boolean };
  render: () => void;
  currentCardId: () => number;
  isCardClickable: (cardId: number) => boolean;
  isPlayerClickable: (name: string) => boolean;
  togglePlayer: (name: string) => void;
  resetSelection: () => void;
}
