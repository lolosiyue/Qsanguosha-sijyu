import { Command, asBool, asNumberList, asString, asStringList } from "./protocol";
import { LiveSession, defaultWsUrl, parseRoute } from "./session";
import { RulesController } from "./rules-client";
import {
  applySceneBackground,
  defaultTableBgUrl,
  lobbyBackgroundUrl
} from "./backdrop";
import { assetImg, fullskinUrls } from "./assets";
import { connectForm, sharePanel, waitingRoom } from "./ui-connect";
import { el } from "./ui-dom";
import { dashboardView, logView, tableView } from "./ui-room";
import { interactionView } from "./ui-interaction";
import type { RulesSelection, UiBind, UiState } from "./ui-types";
import { SoloController } from "./solo-client";
import { localReturnHome, soloSetup } from "./ui-solo";
import { setupBoardLayout } from "./ui-seat-layout";

const session = new LiveSession();
const route = parseRoute();
const solo = new SoloController(() => render());
let soloAvailable = false;
let localWaitingGeneration = -1;
let disposeBoardLayout = () => {};
let countdownInterval: ReturnType<typeof setInterval> | undefined;

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
  hiddenIndex: -1,
  presentation: { chatDraft: "" }
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
    user_string: ui.ruleDeclaration,
    top: [...ui.top],
    bottom: [...ui.bottom]
  };
}

// A rearrangement prompt starts as "everything on top". Seed it before the
// first native query so the runtime never judges an empty draft the shell is
// about to replace anyway.
function seedSelection(): void {
  const interaction = session.interaction;
  if (!interaction || interaction.command !== Command.SKILL_GUANXING)
    return;
  const ids = asNumberList(interaction.payload.card_ids ?? interaction.payload.cards);
  const known = new Set(ids);
  // A state change can retire a card mid-draft; drop it instead of sending it.
  ui.top = ui.top.filter((id) => known.has(id));
  ui.bottom = ui.bottom.filter((id) => known.has(id));
  // A down-only rearrangement has no top area, so start every card where the
  // prompt can actually accept it.
  const start = asString(interaction.payload.mode) === "down_only" ? ui.bottom : ui.top;
  for (const id of ids) {
    if (!ui.top.includes(id) && !ui.bottom.includes(id))
      start.push(id);
  }
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
  return true;
}

function isPlayerClickable(name: string): boolean {
  const interaction = session.interaction;
  if (!interaction)
    return false;
  if (rules.supports(interaction.command)) {
    const result = rules.current(session, rulesSelection()) ? rules.result : null;
    return !!result?.known && result.next_targets.candidates.includes(name);
  }
  if (interaction.command === Command.CHOOSE_PLAYER)
    return asStringList(interaction.payload.players).includes(name);
  return false;
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
  // Enumerated prompts name one recipient; only card-use prompts count votes.
  if (session.interaction && rules.enumerated(session.interaction.command)) {
    ui.selectedPlayers = ui.selectedPlayers.includes(name) ? [] : [name];
    render();
    return;
  }
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

interface FocusSnapshot { key: string; value?: string; start: number | null; end: number | null; request: string; }

function focusKey(node: HTMLElement): string {
  if (node.dataset.focusKey || node.id) return node.dataset.focusKey || node.id;
  if (node.matches(".card")) {
    const zone = node.closest(".hand, .dash-equips, .dash-piles, .interaction-content, .photo-judge, .table-pile");
    return `${zone?.className}:card:${node.dataset.cardId}:${node.dataset.cardLabel}`;
  }
  const optionGroup = node.closest(".general-pick, .interaction-content .cards, .interaction-actions");
  return node instanceof HTMLButtonElement && optionGroup ? `${optionGroup.className}:${node.textContent}` : "";
}

function captureFocus(): FocusSnapshot | null {
  const active = document.activeElement;
  if (!(active instanceof HTMLElement)) return null;
  const key = focusKey(active);
  const input = active instanceof HTMLInputElement || active instanceof HTMLTextAreaElement || active instanceof HTMLSelectElement ? active : null;
  return key ? { key, value: input?.value, start: input && "selectionStart" in input ? input.selectionStart : null,
    end: input && "selectionEnd" in input ? input.selectionEnd : null,
    request: `${session.generation}:${session.interaction?.messageId ?? ""}` } : null;
}

function restoreFocus(snapshot: FocusSnapshot | null): void {
  if (!snapshot) return;
  const target = [...app().querySelectorAll<HTMLElement>("[data-focus-key], [id], button")]
    .find((node) => focusKey(node) === snapshot.key);
  if (!target || target instanceof HTMLButtonElement && target.disabled) return;
  if (target instanceof HTMLButtonElement && snapshot.request !== `${session.generation}:${session.interaction?.messageId ?? ""}`) return;
  // Re-rendering replaces the node; retain in-progress search/chat text as well as caret.
  if (snapshot.value !== undefined && (target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement || target instanceof HTMLSelectElement))
    target.value = snapshot.value;
  target.focus({ preventScroll: true });
  if (snapshot.start !== null && (target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement))
    target.setSelectionRange(snapshot.start, snapshot.end ?? snapshot.start);
}

function finishRender(shell: HTMLElement, focus: FocusSnapshot | null): void {
  const timer = shell.querySelector<HTMLElement>(".interaction-deadline");
  if (timer) {
    // Never restart the gameplay clock on a selection or on a resize.
    const update = () => {
      const remaining = session.remainingInteractionMs();
      timer.hidden = remaining === null;
      if (remaining !== null) timer.textContent = `剩餘 ${Math.ceil(remaining / 1000)} 秒`;
    };
    update();
    countdownInterval = setInterval(update, 250);
  }
  restoreFocus(focus);
}

function readablePhase(): string {
  if (asBool(session.state.gameValue("game_over"))) return "對局已結束";
  switch (session.phase) {
    case "connecting": return "正在連線";
    case "hello": return "正在核對規則";
    case "signup": return "正在加入房間";
    case "setup": return "房間準備中";
    case "active": return "對局進行中";
    case "finished": return "對局已完成";
    case "failed": return "連線中斷";
    default: return "尚未連線";
  }
}

function startSolo(options: import("./solo-client").SoloOptions): void {
  ui.name = localStorage.getItem("qsan-name") || "web-player";
  ui.avatar = localStorage.getItem("qsan-avatar") || "caocao";
  session.connect({
    wsUrl: "local",
    screenName: ui.name,
    avatar: ui.avatar,
    reconnect: false,
    local: true,
    transportFactory: () => solo.createTransport(options)
  });
}

function returnHome(): void {
  if (!session.isLocal) return;
  void solo.close().finally(() => window.location.reload());
}

const rules = new RulesController(() => render());
session.setRulesProvider((activeSession, hello) => rules.initialize(activeSession, hello));
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
  const focus = captureFocus();
  disposeBoardLayout();
  disposeBoardLayout = () => {};
  clearInterval(countdownInterval);
  const request = `${session.generation}:${session.interaction?.messageId ?? ""}`;
  if (request !== selectionRequest) {
    selectionRequest = request;
    resetSelection();
  }
  seedSelection();
  rules.update(session, rulesSelection());
  const root = app();
  root.replaceChildren();
  const shell = el("div", { class: "app" });
  if (session.phase === "idle" || session.phase === "connecting" || session.phase === "failed") {
    applySceneBackground(lobbyBackgroundUrl());
    const logo = assetImg(["/assets/logo/logo.png"], "", "logo");
    logo.alt = "QSanguosha";
    shell.className = "app idle";
    const intro = el("section", { class: "home-hero", "aria-labelledby": "home-title" }, [
      logo,
      assetImg(fullskinUrls("caocao"), "", "hero-portrait"),
      el("p", { class: "eyebrow" }, ["QSANGUOSHA · ONLINE / SOLO"]),
      el("h1", { id: "home-title" }, ["太陽神三國殺", el("span", {}, ["時語版"])]),
      el("p", { class: "hero-copy" }, ["在熟悉的牌局裡，與朋友相逢；也可以隨時開一局單機，讓策略從第一張牌開始。"]),
      el("div", { class: "hero-art-note" }, ["選好座位，讓每一手牌說話。"])
    ]);
    const entries = el("section", { class: "home-entries", "aria-label": "開始遊戲" }, [connectForm(bind)]);
    if (soloAvailable)
      entries.append(soloSetup({ controller: solo, session, name: ui.name, avatar: ui.avatar,
        render, start: startSolo, home: () => render() }));
    shell.append(intro, entries);
    root.append(shell);
    finishRender(shell, focus);
    return;
  }
  const toolbar = el("div", { class: "toolbar" });
  if (!session.isLocal)
    toolbar.append(sharePanel(bind, true));
  if (session.isLocal)
    toolbar.append(localReturnHome({ controller: solo, session, name: ui.name, avatar: ui.avatar,
      render, start: startSolo, home: returnHome }));
  toolbar.append(el("strong", {}, ["QSanguosha"]));
  toolbar.append(el("span", { class: "status" }, [
    `${readablePhase()}${asString(session.state.connectionValue("room_id")) ? ` · 房號 ${asString(session.state.connectionValue("room_id"))}` : ""}`
  ]));
  const tableBg = asString(session.state.gameValue("table_bg")) || defaultTableBgUrl();
  applySceneBackground(tableBg);
  if (!asBool(session.state.gameValue("started"))
      && !asBool(session.state.gameValue("game_over"))) {
    if (session.isLocal && session.phase === "active" && localWaitingGeneration !== session.generation) {
      localWaitingGeneration = session.generation;
      session.addRobots();
      session.setReady(true);
    }
    shell.className = "app wait";
    shell.append(toolbar, waitingRoom(bind));
    if (session.interaction) {
      const view = interactionView(bind);
      view.classList.add("wait-panel");
      shell.append(view);
    }
    root.append(shell);
    finishRender(shell, focus);
    return;
  }
  shell.className = "app room";
  shell.append(toolbar, tableView(bind), logView(bind), dashboardView(bind));
  root.append(shell);
  disposeBoardLayout = setupBoardLayout(shell, bind);
  finishRender(shell, focus);
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
    disposeBoardLayout();
    clearInterval(countdownInterval);
    solo.terminate();
    // A back/forward-cache restoration resumes this same controller instance.
    if (!event.persisted)
      rules.dispose();
  });
  window.addEventListener("pageshow", (event) => {
    if (event.persisted && soloAvailable)
      window.location.reload();
  });
  void fetch("/solo/enabled.json", { cache: "no-store" }).then((response) => {
    if (!response.ok) return null;
    return response.json() as Promise<{ schema_version?: number }>;
  }).then((marker) => {
    soloAvailable = marker?.schema_version === 1;
    render();
  }).catch(() => render());
  render();
}
