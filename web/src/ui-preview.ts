import { loadTranslations, tr } from "./i18n";
import { applySceneBackground, defaultTableBgUrl, loadUiConfig } from "./backdrop";
import { Command, PLACE_HAND, PLACE_EQUIP, PLACE_TABLE, type JsonObject } from "./protocol";
import { LiveSession } from "./session";
import { RulesController } from "./rules-client";
import { el } from "./ui-dom";
import { waitingRoom } from "./ui-connect";
import { interactionView } from "./ui-interaction";
import { dashboardView, logView, tableView } from "./ui-room";
import { setupBoardLayout } from "./ui-seat-layout";
import type { UiBind, UiState } from "./ui-types";

// Development preview only. No socket, worker, or game is started by this entry.
class PreviewSession extends LiveSession {
  override sendReply(_command: number, _replyTo: string, _payload: JsonObject): void {
    this.interactionError = "已確認預覽選擇；此頁使用模擬資料。";
    render();
  }
  override chat(text: string): void {
    this.state.appendPresentationEvent(Command.SPEAK, "", { player_name: "self", text });
    render();
  }
  override setReady(ready: boolean): void {
    this.state.setPlayerValue("self", "ready", ready);
    render();
  }
  override addRobots(): void { playerCount = 8; seed(); render(); }
  override trust(_trusted: boolean): void {}
  override surrender(): void {}
}

const session = new PreviewSession();
const rules = new RulesController(() => render());
const ui: UiState = {
  name: "你的座位", avatar: "caocao", ws: "", reconnect: false,
  selectedCards: [], selectedPlayers: [], selectedOption: "", top: [], bottom: [],
  assignments: {}, qmlText: "{}", skillInstance: 0, ruleDeclaration: "", logPinned: true,
  hiddenIndex: -1, presentation: {}
};
let playerCount = 5;
let scene = "table";
let disposeLayout = () => {};
const generals = ["caocao", "liubei", "sunquan", "guanyu", "zhenji", "zhangfei", "zhaoyun", "diaochan", "zhouyu", "luxun"];
const kingdoms = ["wei", "shu", "wu", "shu", "wei", "shu", "shu", "qun", "wu", "wu"];

function seed(): void {
  session.state.reset();
  session.phase = "active";
  session.state.setSelfName("self");
  session.state.setup = { game_mode: "05p" };
  session.state.setConnectionValue("room_id", 1);
  session.state.setGameValue("started", scene !== "lobby");
  session.state.setGameValue("current_player", "self");
  session.state.setGameValue("focus", ["self"]);
  for (let i = 0; i < playerCount; ++i) {
    const name = i ? `player-${i}` : "self";
    Object.assign(session.state.ensurePlayer(name), {
      screen_name: i ? `${tr(generals[i])} · ${i + 1}號位` : "你的座位",
      general: generals[i], avatar: generals[i], seat: i + 1, kingdom: kingdoms[i],
      role: i === 0 ? "lord" : "unknown", hp: i === 3 ? 2 : 4, max_hp: 4,
      hand_count: i ? 3 + i : 12, phase: i ? "not_active" : "play", ready: i !== 2,
      faceup: true, chained: i === 3, skills: i === 0 ? ["jianxiong", "hujia"] : []
    });
  }
  const hand = ["slash", "jink", "peach", "slash", "dismantlement", "snatch", "duel", "nullification", "jink", "peach", "amazing_grace", "god_salvation"];
  hand.forEach((name, i) => Object.assign(session.state.ensureCard(10000 + i), {
    object_name: name, owner: "self", place: PLACE_HAND, suit: i % 4, number: i + 1
  }));
  Object.assign(session.state.ensureCard(10020), { object_name: "crossbow", owner: "player-1", place: PLACE_EQUIP, suit: 1, number: 1 });
  Object.assign(session.state.ensureCard(10021), { object_name: "eight_diagram", owner: "self", place: PLACE_EQUIP, suit: 0, number: 2 });
  Object.assign(session.state.ensureCard(10022), { object_name: "slash", owner: "", place: PLACE_TABLE, suit: 0, number: 7 });
  session.state.appendPresentationEvent(Command.LOG_SKILL, "", { log_type: "$DrawCards", from_player: "self", to_players: [], card_string: "10000+10001", arguments: ["2"] });
  session.state.appendPresentationEvent(Command.LOG_SKILL, "", { log_type: "#Damage", from_player: "player-1", to_players: ["player-3"], card_string: "", arguments: ["1", "普通"] });
  session.state.appendPresentationEvent(Command.SPEAK, "", { player_name: "player-1", text: "準備好了，開始吧。" });
  session.state.appendPresentationEvent(Command.SPEAK, "", { player_name: "player-2", text: "這裡是房內聊天區。" });
  ui.selectedCards = [];
  ui.selectedPlayers = [];
  ui.selectedOption = "";
  session.interactionError = "";
  session.interaction = scene === "lobby" ? null : {
    command: scene === "generals" ? Command.CHOOSE_GENERAL : Command.CHOOSE_PLAYER,
    messageId: "ui-preview",
    payload: scene === "generals"
      ? { options: generals.slice(0, 5), prompt: "選擇武將，再按確認。" }
      : { players: session.state.playerNames.filter((name) => name !== "self"), prompt: "選擇一名角色，預覽目標選取效果。" }
  };
}

const bind: UiBind = {
  session, rules, ui, route: { reconnect: false }, render,
  currentCardId: () => ui.selectedCards[0] ?? -1,
  rulesSelection: () => ({ card_ids: ui.selectedCards, targets: ui.selectedPlayers,
    skill_name: ui.selectedOption, skill_instance_id: 0, user_string: "", top: [], bottom: [] }),
  isCardClickable: () => false,
  isPlayerClickable: (name) => scene === "table" && name !== "self",
  togglePlayer: (name) => {
    if (!bind.isPlayerClickable(name)) return;
    ui.selectedPlayers = ui.selectedPlayers.includes(name) ? [] : [name];
    render();
  },
  removeTarget: () => { ui.selectedPlayers = []; render(); },
  resetSelection: () => { ui.selectedCards = []; ui.selectedPlayers = []; ui.selectedOption = ""; }
};

function render(): void {
  disposeLayout();
  const root = document.getElementById("app")!;
  const shell = el("div", { class: scene === "table" ? "app room" : "app wait" });
  const toolbar = el("div", { class: "toolbar" }, [el("strong", {}, ["房內 UI 預覽"]), el("span", { class: "status" }, ["模擬資料・未連線"])]);
  const scenes = el("select", { "aria-label": "預覽畫面" });
  for (const [value, label] of [["table", "對局牌桌"], ["lobby", "等待房"], ["generals", "選將畫面"]])
    scenes.append(el("option", { value }, [label]));
  scenes.value = scene;
  scenes.addEventListener("change", () => { scene = scenes.value; seed(); render(); });
  const count = el("select", { "aria-label": "預覽人數" });
  for (const number of [2, 5, 8, 10]) count.append(el("option", { value: String(number) }, [`${number} 人`]));
  count.value = String(playerCount);
  count.addEventListener("change", () => { playerCount = Number(count.value); seed(); render(); });
  toolbar.append(scenes, count, el("a", { href: "/" }, ["返回首頁"]));
  shell.append(toolbar);
  if (scene === "table") shell.append(tableView(bind), logView(bind), dashboardView(bind));
  else if (scene === "lobby") shell.append(waitingRoom(bind));
  else shell.append(interactionView(bind));
  root.replaceChildren(shell);
  disposeLayout = setupBoardLayout(shell, bind);
}

await loadUiConfig();
await loadTranslations();
applySceneBackground(defaultTableBgUrl());
seed();
render();
window.addEventListener("pagehide", () => { disposeLayout(); rules.dispose(); });
