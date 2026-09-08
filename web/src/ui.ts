import { Command, asBool, asString } from "./protocol";
import {
  cardSelectable,
  playerSelectable,
  useMode
} from "./eligibility";
import { LiveSession, defaultWsUrl, parseRoute } from "./session";
import { RulesController } from "./rules-client";
import {
  applySceneBackground,
  defaultTableBgUrl,
  lobbyBackgroundUrl
} from "./backdrop";
import { assetImg } from "./assets";
import { connectForm, sharePanel, waitingRoom } from "./ui-connect";
import { el } from "./ui-dom";
import { dashboardView, logView, tableView } from "./ui-room";
import { interactionView } from "./ui-interaction";
import type { RulesSelection, UiBind, UiState } from "./ui-types";

const session = new LiveSession();
const route = parseRoute();

const ui: UiState = {
  name: localStorage.getItem("qsan-name") || "web-player",
  avatar: localStorage.getItem("qsan-avatar") || "caocao",
  ws: defaultWsUrl(),
  reconnect: route.reconnect,
  selectedCards: [],
  selectedPlayers: [],
  selectedOption: "",
  top: [],
  bottom: [],
  assignments: {},
  qmlText: "{}",
  skillInstance: 0,
  ruleDeclaration: "",
  logPinned: true,
  hiddenIndex: -1
};

function currentCardId(): number {
  return ui.selectedCards.find((id) => id >= 0) ?? -1;
}

function rulesSelection(): RulesSelection {
  return {
    card_ids: [...ui.selectedCards],
    targets: [...ui.selectedPlayers],
    skill_name: ui.selectedOption,
    skill_instance_id: ui.skillInstance,
    user_string: ui.ruleDeclaration
  };
}

function pruneSelection(): void {
  const interaction = session.interaction;
  if (!interaction)
    return;
  // Native candidates describe the next step, not the already selected cards.
  // Keep the ordered draft so an invalidated selection can still be removed.
  if (rules.supports(interaction.command))
    return;
  const { command, payload } = interaction;
  const skill = ui.selectedOption;
  ui.selectedCards = ui.selectedCards.filter((id) =>
    id < 0 || cardSelectable(session.state, command, payload, id, skill));
  const cardId = currentCardId();
  ui.selectedPlayers = ui.selectedPlayers.filter((name) =>
    playerSelectable(session.state, command, payload, name, cardId, ui.selectedPlayers, skill));
}

function isCardClickable(cardId: number): boolean {
  const interaction = session.interaction;
  if (!interaction)
    return false;
  if (rules.supports(interaction.command)) {
    if (ui.selectedCards.includes(cardId))
      return true;
    const result = rules.current(session, rulesSelection()) ? rules.result : null;
    return !!result?.known && result.selectable_cards.includes(cardId);
  }
  return cardSelectable(session.state, interaction.command, interaction.payload, cardId, ui.selectedOption);
}

function isPlayerClickable(name: string): boolean {
  const interaction = session.interaction;
  if (!interaction)
    return false;
  if (rules.supports(interaction.command)) {
    const result = rules.current(session, rulesSelection()) ? rules.result : null;
    return !!result?.known && result.next_targets.candidates.includes(name);
  }
  const cardId = currentCardId();
  if (interaction.command === Command.CHOOSE_PLAYER)
    return playerSelectable(session.state, interaction.command, interaction.payload, name, cardId, ui.selectedPlayers, ui.selectedOption);
  if (useMode(interaction.command) !== "play")
    return false;
  if (!ui.selectedOption && cardId < 0)
    return false;
  return playerSelectable(session.state, interaction.command, interaction.payload, name, cardId, ui.selectedPlayers, ui.selectedOption);
}

function resetSelection(): void {
  ui.selectedCards = [];
  ui.selectedPlayers = [];
  ui.selectedOption = "";
  ui.skillInstance = 0;
  ui.ruleDeclaration = "";
  ui.hiddenIndex = -1;
  ui.top = [];
  ui.bottom = [];
  ui.assignments = {};
  ui.qmlText = "{}";
}

function togglePlayer(name: string): void {
  if (!isPlayerClickable(name))
    return;
  if (session.interaction && rules.supports(session.interaction.command)) {
    // Repeated names are ordered target votes; native maxVotes controls additions.
    ui.selectedPlayers = [...ui.selectedPlayers, name];
    render();
    return;
  }
  ui.selectedPlayers = ui.selectedPlayers.includes(name)
    ? ui.selectedPlayers.filter((item) => item !== name)
    : [...ui.selectedPlayers, name];
  render();
}

function removeTarget(index: number): void {
  ui.selectedPlayers = ui.selectedPlayers.filter((_name, selectedIndex) => selectedIndex !== index);
  render();
}

function app(): HTMLElement {
  return document.getElementById("app") as HTMLElement;
}

const rules = new RulesController(() => render());
session.setRulesProvider(activeSession => rules.initialize(activeSession));
let selectionRequest = "";

const bind: UiBind = {
  session,
  rules,
  ui,
  route,
  render,
  currentCardId,
  rulesSelection,
  isCardClickable,
  isPlayerClickable,
  togglePlayer,
  removeTarget,
  resetSelection
};

export function render(): void {
  const request = `${session.generation}:${session.interaction?.messageId ?? ""}`;
  if (request !== selectionRequest) {
    selectionRequest = request;
    resetSelection();
  }
  pruneSelection();
  rules.update(session, rulesSelection());
  const root = app();
  root.replaceChildren();
  const shell = el("div", { class: "app" });
  if (session.phase === "idle" || session.phase === "failed") {
    applySceneBackground(lobbyBackgroundUrl());
    const logo = assetImg(["/assets/logo/logo.png"], "", "logo");
    logo.alt = "QSanguosha";
    shell.className = "app idle";
    shell.append(logo, connectForm(bind));
    root.append(shell);
    return;
  }
  const toolbar = el("div", { class: "toolbar" });
  toolbar.append(sharePanel(bind, true));
  toolbar.append(el("strong", {}, ["QSanguosha"]));
  toolbar.append(el("span", { class: "status" }, [
    `${session.phase} ${asString(session.state.connectionValue("room_id"))}`
  ]));
  const tableBg = asString(session.state.gameValue("table_bg")) || defaultTableBgUrl();
  applySceneBackground(tableBg);
  if (!asBool(session.state.gameValue("started"))) {
    shell.className = "app wait";
    shell.append(toolbar, waitingRoom(bind));
    if (session.interaction) {
      const view = interactionView(bind);
      view.classList.add("wait-panel");
      shell.append(view);
    }
    root.append(shell);
    return;
  }
  shell.className = "app room";
  shell.append(toolbar, tableView(bind), logView(bind), dashboardView(bind));
  root.append(shell);
}

export function start(): void {
  let frame = 0;
  session.onChange(() => {
    if (frame)
      return;
    frame = requestAnimationFrame(() => {
      frame = 0;
      try {
        render();
      } catch (error) {
        session.interactionError = error instanceof Error ? error.message : String(error);
      }
    });
  });
  window.addEventListener("pagehide", (event) => {
    // A back/forward-cache restoration resumes this same controller instance.
    if (!event.persisted)
      rules.dispose();
  });
  render();
}
