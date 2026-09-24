import { Command, asBool, asNumberList, asString, asStringList } from "./protocol";
import { LiveSession, defaultWsUrl, parseRoute } from "./session";
import { RulesController } from "./rules-client";
import { tr } from "./i18n";
import {
  applySceneBackground,
  defaultTableBgUrl,
  lobbyBackgroundUrl
} from "./backdrop";
import { assetImg, fullskinUrls } from "./assets";
import { cardLabel } from "./ui-cards";
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
    const model = rules.current(session, rulesSelection()) ? rules.actionModel() : null;
    return !!model?.supported && model.cards.some((card) => card.id === String(cardId) && card.enabled);
  }
  return true;
}

function isPlayerClickable(name: string): boolean {
  const interaction = session.interaction;
  if (!interaction)
    return false;
  if (rules.supports(interaction.command)) {
    const model = rules.current(session, rulesSelection()) ? rules.actionModel() : null;
    return !!model?.supported && model.players.some((player) => player.id === name && player.enabled);
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
      if (remaining !== null) timer.textContent = `剩余 ${Math.ceil(remaining / 1000)} 秒`;
    };
    update();
    countdownInterval = setInterval(update, 250);
  }
  restoreFocus(focus);
}

function readablePhase(): string {
  if (asBool(session.state.gameValue("game_over"))) return "对局已结束";
  switch (session.phase) {
    case "connecting": return "正在连线";
    case "hello": return "正在核对规则";
    case "signup": return "正在加入房间";
    case "setup": return "房间准备中";
    case "active": return "对局进行中";
    case "finished": return "对局已完成";
    case "failed": return "连线中断";
    default: return "尚未连线";
  }
}

let snapshotPanel: HTMLDetailsElement | null = null;

function accessibleSnapshot(bind: UiBind): HTMLElement {
  const { session, rules, ui } = bind;
  const presentation = ui.presentation ??= {};
  if (snapshotPanel) return snapshotPanel;
  const panel = el("details", { class: "accessible-snapshot" });
  panel.open = presentation.snapshotOpen === true;
  panel.addEventListener("toggle", () => { presentation.snapshotOpen = panel.open; });
  panel.append(el("summary", {}, ["游戏状态文字快照"]));
  const notice = el("p", { role: "status" }, [presentation.snapshotNotice || "按更新快照读取共享状态。"]);
  const snapshotText = el("textarea", { readonly: "", tabindex: "0", "aria-label": "冻结的游戏状态文字快照" });
  snapshotText.dataset.focusKey = "accessible-snapshot-text";
  snapshotText.rows = 12;
  snapshotText.value = presentation.accessibleSnapshot || "尚未建立快照。";
  const refresh = el("button", { type: "button" }, ["更新快照"]);
  refresh.dataset.focusKey = "accessible-snapshot-refresh";
  refresh.addEventListener("click", () => {
    const view = rules.currentPresentation();
    if (!view) {
      presentation.snapshotNotice = "共享状态尚未更新完成，请稍后重试。";
      notice.textContent = presentation.snapshotNotice;
      return;
    }
    const model = rules.current(session, rulesSelection()) ? rules.actionModel() : null;
    const actionLines = model ? [
      ...model.cards.filter((item) => item.enabled).map((item) => {
        const id = Number(item.id);
        return `可选卡牌：${Number.isSafeInteger(id) ? cardLabel(bind, id) : item.label}（${item.id}）`;
      }),
      ...model.players.filter((item) => item.enabled).map((item) => {
        const player = session.state.player(item.id);
        const general = asString(player?.general) || asString(player?.avatar);
        const screen = asString(player?.screen_name, item.id);
        return `可选目标：${general ? `${tr(general)}（${screen}）` : screen}`;
      }),
      ...model.skills.filter((item) => item.enabled).map((item) => `可用技能：${tr(item.label)}`),
      ...model.actions.filter((item) => item.enabled).map((item) => `可用选项：${tr(item.label)}`),
      model.can_confirm ? "可确认目前选择" : "目前不能确认",
      model.can_cancel ? "可取消" : "不能取消",
      model.can_finish ? "可结束出牌阶段" : ""
    ].filter(Boolean) : ["目前没有可用的共享操作模型；规则尚未判定。"];
    const events = view.events.slice(-10).map((event) => event.text).filter(Boolean);
    presentation.accessibleSnapshot = [view.plain_text, "可用操作：", ...actionLines,
      ...(events.length ? ["近期事件：", ...events] : [])].join("\n");
    presentation.snapshotNotice = "快照已冻结；按更新快照以读取较新的状态。";
    snapshotText.value = presentation.accessibleSnapshot;
    notice.textContent = presentation.snapshotNotice;
    copy.disabled = false;
    snapshotText.focus({ preventScroll: true });
    snapshotText.setSelectionRange(0, 0);
  });
  const copy = el("button", { type: "button" }, ["复制快照"]);
  copy.dataset.focusKey = "accessible-snapshot-copy";
  copy.disabled = !presentation.accessibleSnapshot;
  copy.addEventListener("click", () => {
    const text = presentation.accessibleSnapshot;
    if (!text) return;
    const clipboard = navigator.clipboard;
    if (!clipboard || typeof clipboard.writeText !== "function") {
      presentation.snapshotNotice = "此连线环境无法存取剪贴簿；可直接选取快照文字。";
      notice.textContent = "此连线环境无法存取剪贴簿；可直接选取快照文字。";
      return;
    }
    void clipboard.writeText(text).then(() => {
      presentation.snapshotNotice = "快照已复制。";
      notice.textContent = presentation.snapshotNotice;
    }).catch(() => {
      presentation.snapshotNotice = "无法使用剪贴簿；可直接选取快照文字。";
      notice.textContent = presentation.snapshotNotice;
    });
  });
  panel.append(el("div", { class: "snapshot-content" }, [refresh, copy, notice, snapshotText]));
  snapshotPanel = panel;
  return panel;
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
  const root = app();
  const snapshot = accessibleSnapshot(bind);
  if (snapshot.parentElement !== root)
    root.append(snapshot);
  root.querySelector<HTMLElement>(":scope > .app")?.remove();
  seedSelection();
  rules.update(session, rulesSelection());
  const shell = el("div", { class: "app" });
  if (session.phase === "idle" || session.phase === "connecting" || session.phase === "failed") {
    snapshot.hidden = true;
    applySceneBackground(lobbyBackgroundUrl());
    const logo = assetImg(["/assets/logo/logo.png"], "", "logo");
    logo.alt = "QSanguosha";
    shell.className = "app idle";
    const intro = el("section", { class: "home-hero", "aria-labelledby": "home-title" }, [
      logo,
      assetImg(fullskinUrls("caocao"), "", "hero-portrait"),
      el("p", { class: "eyebrow" }, ["QSANGUOSHA · ONLINE / SOLO"]),
      el("h1", { id: "home-title" }, ["太阳神三国杀", el("span", {}, ["时语版"])]),
      el("p", { class: "hero-copy" }, ["在熟悉的牌局里，与朋友相逢；也可以随时开一局单机，让策略从第一张牌开始。"]),
      el("div", { class: "hero-art-note" }, ["选好座位，让每一手牌说话。"])
    ]);
    const entries = el("section", { class: "home-entries", "aria-label": "开始游戏" }, [connectForm(bind)]);
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
    `${readablePhase()}${asString(session.state.connectionValue("room_id")) ? ` · 房号 ${asString(session.state.connectionValue("room_id"))}` : ""}`
  ]));
  const tableBg = asString(session.state.gameValue("table_bg")) || defaultTableBgUrl();
  applySceneBackground(tableBg);
  if (!asBool(session.state.gameValue("started"))
      && !asBool(session.state.gameValue("game_over"))) {
    snapshot.hidden = true;
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
  snapshot.hidden = false;
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
