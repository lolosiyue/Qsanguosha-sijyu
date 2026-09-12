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
  /** Presentation-only state; protocol and reducer state remain authoritative. */
  presentation?: UiPresentationState;
}

export interface UiPresentationState {
  focusPlayer?: string;
  browsedSeatIndex?: number;
  seatFilter?: "all" | "legal";
  battleFilter?: "all" | "mine" | "damage" | "skill";
  chatDraft?: string;
  chatScrollTop?: number;
  chatPinned?: boolean;
  battleScrollTop?: number;
  seatScrollLeft?: number;
  mobileLogCollapsed?: boolean;
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
