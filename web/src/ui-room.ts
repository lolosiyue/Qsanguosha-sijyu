import {
  assetImg,
  fullskinUrls,
  kingdomIconUrls,
  magatamaUrl,
  roleIconUrls
} from "./assets";
import { playerHandLabel, targetRangeLabel } from "./player-metrics";
import { formatPresentationEvent, logPlayerName } from "./log-text";
import { tr } from "./i18n";
import {
  Command,
  PLACE_DELAYED_TRICK,
  PLACE_EQUIP,
  PLACE_HAND,
  PLACE_JUDGE,
  PLACE_TABLE,
  asBool,
  asNumber,
  asNumberList,
  asString,
  asStringList,
  isObject
} from "./protocol";
import type { PlayerState, PresentationEvent } from "./state";
import type { RulesSkill } from "./rules-client";
import { cardLabel, renderCard, skillBaseName, visibleSkills } from "./ui-cards";
import { el } from "./ui-dom";
import { interactionView } from "./ui-interaction";
import type { UiBind } from "./ui-types";
import { nativeSeatRegion, seatLabel, seatRing } from "./ui-seat-layout";
import { resultRoleLabel, summarizeGameResult } from "./game-result";

function playerGeneralName(player: PlayerState | undefined): string {
  return asString(player?.general) || asString(player?.avatar);
}

const SKILL_STATUS_TEXT: Record<string, string> = {
  missing_skill: "此規則套件沒有這個技能",
  invalid_instance: "此技能實例已失效或不屬於你",
  unavailable: "目前條件不允許發動",
  unknown: "規則尚未判定"
};

// Why a candidate is greyed out, in the runtime's own words. Presentation only:
// the button is enabled by `available`, never by this text.
function skillHint(skill: RulesSkill | undefined, description: string): string {
  if (!skill)
    return description;
  const notes: string[] = [];
  if (!skill.available)
    notes.push(SKILL_STATUS_TEXT[skill.status] ?? "目前無法發動");
  if (skill.subcard_min >= 0)
    notes.push(skill.subcard_min === skill.subcard_max
      ? `子卡 ${skill.subcard_min} 張` : `子卡 ${skill.subcard_min}–${skill.subcard_max} 張`);
  if (skill.usage_scope !== "none" && skill.usage_used >= 0)
    notes.push(`已用 ${skill.usage_used} 次`);
  if (skill.invalid)
    notes.push("實例失效");
  return [description, ...notes].filter((line) => line).join("\n");
}

function skillDescription(skillName: string): string {
  if (!skillName)
    return "";
  const base = skillBaseName(skillName);
  const key = `:${base}`;
  const text = tr(key);
  if (text === key)
    return "";
  return text.replace(/<[^>]+>/gu, "").replace(/&nbsp;/gu, " ").trim();
}

function photoCard(bind: UiBind, name: string, kind: "photo" | "dash"): HTMLElement {
  const { session, ui } = bind;
  const player = session.state.player(name);
  const general = playerGeneralName(player);
  const selected = ui.selectedPlayers.includes(name);
  const clickable = bind.isPlayerClickable(name);
  const nativeRules = !!session.interaction && bind.rules.supports(session.interaction.command);
  const gameOver = asBool(session.state.gameValue("game_over"));
  const evaluation = nativeRules && bind.rules.current(session, bind.rulesSelection())
    ? bind.rules.result : null;
  const button = el("button", {
    class: `${kind}${name === session.state.selfName ? " self" : ""}${player?.alive === false ? " dead" : ""}${selected ? " selected" : ""}${(gameOver || kind === "photo" && !clickable) ? " disabled" : ""}`
  });
  button.type = "button";
  button.dataset.focusKey = `player-${name}`;
  button.setAttribute("aria-pressed", String(selected));
  button.classList.toggle("current", asString(session.state.gameValue("current_player")) === name);
  button.classList.toggle("focused", asStringList(session.state.gameValue("focus")).includes(name));
  button.disabled = gameOver;
  const art = el("div", { class: "photo-art" });
  if (general)
    art.append(assetImg(fullskinUrls(general), "", "fullskin"));
  const kingdom = asString(player?.kingdom);
  if (kingdom)
    art.append(assetImg(kingdomIconUrls(kingdom), "", "kingdom"));
  const role = asString(player?.role);
  if (role)
    art.append(assetImg(roleIconUrls(role), "", "role"));
  const hp = asNumber(player?.hp);
  const maxHp = asNumber(player?.max_hp);
  const mag = el("div", { class: "magatamas" });
  mag.append(assetImg([magatamaUrl(hp)], "", "magatama"));
  mag.append(el("span", {}, [`${hp}/${maxHp}`]));
  const meta = el("div", { class: "photo-meta" });
  meta.append(
    el("strong", { class: "screen-name" }, [asString(player?.screen_name, name)]),
    el("div", { class: "general-name" }, [tr(general)]),
    mag,
    el("div", { class: "hand-count" }, [playerHandLabel(session.state, name, evaluation)])
  );
  if (name !== session.state.selfName)
    meta.append(el("div", { class: "range-info" }, [
      targetRangeLabel(session.state, session.state.selfName, name, evaluation)
    ]));
  art.append(meta);
  const marks = isObject(player?.marks)
    ? Object.entries(player.marks).filter(([, value]) => asNumber(value) !== 0)
      .map(([key, value]) => `${tr(key)}×${asNumber(value)}`).join("、")
    : "";
  const equipCount = session.state.cardsForPlayer(name, PLACE_EQUIP).length;
  const judgeCount = session.state.cardsForPlayer(name, PLACE_DELAYED_TRICK).length;
  const equipNames = session.state.cardsForPlayer(name, PLACE_EQUIP)
    .map((id) => cardLabel(bind, id)).filter(Boolean).join("、");
  const pileValue = player?.piles;
  const pileText = isObject(pileValue)
    ? Object.entries(pileValue).map(([key, value]) => `${tr(key)} ${asNumberList(value).length}`)
      .filter((item) => !item.endsWith(" 0")).join("、")
    : "";
  const focusPlayers = asStringList(session.state.gameValue("focus"));
  const phase = asString(player?.phase);
  const phaseLabel: Record<string, string> = {
    round_start: "回合開始", start: "開始", judge: "判定", draw: "摸牌",
    play: "出牌", discard: "棄牌", finish: "回合結束", not_active: "非行動"
  };
  const stateText = [
    asNumber(player?.seat, -1) > 0 ? `第 ${asNumber(player?.seat)} 席` : "座次未提供",
    player?.alive === false ? "已死亡" : "",
    selected ? "✓ 已選目標" : "",
    kingdom ? `勢力 ${tr(kingdom)}` : "",
    role ? `身分 ${tr(role)}` : "",
    player?.chained === true ? "連環" : "",
    player?.faceup === false ? "背面" : "",
    marks ? `標記 ${marks}` : "",
    equipNames ? `裝備 ${equipNames}` : `裝備 ${equipCount}`,
    `判定 ${judgeCount}`,
    pileText ? `牌堆 ${pileText}` : "",
    phase ? `階段 ${phaseLabel[phase] ?? phase}` : "",
    asString(session.state.gameValue("current_player")) === name ? "目前回合" : "",
    focusPlayers.includes(name) ? "目前焦點" : ""
  ].filter(Boolean).join(" · ");
  button.append(art);
  if (stateText)
    button.append(el("div", { class: "photo-details" }, [stateText]));
  button.addEventListener("click", () => bind.togglePlayer(name));
  const wrap = el("div", { class: `${kind}-wrap` });
  wrap.dataset.player = name;
  wrap.dataset.alive = player?.alive === false ? "false" : "true";
  wrap.dataset.seat = String(asNumber(player?.seat, -1));
  const handCount = session.state.playerValue(name, "hand_count");
  if (typeof handCount === "number" || typeof handCount === "string")
    wrap.dataset.handCount = String(asNumber(handCount));
  wrap.setAttribute("aria-label", seatLabel(bind, name, 0));
  wrap.append(button);
  const tricks = session.state.cardsForPlayer(name, PLACE_DELAYED_TRICK);
  if (tricks.length) {
    const judge = el("div", { class: "photo-judge" });
    for (const id of tricks)
      judge.append(renderCard(bind, id, false, false, -1, false));
    wrap.append(judge);
  }
  return wrap;
}

function tablePileView(bind: UiBind): HTMLElement {
  const pile = el("div", { class: "table-pile" });
  const tableIds = bind.session.state.cardsAtPlace(PLACE_TABLE)
    .filter((id) => asString(bind.session.state.card(id)?.pile) !== "ren_pile");
  const renIds = bind.session.state.cardsAtPlace(PLACE_TABLE, "ren_pile");
  const judgeIds = bind.session.state.cardsAtPlace(PLACE_JUDGE);
  const appendGroup = (label: string, ids: number[]) => {
    if (ids.length === 0)
      return;
    pile.append(el("span", { class: "status" }, [label]));
    for (const id of ids)
      pile.append(renderCard(bind, id, false, false, -1, false));
  };
  appendGroup("處理區", tableIds);
  appendGroup(tr("ren_pile") === "ren_pile" ? "仁" : tr("ren_pile"), renIds);
  appendGroup("判定", judgeIds);
  if (!pile.childElementCount)
    pile.append(el("span", { class: "status" }, ["桌面空"]));
  return pile;
}

export function tableView(bind: UiBind): HTMLElement {
  const { ui } = bind;
  const table = el("div", { class: "table" });
  if (asBool(bind.session.state.gameValue("game_over"))) {
    // Keep long winner lists and the final board reachable on small viewports.
    table.classList.add("game-over");
    table.tabIndex = 0;
    table.append(gameResultView(bind));
  }
  const seats = el("div", { class: "photos seat-ring", role: "list", "aria-label": "座位環" });
  const ring = seatRing(bind);
  const seatFilter = bind.session.interaction ? (ui.presentation?.seatFilter ?? "all") : "all";
  const legalCount = ring.filter((name) => name !== bind.session.state.selfName
    && bind.isPlayerClickable(name)).length;
  ring.forEach((name, index) => {
    if (name === bind.session.state.selfName)
      return;
    const card = photoCard(bind, name, "photo");
    card.dataset.seatOrder = String(index);
    card.dataset.region = nativeSeatRegion(bind, name);
    card.dataset.legal = bind.isPlayerClickable(name) ? "true" : "false";
    card.setAttribute("role", "listitem");
    card.setAttribute("aria-label", seatLabel(bind, name, index));
    seats.append(card);
  });
  // Keep every card in native ring order; CSS applies legal filtering only in strip mode.
  seats.dataset.filter = seatFilter;
  if (!ring.some((name) => name !== bind.session.state.selfName))
    seats.append(el("p", { class: "status" }, ["等待其他角色"]));
  const seatToolbar = el("nav", { class: "seat-toolbar", "aria-label": "座位巡覽" });
  const focus = uiFocusPlayer(bind);
  const filterButton = el("button", { type: "button", class: seatFilter === "legal" ? "active" : "" },
    [seatFilter === "legal" ? `可選目標 ${legalCount} / ${ring.length}` : `全部座位（可選 ${legalCount}）`]);
  filterButton.disabled = !bind.session.interaction;
  filterButton.setAttribute("aria-pressed", seatFilter === "legal" ? "true" : "false");
  filterButton.addEventListener("click", () => {
    ui.presentation ??= {};
    ui.presentation.seatFilter = seatFilter === "legal" ? "all" : "legal";
    bind.render();
  });
  seatToolbar.append(filterButton);
  const currentIndex = () => {
    const saved = bind.ui.presentation?.browsedSeatIndex ?? 0;
    return ring.length ? ((saved % ring.length) + ring.length) % ring.length : -1;
  };
  const move = (step: number) => {
    if (!ring.length || seatFilter === "legal" && !legalCount) return;
    let nextIndex = (currentIndex() + step + ring.length) % ring.length;
    for (let tried = 0; seatFilter === "legal" && tried < ring.length && !bind.isPlayerClickable(ring[nextIndex]); ++tried)
      nextIndex = (nextIndex + step + ring.length) % ring.length;
    ui.presentation ??= {};
    ui.presentation.browsedSeatIndex = nextIndex;
    const next = ring[nextIndex];
    const node = seats.querySelector<HTMLElement>(`[data-player="${CSS.escape(next)}"]`);
    if (node) seats.scrollLeft = node.offsetLeft - (seats.clientWidth - node.clientWidth) / 2;
  };
  const jump = (name: string) => {
    ui.presentation ??= {};
    if (name === bind.session.state.selfName) {
      document.querySelector<HTMLElement>(".dashboard .dash")?.focus({ preventScroll: true });
      return;
    }
    if (seatFilter === "legal" && !bind.isPlayerClickable(name)) {
      ui.presentation.seatFilter = "all";
      ui.presentation.focusPlayer = "";
      bind.render();
      return;
    }
    ui.presentation.browsedSeatIndex = Math.max(0, ring.indexOf(name));
    const node = seats.querySelector<HTMLElement>(`[data-player="${CSS.escape(name)}"]`);
    if (node) seats.scrollLeft = node.offsetLeft - (seats.clientWidth - node.clientWidth) / 2;
  };
  for (const [label, action] of [["上一位", () => move(-1)], ["下一位", () => move(1)],
    ["目前焦點", () => { if (focus) jump(focus); }], ["回到自己", () => {
      ui.presentation ??= {};
      ui.presentation.browsedSeatIndex = -1;
      document.querySelector<HTMLElement>(".dashboard .dash")?.focus({ preventScroll: true });
    }] ] as const) {
    const button = el("button", { type: "button" }, [label]);
    button.disabled = label === "目前焦點" && !focus;
    button.addEventListener("click", action);
    seatToolbar.append(button);
  }
  table.append(seatToolbar);
  const center = el("div", { class: "table-center" });
  center.append(tablePileView(bind));
  table.append(seats, center);
  return table;
}

function uiFocusPlayer(bind: UiBind): string {
  const focus = asStringList(bind.session.state.gameValue("focus"));
  if (focus.length)
    return focus[0];
  return asString(bind.session.state.gameValue("current_player"));
}

export function gameResultView(bind: UiBind): HTMLElement {
  const { session } = bind;
  const payload = session.state.gameValue("result");
  const result = isObject(payload)
    ? summarizeGameResult(payload, session.state.playerNames, session.state.selfName)
    : { draw: false, winners: [], selfOutcome: "unknown" as const };
  const panel = el("section", { class: "game-result", "aria-labelledby": "game-result-heading" });
  panel.append(el("h2", { id: "game-result-heading" }, [result.draw ? "平局" : "遊戲結束"]));
  if (result.selfOutcome === "victory")
    panel.append(el("p", { class: "game-result-outcome victory" }, ["你方勝利"]));
  else if (result.selfOutcome === "defeat")
    panel.append(el("p", { class: "game-result-outcome defeat" }, ["你方敗北"]));
  if (result.draw)
    panel.append(el("p", { class: "status" }, ["本局沒有勝方"]));
  else if (result.winners.length) {
    panel.append(el("p", { class: "status" }, ["勝方"]));
    const list = el("ul", { class: "game-result-winners" });
    for (const winner of result.winners) {
      const name = winner.objectName
        ? (logPlayerName(session.state, winner.objectName) || winner.objectName)
        : "";
      const role = winner.role ? `（${resultRoleLabel(winner.role)}）` : "";
      list.append(el("li", {}, [`${name || resultRoleLabel(winner.role)}${name ? role : ""}`]));
    }
    panel.append(list);
  } else
    panel.append(el("p", { class: "status" }, ["勝方資料未提供"]));
  return panel;
}

export function logView(bind: UiBind): HTMLElement {
  const { session, ui } = bind;
  const panel = el("section", { class: "log-panel", "aria-label": "戰報與房內聊天" });
  const battle = el("div", { class: "log battle-log", role: "log", "aria-label": "戰報" });
  const chat = el("div", { class: "log chat-log", role: "log", "aria-label": "房內聊天" });
  ui.presentation ??= {};
  ui.presentation.mobileLogCollapsed ??= window.matchMedia("(max-width: 719px), (orientation: portrait)").matches;
  const filter = ui.presentation.battleFilter ?? "all";
  const filterBar = el("div", { class: "battle-filters", role: "toolbar", "aria-label": "戰報篩選" });
  const filterNames: Array<[typeof filter, string]> = [["all", "全部"], ["mine", "我的"], ["damage", "傷害"], ["skill", "技能"]];
  const categoriesOf = (event: PresentationEvent): string[] => {
    const payload = event.payload ?? {};
    const type = asString(payload.log_type);
    const from = asString(payload.from_player);
    const targets = asStringList(payload.to_players);
    const mine = from === session.state.selfName || targets.includes(session.state.selfName);
    const damage = /#(?:Damage|LoseHp|Recover|GetHp|ChangeHp)/u.test(type)
      || event.command === Command.CHANGE_HP;
    const skill = /#(?:UseSkill|InvokeSkill|Skill)/u.test(type);
    const categories: string[] = [];
    if (mine) categories.push("mine");
    if (damage) categories.push("damage");
    if (skill) categories.push("skill");
    return categories.length ? categories : ["all"];
  };
  for (const [value, label] of filterNames) {
    const button = el("button", { type: "button", class: value === filter ? "active" : "" }, [label]);
    button.addEventListener("click", () => {
      ui.presentation ??= {};
      ui.presentation.battleFilter = value;
      bind.render();
    });
    filterBar.append(button);
  }
  // Bound each stream independently so a busy battle log cannot erase chat history.
  const events = session.state.presentationEvents;
  const visibleEvents = [
    ...events.filter((event) => event.command !== Command.SPEAK).slice(-80),
    ...events.filter((event) => event.command === Command.SPEAK).slice(-80)
  ];
  for (const event of visibleEvents) {
    const line = formatPresentationEvent(event, (name) =>
      event.command === Command.SPEAK
        ? (asString(session.state.player(name)?.screen_name) || name)
        : logPlayerName(session.state, name));
    if (!line)
      continue;
    const target = event.command === Command.SPEAK ? chat : battle;
    const item = el("p", { "data-category": categoriesOf(event).join(" ") }, [line]);
    if (target === battle && filter !== "all" && !item.dataset.category?.split(" ").includes(filter))
      item.hidden = true;
    target.append(item);
  }
  battle.addEventListener("scroll", () => {
    ui.logPinned = battle.scrollHeight - battle.scrollTop - battle.clientHeight < 32;
    ui.presentation ??= {};
    ui.presentation.battleScrollTop = battle.scrollTop;
  });
  chat.addEventListener("scroll", () => {
    ui.presentation ??= {};
    ui.presentation.chatScrollTop = chat.scrollTop;
    ui.presentation.chatPinned = chat.scrollHeight - chat.scrollTop - chat.clientHeight < 32;
  });
  requestAnimationFrame(() => {
    if (ui.logPinned)
      battle.scrollTop = battle.scrollHeight;
    else
      battle.scrollTop = ui.presentation?.battleScrollTop ?? 0;
    if (ui.presentation?.chatPinned ?? true)
      chat.scrollTop = chat.scrollHeight;
    else
      chat.scrollTop = ui.presentation?.chatScrollTop ?? 0;
  });
  const chatForm = el("form", { class: "chat-form" });
  const draft = el("input", { id: "room-chat", class: "chat-draft", "data-focus-key": "room-chat", placeholder: "房內聊天", "aria-label": "房內聊天" });
  draft.value = ui.presentation?.chatDraft ?? "";
  draft.addEventListener("input", () => {
    ui.presentation ??= {};
    ui.presentation.chatDraft = draft.value;
  });
  const send = el("button", { type: "submit" }, ["送出"]);
  chatForm.append(draft, send);
  chatForm.addEventListener("submit", (event) => {
    event.preventDefault();
    const text = draft.value.trim();
    if (!text) return;
    if (session.phase !== "active") return;
    ui.presentation ??= {};
    ui.presentation.chatDraft = "";
    draft.value = "";
    session.chat(text);
  });
  const collapsed = ui.presentation.mobileLogCollapsed === true;
  const collapse = el("button", { type: "button", class: "log-collapse", "aria-expanded": String(!collapsed), "aria-label": collapsed ? "展開戰報與聊天" : "收起戰報與聊天" }, [collapsed ? "展開訊息" : "收起訊息"]);
  collapse.addEventListener("click", () => {
    ui.presentation ??= {};
    ui.presentation.mobileLogCollapsed = !ui.presentation.mobileLogCollapsed;
    bind.render();
  });
  if (collapsed)
    panel.classList.add("is-collapsed");
  panel.append(el("h2", { class: "sr-only" }, ["訊息流"]), collapse, filterBar,
    el("h2", { class: "log-header" }, ["戰報"]), battle,
    el("h2", { class: "log-header" }, ["聊天"]), chat, chatForm);
  return panel;
}

function pileRow(bind: UiBind, player: string): HTMLElement {
  const row = el("div", { class: "dash-piles" });
  const piles = bind.session.state.playerValue(player, "piles");
  if (!isObject(piles))
    return row;
  for (const [name, value] of Object.entries(piles)) {
    const ids = asNumberList(value);
    if (ids.length === 0)
      continue;
    row.append(el("span", { class: "status" }, [tr(name)]));
    for (const id of ids)
      row.append(renderCard(bind, id, bind.ui.selectedCards.includes(id), false, -1, bind.isCardClickable(id), !bind.isCardClickable(id)));
  }
  return row;
}

function skillBar(bind: UiBind): HTMLElement {
  const { ui, session, rules } = bind;
  const row = el("div", { class: "skill-bar" });
  const skills = visibleSkills(bind);
  const nativeRules = !!session.interaction && rules.supports(session.interaction.command);
  const evaluation = nativeRules && rules.current(session, bind.rulesSelection()) ? rules.result : null;
  // A server-named borrowed prompt can expose a ViewAs skill absent from the portrait.
  for (const candidate of evaluation?.skills ?? []) {
    if (!skills.some((skill) => skill.name === candidate.name && skill.instanceId === candidate.instance_id))
      skills.push({ name: candidate.name, instanceId: candidate.instance_id });
  }
  if (nativeRules && ui.selectedOption
      && !skills.some((skill) => skill.name === ui.selectedOption && skill.instanceId === ui.skillInstance))
    skills.push({ name: ui.selectedOption, instanceId: ui.skillInstance });
  if (skills.length === 0)
    return row;
  for (const skill of skills) {
    const selected = ui.selectedOption === skill.name && ui.skillInstance === skill.instanceId;
    const desc = skillDescription(skill.name);
    const detail = evaluation?.known
      ? evaluation.skills.find((candidate) =>
        candidate.name === skill.name && candidate.instance_id === skill.instanceId)
      : undefined;
    const button = el("button", {
      class: `skill-btn${selected ? " primary" : ""}`,
      title: skillHint(detail, desc || tr(skill.name))
    }, [tr(skill.name)]);
    const available = !!detail?.available;
    // Descriptions remain visible; only native ViewAs candidates can be activated.
    if (!selected && (!nativeRules || !available))
      button.disabled = true;
    button.addEventListener("click", () => {
      const current = rules.current(session, bind.rulesSelection()) ? rules.result : null;
      if (!selected && (!current?.known || !current.skills.some((candidate) =>
        candidate.name === skill.name && candidate.instance_id === skill.instanceId && candidate.available)))
        return;
      if (selected) {
        ui.selectedOption = "";
        ui.skillInstance = 0;
      } else {
        ui.selectedOption = skill.name;
        ui.skillInstance = skill.instanceId;
      }
      ui.selectedCards = [];
      ui.selectedPlayers = [];
      ui.ruleDeclaration = "";
      bind.render();
    });
    row.append(button);
  }
  const selectedDesc = skillDescription(ui.selectedOption);
  if (ui.selectedOption)
    row.append(el("p", { class: "skill-desc" }, [
      selectedDesc || `${tr(ui.selectedOption)}（無技能描述）`
    ]));
  return row;
}

function handView(bind: UiBind): HTMLElement {
  const hand = el("div", { class: "hand" });
  const ids = bind.session.state.cardsForPlayer(bind.session.state.selfName, PLACE_HAND);
  hand.style.setProperty("--hand-count", String(ids.length));
  if (ids.length === 0)
    hand.append(el("span", { class: "status" }, ["手牌空"]));
  for (const [index, id] of ids.entries()) {
    const card = el("div", { class: "hand-card" });
    card.style.setProperty("--hand-index", String(index));
    card.append(renderCard(bind, id, bind.ui.selectedCards.includes(id), false, -1, bind.isCardClickable(id), !bind.isCardClickable(id)));
    hand.append(card);
  }
  return hand;
}

export function dashboardView(bind: UiBind): HTMLElement {
  const dash = el("section", { class: "dashboard" });
  const self = bind.session.state.selfName;
  dash.append(photoCard(bind, self, "dash"));
  const body = el("div", { class: "dash-body" });
  const equips = el("div", { class: "dash-equips" });
  const equipIds = bind.session.state.cardsForPlayer(self, PLACE_EQUIP);
  if (equipIds.length === 0)
    equips.append(el("span", { class: "status" }, ["裝備空"]));
  for (const id of equipIds)
    equips.append(renderCard(bind, id, bind.ui.selectedCards.includes(id), false, -1, bind.isCardClickable(id), !bind.isCardClickable(id)));
  body.append(equips, pileRow(bind, self), skillBar(bind), handView(bind));
  dash.append(body);
  const interaction = interactionView(bind);
  interaction.classList.add("dashboard-interaction");
  dash.append(interaction);
  return dash;
}
