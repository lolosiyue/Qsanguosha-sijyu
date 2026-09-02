import {
  CARD_BACK_URL,
  UNKNOWN_CARD_URL,
  assetImg,
  cardFaceUrl
} from "./assets";
import { cardRecord, tr } from "./i18n";
import { useMode } from "./eligibility";
import { asBool, asNumber, asString, asStringList, isObject } from "./protocol";
import { el } from "./ui-dom";
import type { UiBind } from "./ui-types";

export function cardNumberText(value: number): string {
  if (value === 1)
    return "A";
  if (value === 11)
    return "J";
  if (value === 12)
    return "Q";
  if (value === 13)
    return "K";
  return value > 0 ? String(value) : "";
}

export function cardObjectName(bind: UiBind, cardId: number): string {
  const card = bind.session.state.card(cardId);
  const catalog = cardRecord(cardId);
  return asString(card?.object_name)
    || asString(card?.card_name)
    || asString(catalog?.object_name);
}

export function cardLabel(bind: UiBind, cardId: number): string {
  const card = bind.session.state.card(cardId);
  const catalog = cardRecord(cardId);
  const raw = asString(card?.object_name)
    || asString(card?.card_name)
    || asString(catalog?.object_name)
    || `#${cardId}`;
  const name = tr(raw);
  const suitRaw = card?.suit ?? catalog?.suit;
  const suit = typeof suitRaw === "number"
    ? ["spade", "club", "heart", "diamond"][suitRaw] ?? ""
    : asString(suitRaw);
  const number = asNumber(card?.number, asNumber(catalog?.number));
  if (!suit && number <= 0)
    return name;
  return `${name}[${tr(suit)}${cardNumberText(number)}]`;
}

export function visibleSkills(bind: UiBind): { name: string; instanceId: number }[] {
  const { session } = bind;
  const instances = session.state.playerValue(session.state.selfName, "skill_instances");
  if (isObject(instances)) {
    const result: { name: string; instanceId: number }[] = [];
    for (const value of Object.values(instances)) {
      if (!isObject(value) || asBool(value.visible, true) === false)
        continue;
      const name = asString(value.skill_name);
      if (name)
        result.push({ name, instanceId: asNumber(value.instance_id) });
    }
    if (result.length)
      return result;
  }
  return asStringList(session.state.playerValue(session.state.selfName, "skills"))
    .map((name) => ({ name, instanceId: 0 }));
}

export function skillBaseName(skillName: string): string {
  return skillName.replace(/^#/u, "").replace(/&$/u, "");
}

export function renderCard(
  bind: UiBind,
  cardId: number,
  selected: boolean,
  hidden = false,
  hiddenIndex = -1,
  selectable = true,
  dim = false
): HTMLButtonElement {
  const { session, ui } = bind;
  const button = el("button", {
    class: `card${selected ? " selected" : ""}${hidden ? " hidden" : ""}${!selectable && dim ? " disabled" : ""}${!selectable && !dim ? " display" : ""}`
  });
  button.type = "button";
  const objectName = cardObjectName(bind, cardId);
  const face = hidden
    ? assetImg([CARD_BACK_URL], CARD_BACK_URL)
    : assetImg([cardFaceUrl(objectName)], UNKNOWN_CARD_URL);
  const caption = el("span", { class: "card-caption" }, [hidden ? "暗牌" : cardLabel(bind, cardId)]);
  button.append(face, caption);
  if (!selectable)
    return button;
  button.addEventListener("click", () => {
    const mode = session.interaction ? useMode(session.interaction.command) : "free";
    if (hidden) {
      ui.selectedCards = [-1];
      ui.hiddenIndex = hiddenIndex;
    } else if (mode === "play" || mode === "response") {
      ui.selectedCards = ui.selectedCards.includes(cardId) ? [] : [cardId];
      ui.selectedPlayers = [];
      ui.hiddenIndex = -1;
    } else if (ui.selectedCards.includes(cardId)) {
      ui.selectedCards = ui.selectedCards.filter((id) => id !== cardId);
      ui.hiddenIndex = -1;
    } else {
      ui.selectedCards = [...ui.selectedCards, cardId];
      ui.hiddenIndex = -1;
    }
    bind.render();
  });
  return button;
}
