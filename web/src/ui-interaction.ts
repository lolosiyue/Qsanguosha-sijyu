import { assetImg, generalFaceUrls } from "./assets";
import { targetsAreFeasible, useMode } from "./eligibility";
import { formatInteractionPrompt, tr } from "./i18n";
import { logPlayerName } from "./log-text";
import {
  Command,
  PLACE_DELAYED_TRICK,
  PLACE_EQUIP,
  PLACE_HAND,
  asBool,
  asNumber,
  asNumberList,
  asString,
  asStringList,
  type JsonObject
} from "./protocol";
import { replyForCommand } from "./replies";
import { renderCard, skillBaseName, visibleSkills } from "./ui-cards";
import { waitingRoom } from "./ui-connect";
import { el, hasCommand } from "./ui-dom";
import type { UiBind } from "./ui-types";

const INTERACTION_TITLES: Record<number, string> = {
  [Command.CHOOSE_ROLE]: "分配身分",
  [Command.CHOOSE_GENERAL]: "選擇武將",
  [Command.ASK_GENERAL]: "選擇武將",
  [Command.CHOOSE_DIRECTION]: "選擇座次方向",
  [Command.PLAY_CARD]: "出牌",
  [Command.RESPONSE_CARD]: "打出牌",
  [Command.DISCARD_CARD]: "棄牌",
  [Command.EXCHANGE_CARD]: "換牌",
  [Command.ASK_PEACH]: "求桃",
  [Command.NULLIFICATION]: "無懈可擊",
  [Command.MULTIPLE_CHOICE]: "選擇",
  [Command.INVOKE_SKILL]: "發動技能",
  [Command.CHOOSE_PLAYER]: "選擇角色",
  [Command.CHOOSE_CARD]: "選擇卡牌",
  [Command.CHOOSE_SUIT]: "選擇花色",
  [Command.CHOOSE_KINGDOM]: "選擇勢力",
  [Command.AMAZING_GRACE]: "五穀豐登",
  [Command.SKILL_GUANXING]: "觀星",
  [Command.SKILL_GONGXIN]: "攻心",
  [Command.SKILL_YIJI]: "遺計",
  [Command.PINDIAN]: "拼點",
  [Command.TRIGGER_ORDER]: "技能發動順序",
  [Command.ARRANGE_GENERAL]: "排列武將",
  [Command.LUCK_CARD]: "手氣卡",
  [Command.SURRENDER]: "投降",
  [Command.CHOOSE_ORDER]: "選擇順序",
  [Command.CHOOSE_ROLE_3V3]: "選擇身分",
  [Command.SHOW_CARD]: "展示牌",
  [Command.QML_INTERACT]: "互動"
};

function playCardText(bind: UiBind, cardIds: number[], skillName: string): string {
  const ids = cardIds.filter((id) => id >= 0);
  const sub = ids.length > 0 ? ids.join("+") : ".";
  if (skillName) {
    const base = skillBaseName(skillName);
    if (base.includes("_")
        || /^(nos|ol|tw|mobile|tenyear|jx|yj|js|sp|mt|mou|new)/u.test(base))
      return `#${base}:${sub}:`;
    const klass = `${base.charAt(0).toUpperCase()}${base.slice(1)}Card`;
    return `@${klass}=${sub}`;
  }
  if (ids.length === 1) {
    const card = bind.session.state.card(ids[0]);
    return asString(card?.card_string) || String(ids[0]);
  }
  return "";
}

function optionButtons(bind: UiBind, values: string[], onPick: (value: string) => void): HTMLElement {
  const row = el("div", { class: "cards" });
  for (const value of values) {
    const button = el("button", { class: bind.ui.selectedOption === value ? "primary" : "" }, [tr(value)]);
    button.addEventListener("click", () => onPick(value));
    row.append(button);
  }
  return row;
}

function generalPicker(bind: UiBind, values: string[], onPick: (value: string) => void): HTMLElement {
  const row = el("div", { class: "general-pick" });
  for (const value of values) {
    const button = el("button", { class: bind.ui.selectedOption === value ? "primary" : "" });
    button.append(assetImg(generalFaceUrls(value), "", "portrait"), el("span", {}, [tr(value)]));
    button.addEventListener("click", () => onPick(value));
    row.append(button);
  }
  return row;
}

function payloadOptions(payload: JsonObject): string[] {
  return [
    ...asStringList(payload.options),
    ...asStringList(payload.candidates),
    ...asStringList(payload.roles),
    ...asStringList(payload.choices)
  ];
}

export function interactionView(bind: UiBind): HTMLElement {
  const { session, ui } = bind;
  const root = el("section", { class: "prompt" });
  const interaction = session.interaction;
  if (!interaction) {
    if (!asBool(session.state.gameValue("started")))
      return waitingRoom(bind);
    root.append(el("p", { class: "status" }, ["等待詢問"]));
    const trust = el("button", {}, ["託管"]);
    trust.addEventListener("click", () => session.trust(true));
    const surrender = el("button", { class: "danger" }, ["投降"]);
    surrender.addEventListener("click", () => session.surrender());
    root.append(trust, surrender);
    return root;
  }
  const { command, payload, messageId } = interaction;
  root.append(el("h2", {}, [INTERACTION_TITLES[command] ?? `詢問 ${command}`]));
  const prompt = asString(payload.prompt) || asString(payload.skill_name);
  if (prompt) {
    const text = asString(payload.prompt)
      ? formatInteractionPrompt(prompt, (name) => logPlayerName(session.state, name))
      : tr(prompt);
    root.append(el("p", {}, [text]));
  }
  if (session.interactionError)
    root.append(el("p", { class: "error" }, [session.interactionError]));

  const submit = (builder: () => JsonObject) => {
    try {
      session.sendReply(command, messageId, builder());
      bind.resetSelection();
    } catch (error) {
      session.interactionError = error instanceof Error ? error.message : String(error);
      bind.render();
    }
  };

  const cancel = el("button", {}, ["取消"]);
  cancel.addEventListener("click", () => submit(() => replyForCommand(command, { cancelled: true })));

  if (hasCommand(command, [Command.CHOOSE_GENERAL, Command.ASK_GENERAL])) {
    const options = payloadOptions(payload);
    root.append(generalPicker(bind, options, (value) => {
      ui.selectedOption = value;
      submit(() => replyForCommand(command, { option: value }));
    }));
    root.append(cancel);
    return root;
  }

  if (hasCommand(command, [
    Command.MULTIPLE_CHOICE,
    Command.CHOOSE_SUIT, Command.CHOOSE_KINGDOM, Command.CHOOSE_DIRECTION,
    Command.TRIGGER_ORDER, Command.CHOOSE_ROLE_3V3
  ])) {
    const options = payloadOptions(payload);
    root.append(optionButtons(bind, options, (value) => {
      ui.selectedOption = value;
      submit(() => replyForCommand(command, { option: value }));
    }));
    root.append(cancel);
    return root;
  }

  if (command === Command.INVOKE_SKILL || command === Command.SURRENDER || command === Command.LUCK_CARD) {
    const yes = el("button", { class: "primary" }, ["是"]);
    const no = el("button", {}, ["否"]);
    yes.addEventListener("click", () => submit(() => replyForCommand(command, { bool: true })));
    no.addEventListener("click", () => submit(() => replyForCommand(command, { bool: false })));
    root.append(yes, no);
    return root;
  }

  if (command === Command.CHOOSE_ORDER) {
    root.append(optionButtons(bind, ["0", "1"], (value) => {
      submit(() => replyForCommand(command, { int: Number(value) }));
    }));
    return root;
  }

  if (command === Command.CHOOSE_ROLE) {
    const players = session.state.playerNames;
    const roles = ["lord", "loyalist", "rebel", "renegade"];
    for (const player of players) {
      const select = el("select");
      for (const role of roles) {
        const option = el("option", { value: role }, [tr(role)]);
        if (ui.assignments[player] === role)
          option.selected = true;
        select.append(option);
      }
      select.addEventListener("change", () => {
        ui.assignments[player] = select.value;
      });
      root.append(el("label", {}, [`${player} `, select]));
    }
    const ok = el("button", { class: "primary" }, ["送出"]);
    ok.addEventListener("click", () => submit(() => {
      const assignments: Record<string, string> = {};
      for (const player of players)
        assignments[player] = ui.assignments[player] || roles[0];
      return replyForCommand(command, { assignments });
    }));
    root.append(ok, cancel);
    return root;
  }

  if (command === Command.CHOOSE_PLAYER) {
    root.append(el("p", {}, ["點 Photo 或自身頭像選玩家"]));
    const ok = el("button", { class: "primary" }, ["送出"]);
    ok.addEventListener("click", () => submit(() => replyForCommand(command, { players: ui.selectedPlayers })));
    root.append(ok, cancel);
    return root;
  }

  if (command === Command.SKILL_GUANXING) {
    const ids = asNumberList(payload.card_ids ?? payload.cards);
    if (ui.top.length === 0 && ui.bottom.length === 0)
      ui.top = [...ids];
    root.append(el("p", {}, ["上：點牌移到下"]));
    const row = el("div", { class: "cards" });
    for (const id of ui.top) {
      const card = renderCard(bind, id, false);
      card.addEventListener("click", () => {
        ui.top = ui.top.filter((item) => item !== id);
        ui.bottom.push(id);
        bind.render();
      }, { once: true });
      row.append(card);
    }
    const bottom = el("div", { class: "cards" });
    for (const id of ui.bottom)
      bottom.append(renderCard(bind, id, true));
    const ok = el("button", { class: "primary" }, ["確定"]);
    ok.addEventListener("click", () => submit(() => replyForCommand(command, { top: ui.top, bottom: ui.bottom })));
    root.append(row, el("p", {}, ["下"]), bottom, ok);
    return root;
  }

  if (command === Command.SKILL_YIJI) {
    const ids = asNumberList(payload.card_ids);
    root.append(el("p", {}, ["選手牌再點座位"]));
    const row = el("div", { class: "cards" });
    for (const id of ids)
      row.append(renderCard(bind, id, ui.selectedCards.includes(id)));
    const ok = el("button", { class: "primary" }, ["交給所選玩家"]);
    ok.addEventListener("click", () => submit(() => replyForCommand(command, {
      cardIds: ui.selectedCards,
      yijiTarget: ui.selectedPlayers[0] ?? ""
    })));
    root.append(row, ok, cancel);
    return root;
  }

  if (command === Command.ARRANGE_GENERAL) {
    const generals = asStringList(payload.generals ?? payload.general_names ?? payload.candidates);
    root.append(generalPicker(bind, generals, (value) => {
      ui.selectedOption = value;
      bind.render();
    }));
    const ok = el("button", { class: "primary" }, ["送出順序（點選後按）"]);
    ok.addEventListener("click", () => submit(() => replyForCommand(command, {
      generals: ui.selectedOption ? [ui.selectedOption, ...generals.filter((item) => item !== ui.selectedOption)] : generals
    })));
    root.append(ok, cancel);
    return root;
  }

  if (command === Command.QML_INTERACT) {
    const area = el("textarea");
    area.value = ui.qmlText;
    area.rows = 6;
    const ok = el("button", { class: "primary" }, ["送出 JSON"]);
    ok.addEventListener("click", () => {
      ui.qmlText = area.value;
      submit(() => replyForCommand(command, { qml: JSON.parse(area.value) as JsonObject }));
    });
    root.append(el("p", {}, ["未知 QML type 請填 JSON 或取消"]), area, ok, cancel);
    return root;
  }

  if (command === Command.CHOOSE_CARD) {
    const target = asString(payload.player);
    const flags = asString(payload.zone_flags) || "hej";
    const visible = asBool(payload.hand_cards_visible);
    const disabled = new Set(asNumberList(payload.disabled_card_ids));
    const row = el("div", { class: "cards" });
    const appendKnown = (ids: number[]) => {
      for (const id of ids) {
        if (disabled.has(id))
          continue;
        row.append(renderCard(bind, id, ui.selectedCards.includes(id)));
      }
    };
    if (flags.includes("e"))
      appendKnown(session.state.cardsForPlayer(target, PLACE_EQUIP));
    if (flags.includes("j"))
      appendKnown(session.state.cardsForPlayer(target, PLACE_DELAYED_TRICK));
    if (flags.includes("h")) {
      const handCount = asNumber(session.state.playerValue(target, "hand_count"));
      if (visible)
        appendKnown(session.state.cardsForPlayer(target, PLACE_HAND));
      else {
        for (let index = 0; index < handCount; index++)
          row.append(renderCard(bind, -1, ui.hiddenIndex === index, true, index));
      }
    }
    const ok = el("button", { class: "primary" }, ["選這張"]);
    ok.addEventListener("click", () => submit(() => replyForCommand(command, { cardId: ui.selectedCards[0] ?? -1 })));
    root.append(row, ok, cancel);
    return root;
  }

  if (hasCommand(command, [Command.SKILL_GONGXIN, Command.AMAZING_GRACE])) {
    const ids = [...new Set(asNumberList(payload.card_ids).concat(asNumberList(payload.enabled_card_ids)))];
    const row = el("div", { class: "cards" });
    for (const id of ids)
      row.append(renderCard(bind, id, ui.selectedCards.includes(id)));
    const ok = el("button", { class: "primary" }, ["送出"]);
    ok.addEventListener("click", () => submit(() => replyForCommand(command, { cardId: ui.selectedCards[0] ?? 0 })));
    root.append(row, ok, cancel);
    return root;
  }

  if (hasCommand(command, [
    Command.PLAY_CARD, Command.RESPONSE_CARD, Command.ASK_PEACH,
    Command.NULLIFICATION, Command.SHOW_CARD, Command.PINDIAN,
    Command.EXCHANGE_CARD, Command.DISCARD_CARD
  ])) {
    const skills = visibleSkills(bind);
    if (command === Command.PLAY_CARD && skills.length) {
      const selected = ui.selectedOption ? tr(ui.selectedOption) : "未選";
      root.append(el("p", {}, [`技能（再點一次取消）：${selected}`]));
    }
    const extras = [...new Set(
      asNumberList(payload.card_ids).concat(asNumberList(payload.enabled_card_ids))
    )].filter((id) => {
      const owner = asString(session.state.card(id)?.owner);
      return owner !== session.state.selfName;
    });
    if (extras.length) {
      const row = el("div", { class: "cards" });
      for (const id of extras)
        row.append(renderCard(bind, id, ui.selectedCards.includes(id), false, -1, bind.isCardClickable(id), !bind.isCardClickable(id)));
      root.append(el("p", {}, ["額外可選牌"]), row);
    }
    const ok = el("button", { class: "primary" }, ["送出"]);
    const mode = useMode(command);
    const cardId = bind.currentCardId();
    const playBlocked = mode === "play" && !ui.selectedOption
      && (cardId < 0 || !targetsAreFeasible(session.state, session.state.selfName, cardId, ui.selectedPlayers));
    const responseBlocked = mode === "response" && cardId < 0;
    if (playBlocked || responseBlocked)
      ok.setAttribute("disabled", "true");
    ok.addEventListener("click", () => {
      if (command === Command.EXCHANGE_CARD || command === Command.DISCARD_CARD) {
        submit(() => replyForCommand(command, { cardIds: ui.selectedCards }));
        return;
      }
      submit(() => replyForCommand(command, {
        cardText: playCardText(bind, ui.selectedCards.filter((id) => id >= 0), ui.selectedOption),
        targets: ui.selectedPlayers,
        skillName: ui.selectedOption,
        instanceId: ui.skillInstance
      }));
    });
    const pass = command === Command.PLAY_CARD ? el("button", {}, ["結束出牌"]) : cancel;
    if (command === Command.PLAY_CARD)
      pass.addEventListener("click", () => submit(() => replyForCommand(command, { cancelled: true })));
    root.append(el("p", {}, ["點 Dashboard 技能（可再點取消），選手牌／裝備，再點目標 Photo，然後送出"]), ok, pass);
    return root;
  }

  root.append(el("p", { class: "error" }, [`未覆蓋的互動 ${command}`]), cancel);
  return root;
}
