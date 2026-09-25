import {
  assetImg,
  cardNumberUrl,
  cardSuitUrl,
  deathImageUrls,
  equipImageUrls,
  fullskinUrls,
  handCardNumUrls,
  judgeIconUrls,
  kingdomFrameUrls,
  kingdomIconUrls,
  magatamaUrl,
  markIconUrls,
  phaseImageUrl,
  roleIconUrls,
  skillButtonUrl,
  skinImageUrl
} from "./assets";
import { playerHandLabel, targetRangeLabel } from "./player-metrics";
import { tr } from "./i18n";
import { toSimplified } from "./zh-hans";
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
import { cardLabel, cardObjectName, cardSuitNumber, renderCard, skillBaseName, visibleSkills } from "./ui-cards";
import { el } from "./ui-dom";
import { interactionView } from "./ui-interaction";
import type { UiBind } from "./ui-types";
import { nativeSeatRegion, seatLabel, seatRing } from "./ui-seat-layout";
import { resultRoleLabel, summarizeGameResult } from "./game-result";

function playerGeneralName(player: PlayerState | undefined): string {
  return asString(player?.general) || asString(player?.avatar);
}

const SKILL_STATUS_TEXT: Record<string, string> = {
  missing_skill: "此规则套件没有这个技能",
  invalid_instance: "此技能实例已失效或不属于你",
  unavailable: "当前条件不允许发动",
  unknown: "规则尚未判定"
};

// Why a candidate is greyed out, in the runtime's own words. Presentation only:
// the button is enabled by `available`, never by this text.
function skillHint(skill: RulesSkill | undefined, description: string): string {
  if (!skill)
    return description;
  const notes: string[] = [];
  if (!skill.available)
    notes.push(SKILL_STATUS_TEXT[skill.status] ?? "当前无法发动");
  if (skill.subcard_min >= 0)
    notes.push(skill.subcard_min === skill.subcard_max
      ? `子卡 ${skill.subcard_min} 张` : `子卡 ${skill.subcard_min}–${skill.subcard_max} 张`);
  if (skill.usage_scope !== "none" && skill.usage_used >= 0)
    notes.push(`已用 ${skill.usage_used} 次`);
  if (skill.invalid)
    notes.push("实例失效");
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
  const current = asString(session.state.gameValue("current_player")) === name;
  const focusPlayers = asStringList(session.state.gameValue("focus"));
  button.classList.toggle("current", current);
  button.classList.toggle("focused", focusPlayers.includes(name));
  button.disabled = gameOver;
  // Layers follow PlayerCardContainer: avatar, focus frame, main frame, then badges.
  const art = el("div", { class: "photo-art" });
  if (general)
    art.append(assetImg(fullskinUrls(general), "", "fullskin"));
  if (player?.faceup === false)
    art.append(assetImg([skinImageUrl("fullskin/generals/faceturned.png")], "", "face-turned"));
  // The focus frame lies between the avatar and the main frame, as in Photo::updateFocus.
  const frame = selected
    ? (kind === "photo" ? "system/frame/photoSelected.png" : "system/frame/dashboardSelected.png")
    : current ? "system/frame/playing.png" : focusPlayers.includes(name) ? "system/frame/responding.png" : "";
  if (frame)
    art.append(assetImg([skinImageUrl(frame)], "", "focus-frame"));
  art.append(assetImg([skinImageUrl(kind === "photo" ? "fullskin/system/photo-back.png" : "fullskin/system/dashboard-avatar.png")], "", "main-frame"));
  const kingdom = asString(player?.kingdom);
  if (kingdom) {
    art.append(assetImg(kingdomFrameUrls(kingdom, kind === "dash"), "", "kingdom-mask"));
    art.append(assetImg(kingdomIconUrls(kingdom), "", "kingdom"));
  }
  if (general)
    art.append(el("span", { class: "general-name" }, [tr(general)]));
  if (kind === "photo")
    art.append(el("span", { class: "screen-name" }, [asString(player?.screen_name, name)]));
  const role = asString(player?.role);
  if (role)
    art.append(assetImg(roleIconUrls(role), "", "role"));
  const handCount = session.state.playerValue(name, "hand_count");
  art.append(el("span", { class: "hand-count" }, [
    assetImg(handCardNumUrls(kingdom), "", "hand-count-bg"),
    el("span", {}, [String(asNumber(handCount))])
  ]));
  art.append(magatamaBox(asNumber(player?.hp), asNumber(player?.max_hp)));
  if (player?.chained === true)
    art.append(assetImg([skinImageUrl("system/chain.png")], "", "chain"));
  const markEntries = isObject(player?.marks) ? Object.entries(player.marks) : [];
  // Only @ marks are drawn, as images, exactly like ClientPlayer::setMark.
  const markIcons = markEntries.filter(([key, value]) => key.startsWith("@") && asNumber(value) > 0);
  if (markIcons.length) {
    const row = el("span", { class: "photo-marks" });
    for (const [key, value] of markIcons) {
      const count = asNumber(value);
      const mark = el("span", { class: "mark", title: count > 1 ? `${tr(key)} ${count}` : tr(key) },
        [assetImg(markIconUrls(key), "", "mark-icon")]);
      if (count > 1)
        mark.append(el("span", { class: "mark-count" }, [String(count)]));
      row.append(mark);
    }
    art.append(row);
  }
  const equipIds = session.state.cardsForPlayer(name, PLACE_EQUIP);
  if (kind === "photo" && equipIds.length) {
    const equips = el("span", { class: "photo-equips" });
    for (const id of equipIds)
      equips.append(equipBar(bind, id));
    art.append(equips);
  }
  const tricks = session.state.cardsForPlayer(name, PLACE_DELAYED_TRICK);
  if (kind === "photo" && tricks.length)
    art.append(judgeIcons(bind, tricks));
  const phase = asString(player?.phase);
  if (phase && phase !== "not_active" && phaseImageUrl(phase))
    art.append(assetImg([phaseImageUrl(phase)], "", "phase"));
  if (player?.alive === false)
    art.append(assetImg(deathImageUrls(role || "unknown"), "", "death"));
  const marks = markEntries.filter(([, value]) => asNumber(value) !== 0)
    .map(([key, value]) => `${tr(key)}×${asNumber(value)}`).join("、");
  const equipNames = equipIds.map((id) => cardLabel(bind, id)).filter(Boolean).join("、");
  const pileValue = player?.piles;
  const pileText = isObject(pileValue)
    ? Object.entries(pileValue).map(([key, value]) => `${tr(key)} ${asNumberList(value).length}`)
      .filter((item) => !item.endsWith(" 0")).join("、")
    : "";
  const phaseLabel: Record<string, string> = {
    round_start: "回合开始", start: "开始", judge: "判定", draw: "摸牌",
    play: "出牌", discard: "弃牌", finish: "回合结束", not_active: "非行动"
  };
  // Screen readers keep the full state that the artwork above only shows visually.
  const stateText = [
    asNumber(player?.seat, -1) > 0 ? `${asNumber(player?.seat)} 号位` : "座次未提供",
    player?.alive === false ? "已死亡" : "",
    selected ? "已选为目标" : "",
    kingdom ? `势力 ${tr(kingdom)}` : "",
    role ? `身份 ${tr(role)}` : "",
    `体力 ${asNumber(player?.hp)}/${asNumber(player?.max_hp)}`,
    player?.chained === true ? "连环" : "",
    player?.faceup === false ? "背面" : "",
    marks ? `标记 ${marks}` : "",
    equipNames ? `装备 ${equipNames}` : `装备 ${equipIds.length}`,
    `判定 ${tricks.length}`,
    pileText ? `牌堆 ${pileText}` : "",
    phase ? `阶段 ${phaseLabel[phase] ?? phase}` : "",
    current ? "当前回合" : "",
    focusPlayers.includes(name) ? "当前焦点" : ""
  ].filter(Boolean).join(" · ");
  button.append(art, el("span", { class: "sr-only" }, [stateText]));
  // The native Photo keeps these numbers off the frame; hovering reveals them.
  button.title = [playerHandLabel(session.state, name, evaluation),
    name !== session.state.selfName ? targetRangeLabel(session.state, session.state.selfName, name, evaluation) : ""]
    .filter(Boolean).join(" · ");
  button.addEventListener("click", () => bind.togglePlayer(name));
  const wrap = el("div", { class: `${kind}-wrap` });
  wrap.dataset.player = name;
  wrap.dataset.alive = player?.alive === false ? "false" : "true";
  wrap.dataset.seat = String(asNumber(player?.seat, -1));
  if (typeof handCount === "number" || typeof handCount === "string")
    wrap.dataset.handCount = String(asNumber(handCount));
  wrap.setAttribute("aria-label", seatLabel(bind, name, 0));
  wrap.append(button);
  return wrap;
}

// MagatamasBoxItem: lost HP slots first, then the remaining ones; above five
// maximum HP a single magatama carries the numbers instead.
function magatamaBox(hp: number, maxHp: number): HTMLElement {
  const box = el("span", { class: "magatamas" });
  if (maxHp <= 0)
    return box;
  const index = hp >= maxHp ? 5 : Math.max(0, Math.min(5, hp));
  if (maxHp <= 5) {
    for (let i = 0; i < maxHp; ++i)
      box.append(assetImg([magatamaUrl(i < maxHp - hp ? 0 : index)], "", "magatama"));
    return box;
  }
  box.classList.add("numeric");
  box.dataset.level = String(index);
  box.append(assetImg([magatamaUrl(index)], "", "magatama"),
    el("span", { class: "hp-text" }, [String(hp)]), el("span", { class: "hp-text" }, ["/"]),
    el("span", { class: "hp-text" }, [String(maxHp)]));
  return box;
}

function equipBar(bind: UiBind, cardId: number): HTMLElement {
  const label = cardLabel(bind, cardId);
  const bar = el("span", { class: "equip-bar", title: label }, [
    el("span", { class: "equip-name" }, [label]),
    assetImg(equipImageUrls(cardObjectName(bind, cardId), true), "", "equip-image")
  ]);
  // Suit and point sit on the right end of the bar, as in the Photo equip area.
  const { suit, number } = cardSuitNumber(bind, cardId);
  if (number > 0 && number <= 13)
    bar.append(assetImg([cardNumberUrl(number, suit === "heart" || suit === "diamond")], "", "equip-point"));
  if (cardSuitUrl(suit))
    bar.append(assetImg([cardSuitUrl(suit)], "", "equip-suit"));
  return bar;
}

function judgeIcons(bind: UiBind, ids: number[]): HTMLElement {
  const row = el("span", { class: "photo-judge" });
  for (const id of ids) {
    const icon = assetImg(judgeIconUrls(cardObjectName(bind, id)), "", "judge-icon");
    icon.title = cardLabel(bind, id);
    row.append(icon);
  }
  return row;
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
  appendGroup("处理区", tableIds);
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
  const seats = el("div", { class: "photos seat-ring", role: "list", "aria-label": "座位环" });
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
  // Like RoomScene, photos that cannot be chosen darken only while choosing targets.
  seats.classList.toggle("targeting", legalCount > 0);
  if (!ring.some((name) => name !== bind.session.state.selfName))
    seats.append(el("p", { class: "status" }, ["等待其他角色"]));
  const seatToolbar = el("nav", { class: "seat-toolbar", "aria-label": "座位浏览" });
  const focus = uiFocusPlayer(bind);
  const filterButton = el("button", { type: "button", class: seatFilter === "legal" ? "active" : "" },
    [seatFilter === "legal" ? `可选目标 ${legalCount} / ${ring.length}` : `全部座位（可选 ${legalCount}）`]);
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
    ["当前焦点", () => { if (focus) jump(focus); }], ["回到自己", () => {
      ui.presentation ??= {};
      ui.presentation.browsedSeatIndex = -1;
      document.querySelector<HTMLElement>(".dashboard .dash")?.focus({ preventScroll: true });
    }] ] as const) {
    const button = el("button", { type: "button" }, [label]);
    button.disabled = label === "当前焦点" && !focus;
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
  panel.append(el("h2", { id: "game-result-heading" }, [result.draw ? "平局" : "游戏结束"]));
  if (result.selfOutcome === "victory")
    panel.append(el("p", { class: "game-result-outcome victory" }, ["你方胜利"]));
  else if (result.selfOutcome === "defeat")
    panel.append(el("p", { class: "game-result-outcome defeat" }, ["你方败北"]));
  if (result.draw)
    panel.append(el("p", { class: "status" }, ["本局没有胜方"]));
  else if (result.winners.length) {
    panel.append(el("p", { class: "status" }, ["胜方"]));
    const list = el("ul", { class: "game-result-winners" });
    for (const winner of result.winners) {
      const name = winner.objectName
        ? (asString(session.state.player(winner.objectName)?.screen_name) || winner.objectName)
        : "";
      const role = winner.role ? `（${resultRoleLabel(winner.role)}）` : "";
      list.append(el("li", {}, [`${name || resultRoleLabel(winner.role)}${name ? role : ""}`]));
    }
    panel.append(list);
  } else
    panel.append(el("p", { class: "status" }, ["胜方信息未提供"]));
  return panel;
}

export function logView(bind: UiBind): HTMLElement {
  const { session, ui } = bind;
  const panel = el("section", { class: "log-panel", "aria-label": "战报与房内聊天" });
  const battle = el("div", { class: "log battle-log", role: "log", "aria-label": "战报" });
  const chat = el("div", { class: "log chat-log", role: "log", "aria-label": "房内聊天" });
  ui.presentation ??= {};
  ui.presentation.mobileLogCollapsed ??= window.matchMedia("(max-width: 719px), (orientation: portrait)").matches;
  const filter = ui.presentation.battleFilter ?? "all";
  const filterBar = el("div", { class: "battle-filters", role: "toolbar", "aria-label": "战报筛选" });
  const filterNames: Array<[typeof filter, string]> = [["all", "全部"], ["mine", "我的"], ["damage", "伤害"], ["skill", "技能"]];
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
    // Native GameEventStream already supplies the authoritative formatted text;
    // chat stays as typed, battle lines follow the simplified display tables.
    const line = event.command === Command.SPEAK ? event.text : toSimplified(event.text);
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
  const draft = el("input", { id: "room-chat", class: "chat-draft", "data-focus-key": "room-chat", placeholder: "房内聊天", "aria-label": "房内聊天" });
  draft.value = ui.presentation?.chatDraft ?? "";
  draft.addEventListener("input", () => {
    ui.presentation ??= {};
    ui.presentation.chatDraft = draft.value;
  });
  const send = el("button", { type: "submit" }, ["发送"]);
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
  const collapse = el("button", { type: "button", class: "log-collapse", "aria-expanded": String(!collapsed), "aria-label": collapsed ? "展开战报与聊天" : "收起战报与聊天" }, [collapsed ? "展开消息" : "收起消息"]);
  collapse.addEventListener("click", () => {
    ui.presentation ??= {};
    ui.presentation.mobileLogCollapsed = !ui.presentation.mobileLogCollapsed;
    bind.render();
  });
  if (collapsed)
    panel.classList.add("is-collapsed");
  panel.append(el("h2", { class: "sr-only" }, ["消息流"]), collapse, filterBar,
    el("h2", { class: "log-header" }, ["战报"]), battle,
    el("h2", { class: "log-header" }, ["聊天"]), chat, chatForm);
  return panel;
}

// QSanSkillButton art per frequency. The Web client only has the description,
// whose leading tag names the same frequency as the native skill.
function skillButtonType(description: string): string {
  if (description.startsWith("锁定技")) return "compulsory";
  if (description.startsWith("觉醒技")) return "awaken";
  if (description.startsWith("限定技")) return "oneoff";
  if (description.startsWith("转换技")) return "change";
  return "proactive";
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
  // The dock uses wide, medium or narrow buttons like the native skill dock.
  const columns = skills.length === 1 ? 1 : skills.length % 2 === 0 && skills.length < 6 ? 2 : 3;
  row.dataset.columns = String(columns);
  for (const skill of skills) {
    const selected = ui.selectedOption === skill.name && ui.skillInstance === skill.instanceId;
    const desc = skillDescription(skill.name);
    const detail = evaluation?.known
      ? evaluation.skills.find((candidate) =>
        candidate.name === skill.name && candidate.instance_id === skill.instanceId)
      : undefined;
    const candidate = rules.actionModel()?.skills.find((item) => item.id === JSON.stringify([skill.name, skill.instanceId]));
    const button = el("button", {
      class: `skill-btn${selected ? " primary" : ""}`,
      title: skillHint(detail, desc || tr(skill.name))
    }, [tr(skill.name)]);
    const type = skillButtonType(desc);
    for (const state of ["normal", "hover", "down", "disabled"])
      button.style.setProperty(`--skill-${state}`, `url("${skillButtonUrl(type, columns, state)}")`);
    const available = !!candidate?.enabled;
    // Descriptions remain visible; only native ViewAs candidates can be activated.
    if (!selected && (!nativeRules || !available))
      button.disabled = true;
    button.addEventListener("click", () => {
      const current = rules.current(session, bind.rulesSelection()) ? rules.result : null;
      const currentAction = rules.actionModel()?.skills.find((item) => item.id === JSON.stringify([skill.name, skill.instanceId]));
      if (!selected && (!current?.known || !currentAction?.enabled))
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
  return row;
}

function selectedSkillDescription(bind: UiBind): HTMLElement | null {
  const { ui } = bind;
  if (!ui.selectedOption)
    return null;
  return el("p", { class: "skill-desc" }, [
    skillDescription(ui.selectedOption) || `${tr(ui.selectedOption)}（无技能描述）`
  ]);
}

// Hand cards first, then any private pile the player may pick from, all in one
// strip like the native Dashboard hand area.
function handView(bind: UiBind): HTMLElement {
  const hand = el("div", { class: "hand" });
  const self = bind.session.state.selfName;
  let index = 0;
  const append = (id: number) => {
    const card = el("div", { class: "hand-card" });
    card.style.setProperty("--hand-index", String(index++));
    card.append(renderCard(bind, id, bind.ui.selectedCards.includes(id), false, -1, bind.isCardClickable(id), !bind.isCardClickable(id)));
    hand.append(card);
  };
  bind.session.state.cardsForPlayer(self, PLACE_HAND).forEach(append);
  const piles = bind.session.state.playerValue(self, "piles");
  if (isObject(piles)) {
    for (const [name, value] of Object.entries(piles)) {
      const ids = asNumberList(value);
      if (ids.length === 0)
        continue;
      hand.append(el("span", { class: "pile-label" }, [tr(name)]));
      ids.forEach(append);
    }
  }
  hand.style.setProperty("--hand-count", String(index));
  return hand;
}

function dashboardEquips(bind: UiBind, self: string): HTMLElement {
  const equips = el("div", { class: "dash-equips" });
  const player = bind.session.state.player(self);
  equips.append(el("strong", { class: "dash-screen-name" }, [asString(player?.screen_name, self)]));
  for (const id of bind.session.state.cardsForPlayer(self, PLACE_EQUIP)) {
    const card = renderCard(bind, id, bind.ui.selectedCards.includes(id), false, -1, bind.isCardClickable(id), !bind.isCardClickable(id));
    // Equipment is a bar in the left frame, not a card face.
    card.classList.add("equip-card");
    card.querySelector("img")?.replaceWith(assetImg(equipImageUrls(cardObjectName(bind, id), false), "", "equip-image"));
    equips.append(card);
  }
  return equips;
}

// Frame and platter art resolves through package aliases like every other image.
function setDashboardSkin(dash: HTMLElement): void {
  const skin: Record<string, string> = {
    "--dash-equip-bg": "fullskin/system/dashboard-equip.png",
    "--dash-hand-bg": "fullskin/system/dashboard-hand.png",
    "--platter-bg": "system/button/platter/bg.png"
  };
  for (const button of ["confirm", "cancel", "discard", "trust"]) {
    for (const state of ["normal", "hover", "down", "disabled"])
      skin[`--platter-${button}-${state}`] = `system/button/platter/${button}/${state}.png`;
  }
  for (const [name, path] of Object.entries(skin))
    dash.style.setProperty(name, `url("${skinImageUrl(path)}")`);
}

export function dashboardView(bind: UiBind): HTMLElement {
  const dash = el("section", { class: "dashboard" });
  setDashboardSkin(dash);
  const self = bind.session.state.selfName;
  const handArea = el("div", { class: "dash-hand" });
  const tricks = bind.session.state.cardsForPlayer(self, PLACE_DELAYED_TRICK);
  if (tricks.length)
    handArea.append(judgeIcons(bind, tricks));
  handArea.append(handView(bind));
  // Confirm, cancel, finish and trust sit on the platter between hand and avatar.
  const interaction = interactionView(bind);
  interaction.classList.add("dashboard-interaction");
  if (!bind.session.interaction)
    interaction.classList.add("idle");
  const platter = el("div", { class: "dash-platter" });
  const actions = interaction.querySelector(".interaction-actions");
  if (actions)
    platter.append(actions);
  const description = selectedSkillDescription(bind);
  if (description)
    interaction.querySelector(".interaction-header")?.append(description);
  const avatar = photoCard(bind, self, "dash");
  avatar.append(skillBar(bind));
  dash.append(dashboardEquips(bind, self), handArea, platter, avatar, interaction);
  return dash;
}
