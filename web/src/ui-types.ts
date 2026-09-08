import type { LiveSession } from "./session";
import type { RulesController, RulesSelection } from "./rules-client";
export type { RulesSelection } from "./rules-client";

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
  ruleDeclaration: string;
  logPinned: boolean;
  hiddenIndex: number;
}

export interface UiBind {
  session: LiveSession;
  rules: RulesController;
  ui: UiState;
  route: { roomId?: number; reconnect: boolean };
  render: () => void;
  currentCardId: () => number;
  rulesSelection: () => RulesSelection;
  isCardClickable: (cardId: number) => boolean;
  isPlayerClickable: (name: string) => boolean;
  togglePlayer: (name: string) => void;
  removeTarget: (index: number) => void;
  resetSelection: () => void;
}
