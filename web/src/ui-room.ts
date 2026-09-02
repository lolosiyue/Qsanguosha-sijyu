import {
  assetImg,
  fullskinUrls,
  kingdomIconUrls,
  magatamaUrl,
  roleIconUrls
} from "./assets";
import { formatPresentationEvent, logPlayerName } from "./log-text";
import { tr } from "./i18n";
import {
  Command,
  PLACE_DELAYED_TRICK,
  PLACE_EQUIP,
  PLACE_HAND,
  PLACE_JUDGE,
  PLACE_TABLE,
  asNumber,
  asNumberList,
  asString,
  isObject
} from "./protocol";
import type { PlayerState } from "./state";
import { renderCard, skillBaseName, visibleSkills } from "./ui-cards";
import { el } from "./ui-dom";
import { interactionView } from "./ui-interaction";
import type { UiBind } from "./ui-types";

function playerGeneralName(player: PlayerState | undefined): string {
  return asString(player?.general) || asString(player?.avatar);
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
  const button = el("button", {
    class: `${kind}${name === session.state.selfName ? " self" : ""}${player?.alive === false ? " dead" : ""}${selected ? " selected" : ""}${kind === "photo" && !clickable ? " disabled" : ""}`
  });
  button.type = "button";
  const art = el("div", { class: "photo-art" });
  if (general)
    art.append(assetImg(fullskinUrls(general), "", "fullskin"));
  const kingdom = asString(player?.kingdom);
  if (kingdom)
    art.append(assetImg(kingdomIconUrls(kingdom), "", "kingdom"));
  const role = asString(player?.role) || "unknown";
  art.append(assetImg(roleIconUrls(role), "", "role"));
  const hp = asNumber(player?.hp);
  const maxHp = asNumber(player?.max_hp);
  const mag = el("div", { class: "magatamas" });
  mag.append(assetImg([magatamaUrl(hp)], "", "magatama"));
  mag.append(el("span", {}, [`${hp}/${maxHp}`]));
  const handCount = asNumber(session.state.playerValue(name, "hand_count"));
  const meta = el("div", { class: "photo-meta" });
  meta.append(
    el("strong", { class: "screen-name" }, [asString(player?.screen_name, name)]),
    el("div", { class: "general-name" }, [tr(general)]),
    mag,
    el("div", { class: "hand-count" }, [`手${handCount}`])
  );
  art.append(meta);
  button.append(art);
  button.addEventListener("click", () => bind.togglePlayer(name));
  const wrap = el("div", { class: `${kind}-wrap` });
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
  const table = el("div", { class: "table" });
  const seats = el("div", { class: "photos" });
  for (const name of bind.session.state.playerNames) {
    if (name === bind.session.state.selfName)
      continue;
    seats.append(photoCard(bind, name, "photo"));
  }
  if (!seats.childElementCount)
    seats.append(el("p", { class: "status" }, ["等待其他角色"]));
  table.append(seats, tablePileView(bind));
  return table;
}

export function logView(bind: UiBind): HTMLElement {
  const { session, ui } = bind;
  const log = el("div", { class: "log" });
  for (const event of session.state.presentationEvents.slice(-80)) {
    const line = formatPresentationEvent(event, (name) =>
      event.command === Command.SPEAK
        ? (asString(session.state.player(name)?.screen_name) || name)
        : logPlayerName(session.state, name));
    if (line)
      log.append(el("p", {}, [line]));
  }
  log.addEventListener("scroll", () => {
    ui.logPinned = log.scrollHeight - log.scrollTop - log.clientHeight < 32;
  });
  requestAnimationFrame(() => {
    if (ui.logPinned)
      log.scrollTop = log.scrollHeight;
  });
  return log;
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
  const { ui } = bind;
  const row = el("div", { class: "skill-bar" });
  const skills = visibleSkills(bind);
  if (skills.length === 0)
    return row;
  for (const skill of skills) {
    const selected = ui.selectedOption === skill.name;
    const desc = skillDescription(skill.name);
    const button = el("button", {
      class: `skill-btn${selected ? " primary" : ""}`,
      title: desc || tr(skill.name)
    }, [tr(skill.name)]);
    button.addEventListener("click", () => {
      if (ui.selectedOption === skill.name) {
        ui.selectedOption = "";
        ui.skillInstance = 0;
      } else {
        ui.selectedOption = skill.name;
        ui.skillInstance = skill.instanceId;
      }
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
  if (ids.length === 0)
    hand.append(el("span", { class: "status" }, ["手牌空"]));
  for (const id of ids)
    hand.append(renderCard(bind, id, bind.ui.selectedCards.includes(id), false, -1, bind.isCardClickable(id), !bind.isCardClickable(id)));
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
  body.append(equips, pileRow(bind, self), skillBar(bind), handView(bind), interactionView(bind));
  dash.append(body);
  return dash;
}
